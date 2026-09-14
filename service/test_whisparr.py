"""Synthetic imports only: temporary SQLite/posters and a simulated display."""
import json
import threading
from unittest.mock import patch
import requests
import app
import unittest
import test_app

class WhisparrTests(unittest.TestCase):
 setUp = test_app.Tests.setUp
 tearDown = test_app.Tests.tearDown
 image = test_app.Tests.image
 response = test_app.Tests.response
 info = test_app.Tests.info
 def test_movie_compatibility_and_filter(self):
  for source in ('radarr','whisparr'):
   for item in ('movie','scene'):
    for upgrade in (False,True):
     event=app.parse_event(source,{'eventType':'Download','isUpgrade':upgrade,'movie':{'id':7,'title':'Synthetic import','itemType':item}})
     self.assertEqual((event['source'],event['id']),(source,7))
  for kind in ('Test','Grab','Rename','MovieAdded','MovieDelete','MovieFileDelete','Health','ManualInteractionRequired',None):
   self.assertIsNone(app.parse_event('whisparr',{'eventType':kind}))
  for value in (None,True,0,-1,'7','../7'):
   with self.assertRaises(ValueError):app.parse_event('whisparr',{'eventType':'Download','movie':{'id':value}})
  with self.assertRaises(ValueError):app.parse_event('whisparr',{'eventType':'Download','series':{'id':7}})

 def test_http_latest_across_sources_and_restart(self):
  server=app.ThreadingHTTPServer(('127.0.0.1',0),app.Handler)
  thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
  base=f'http://127.0.0.1:{server.server_port}/webhook/'
  try:
   for revision,(source,key) in enumerate((('sonarr','series'),('radarr','movie'),('whisparr','movie'),('sonarr','series')),1):
    response=requests.post(base+source+'/'+app.HOOK,json={'eventType':'Download',key:{'id':7,'title':'Synthetic import'}},timeout=5)
    self.assertEqual(response.status_code,202)
    app.initialize()
    with app.db() as c:row=c.execute('SELECT revision,payload FROM latest').fetchone()
    self.assertEqual(row[0],revision);self.assertEqual(json.loads(row[1])['source'],source)
   for kind in ('Test','Grab','Rename'):
    self.assertEqual(requests.post(base+'whisparr/'+app.HOOK,json={'eventType':kind},timeout=5).status_code,200)
   self.assertEqual(requests.post(base+'whisparr/wrong',json={'eventType':'Download','movie':{'id':7}},timeout=5).status_code,401)
   self.assertEqual(requests.post(base+'whisparr/'+app.HOOK,json={'eventType':'Download','movie':{'id':False}},timeout=5).status_code,400)
   with app.db() as c:self.assertEqual(c.execute('SELECT revision FROM latest').fetchone()[0],4)
  finally:server.shutdown();server.server_close();thread.join()

 def test_whisparr_poster_delivery_retry(self):
  root=app.DATA/'posters';p=root/'whisparr'/'7'/'poster.jpg';p.parent.mkdir(parents=True)
  event=app.parse_event('whisparr',{'eventType':'Download','movie':{'id':7}})
  app.enqueue(event)
  with patch.dict('os.environ',{'POSTER_ROOT':str(root)}),patch.object(app.requests,'get',return_value=self.info()),patch.object(app.requests,'post',return_value=self.response({})) as post:
   with self.assertRaises(ValueError):app.deliver_once()
   post.assert_not_called();p.write_bytes(self.image());app.initialize()
   self.assertTrue(app.deliver_once())
   self.assertEqual(post.call_args.kwargs['files']['image'][1],b'\x00\xf8'*54400)
   with app.db() as c:self.assertEqual(c.execute('SELECT delivered FROM latest').fetchone()[0],1)
   # Same numeric ID must never use another Arr instance's poster.
   with self.assertRaises(ValueError):app.poster({'source':'radarr','id':7})
