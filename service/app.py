"""Persistent latest-import mailbox; one worker serializes poster delivery."""
import hashlib, hmac, io, json, logging, os, sqlite3, struct, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import requests
from PIL import Image, ImageOps

LOG = logging.getLogger('cover')
DATA = Path(os.getenv('DATA_DIR', '/data'))
TOKEN = os.getenv('COVER_TOKEN', '')
DISPLAY = os.getenv('DISPLAY_URL', '').rstrip('/')
HOOK = os.getenv('WEBHOOK_TOKEN', '')

def db():
    conn = sqlite3.connect(DATA / 'state.sqlite', timeout=30)
    conn.execute('PRAGMA synchronous=FULL')
    return conn

def initialize():
    DATA.mkdir(parents=True, exist_ok=True)
    with db() as c:
        c.execute('CREATE TABLE IF NOT EXISTS latest (id INTEGER PRIMARY KEY CHECK(id=1), revision INTEGER, payload TEXT, delivered INTEGER DEFAULT 0, frame BLOB, digest TEXT)')

def parse_event(source, payload):
    if not isinstance(payload, dict): raise ValueError('Expected JSON object')
    event = payload.get('eventType')
    if event == 'Test': return None
    if event != 'Download': return None
    key = 'movie' if source in ('radarr', 'whisparr') else 'series'
    media = payload.get(key)
    if not isinstance(media, dict) or type(media.get('id')) is not int or media['id'] <= 0:
        raise ValueError('Import needs a positive media ID')
    return {'source': source, 'id': media['id'], 'title': str(media.get('title', ''))[:500],
            'tmdbId': media.get('tmdbId'), 'tvdbId': media.get('tvdbId'),
            'season': (payload.get('episode') or {}).get('seasonNumber'),
            'episode': (payload.get('episode') or {}).get('episodeNumber'),
            'year': media.get('year')}

def enqueue(event):
    with db() as c:
        c.execute('INSERT INTO latest(id,revision,payload) VALUES(1,1,?) ON CONFLICT(id) DO UPDATE SET revision=revision+1,payload=excluded.payload,delivered=0,frame=NULL,digest=NULL', (json.dumps(event),))

def rgb565(image_bytes, width, height):
    if (width, height) != (170, 320): raise ValueError('Unexpected display resolution')
    with Image.open(io.BytesIO(image_bytes)) as original:
        if original.width * original.height > 25_000_000: raise ValueError('Poster too large')
        im = ImageOps.fit(ImageOps.exif_transpose(original).convert('RGB'), (width, height), method=Image.Resampling.LANCZOS)
        return b''.join(struct.pack('<H', ((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in im.getdata())

def poster(event):
    source = event['source']
    if source not in ('radarr', 'sonarr', 'whisparr') or type(event['id']) is not int or event['id'] <= 0:
        raise ValueError('Invalid media identifier')
    root = Path(os.getenv('POSTER_ROOT', '/posters')) / source
    path = root / str(event['id']) / 'poster.jpg'
    if not path.is_file(): raise ValueError('Poster not yet cached by Arr')
    if path.stat().st_size > 15_000_000: raise ValueError('Poster exceeds limit')
    return path.read_bytes()

def deliver_once():
    with db() as c: row=c.execute('SELECT revision,payload,delivered,frame,digest FROM latest WHERE id=1').fetchone()
    if not row: return False
    revision, payload, delivered, frame, digest = row
    headers={'Authorization': 'Bearer '+TOKEN}
    info=requests.get(DISPLAY+'/info',headers=headers,timeout=15,allow_redirects=False)
    info.raise_for_status(); info=info.json()
    if not info.get('storage'): raise ValueError('Display storage needs initialization')
    if info.get('format') != 'rgb565le': raise ValueError('Unsupported display format')
    if frame is None:
        frame=rgb565(poster(json.loads(payload)),info['width'],info['height']); digest=hashlib.sha256(frame).hexdigest()
        with db() as c: c.execute('UPDATE latest SET frame=?,digest=? WHERE id=1 AND revision=?',(frame,digest,revision))
    # Superseded preparation must never replace a more recent queued import.
    with db() as c:
        if c.execute('SELECT revision FROM latest WHERE id=1').fetchone()[0] != revision: return False
    if info.get('sha256') != digest:
        event=json.loads(payload)
        params={'sha256':digest, 'title':event.get('title',''), 'season':event.get('season',''), 'episode':event.get('episode',''), 'year':event.get('year','')}
        r=requests.post(DISPLAY+'/cover',params=params,headers=headers,files={'image':('cover.raw',frame,'application/octet-stream')},timeout=30,allow_redirects=False)
        r.raise_for_status()
        if r.status_code != 200: raise ValueError('Upload not acknowledged')
    with db() as c: c.execute('UPDATE latest SET delivered=1 WHERE id=1 AND revision=?',(revision,))
    return True

def worker():
    while True:
        try: deliver_once()
        except Exception as e: LOG.warning('Delivery pending (%s); retry in 10 seconds',type(e).__name__)
        time.sleep(10)

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args): pass  # Webhook URL contains a shared secret.
    def reply(self, code, text):
        body=text.encode(); self.send_response(code); self.send_header('Content-Type','text/plain'); self.send_header('Content-Length',str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        self.reply(200,'ok') if self.path == '/health' else self.reply(404,'not found')
    def do_POST(self):
        self.connection.settimeout(10)
        route = urlsplit(self.path).path.split('/')
        if len(route)!=4 or route[1]!='webhook' or route[2] not in ('radarr','sonarr','whisparr') or not hmac.compare_digest(route[3],HOOK):
            self.reply(401,'unauthorized'); return
        try:
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<=262144: self.reply(413,'invalid size'); return
            data=self.rfile.read(size)
            if len(data)!=size: raise ValueError('Incomplete request')
            event=parse_event(route[2],json.loads(data))
            if event is None: self.reply(200,'test or ignored event'); return
            enqueue(event)
            self.reply(202,'saved for delivery')
        except (ValueError, TypeError): self.reply(400,'invalid import payload')
        except Exception: self.reply(503,'could not persist import; retry')

if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)
    if not DISPLAY or len(TOKEN)<24 or len(HOOK)<24: raise SystemExit('Set DISPLAY_URL, COVER_TOKEN and WEBHOOK_TOKEN (minimum 24 characters)')
    initialize()
    threading.Thread(target=worker,daemon=True).start()
    ThreadingHTTPServer(('0.0.0.0',8080),Handler).serve_forever()
