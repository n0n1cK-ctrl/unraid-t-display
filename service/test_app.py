import io,json,tempfile,unittest,threading
from pathlib import Path
from unittest.mock import patch,Mock
from PIL import Image
import requests,app
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();app.DATA=Path(self.tmp.name);app.initialize();app.TOKEN='a'*48;app.HOOK='b'*48;app.DISPLAY='http://display'
 def tearDown(self):self.tmp.cleanup()
 def event(self,n=1):return app.parse_event('radarr',{'eventType':'Download','movie':{'id':n,'title':str(n)}})
 def image(self):
  out=io.BytesIO();Image.new('RGB',(300,400),(255,0,0)).save(out,'PNG');return out.getvalue()
 def response(self,d):
  r=Mock();r.json.return_value=d;r.status_code=200;return r
 def info(self):return self.response({'width':170,'height':320,'format':'rgb565le','storage':True,'sha256':''})
 def test_only_imports(self):
  for event in ('Grab','Test','MovieAdded','Rename','Health'):self.assertIsNone(app.parse_event('radarr',{'eventType':event}))
  self.assertEqual(app.parse_event('sonarr',{'eventType':'Download','series':{'id':8}})['id'],8)
 def test_invalid_ids(self):
  for val in (None,True,-1,'1'):
   with self.assertRaises(ValueError):app.parse_event('radarr',{'eventType':'Download','movie':{'id':val}})
 def test_rgb_size_and_order(self):self.assertEqual(app.rgb565(self.image(),170,320),b'\x00\xf8'*54400)
 def test_local_poster(self):
  root=app.DATA/"posters"; p=root/"radarr"/"1"/"poster.jpg";p.parent.mkdir(parents=True);p.write_bytes(self.image())
  with patch.dict("os.environ",{"POSTER_ROOT":str(root)}):
   self.assertEqual(app.poster(self.event()),self.image())
   with self.assertRaises(ValueError):app.poster(self.event(2))
 def test_bad_image(self):
  with self.assertRaises(Exception):app.rgb565(b'broken',170,320)
 def test_restart(self):
  app.enqueue(self.event(1));app.enqueue(self.event(2));app.initialize()
  with app.db() as c:row=c.execute('SELECT revision,payload FROM latest').fetchone()
  self.assertEqual(row[0],2);self.assertEqual(json.loads(row[1])['id'],2)
 def test_retry(self):
  app.enqueue(self.event())
  with patch.object(app.requests,'get',return_value=self.info()),patch.object(app,'poster',return_value=self.image()),patch.object(app.requests,'post',side_effect=requests.Timeout):
   with self.assertRaises(requests.Timeout):app.deliver_once()
  app.initialize()
  with patch.object(app.requests,'get',return_value=self.info()),patch.object(app,'poster') as poster,patch.object(app.requests,'post',return_value=self.response({})):
   self.assertTrue(app.deliver_once());poster.assert_not_called()
  with app.db() as c:self.assertEqual(c.execute('SELECT delivered FROM latest').fetchone()[0],1)
 def test_superseded(self):
  app.enqueue(self.event())
  def replace(event):app.enqueue(self.event(2));return self.image()
  with patch.object(app.requests,'get',return_value=self.info()),patch.object(app,'poster',side_effect=replace),patch.object(app.requests,'post') as post:
   self.assertFalse(app.deliver_once());post.assert_not_called()
 def test_same_digest(self):
  app.enqueue(self.event());info=self.info()
  with patch.object(app.requests,'get',return_value=info),patch.object(app,'poster',return_value=self.image()),patch.object(app.requests,'post',return_value=self.response({})) as post:
   app.deliver_once()
   with app.db() as c:digest=c.execute('SELECT digest FROM latest').fetchone()[0]
   info.json.return_value['sha256']=digest;app.deliver_once();self.assertEqual(post.call_count,1)
 def test_no_storage(self):
  app.enqueue(self.event());info=self.info();info.json.return_value['storage']=False
  with patch.object(app.requests,'get',return_value=info),patch.object(app.requests,'post') as post:
   with self.assertRaises(ValueError):app.deliver_once()
   post.assert_not_called()
 def test_http(self):
  srv=app.ThreadingHTTPServer(('127.0.0.1',0),app.Handler);t=threading.Thread(target=srv.serve_forever,daemon=True);t.start();base=f'http://127.0.0.1:{srv.server_port}'
  try:
   self.assertEqual(requests.post(base+'/webhook/radarr/wrong',json={}).status_code,401)
   url=base+'/webhook/radarr/'+app.HOOK
   self.assertEqual(requests.post(url,json={'eventType':'Test'}).status_code,200)
   self.assertEqual(requests.post(url,json={'eventType':'Download','movie':{'id':5}}).status_code,202)
   self.assertEqual(requests.post(url,json=[]).status_code,400)
  finally:srv.shutdown();srv.server_close();t.join()
if __name__=='__main__':unittest.main()
