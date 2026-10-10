# Sandbox fixture only. PHP_FFI_EXTENSION may be ffi or an absolute module path.
# OPENADS_BUILD=build PHP_BIN=php python3 tests/health-consumers/portal_fixture.py
import subprocess,os,tempfile,urllib.request,urllib.error,json,time,socket,pathlib,ctypes
root=tempfile.mkdtemp(prefix='stage7-portal-');php=os.environ.get('PHP_BIN','php');ffi=os.environ.get('PHP_FFI_EXTENSION','ffi');src=str(pathlib.Path(__file__).resolve().parents[2]);build=os.environ['OPENADS_BUILD']
def wait(port):
 for _ in range(80):
  try:
   with socket.create_connection(('127.0.0.1',port),timeout=.2):return
  except OSError:time.sleep(.1)
 raise RuntimeError('listener timeout')
cfg=root+'/daemon.ini';open(cfg,'w').write('host=127.0.0.1\nport=26391\nhttp_port=26392\ndata='+root+'\nauth_user=testadmin:test-only-password\nhttp_user=webadmin:web-test-password\nmax_sessions=11\n')
e=dict(os.environ,OPENADS_DLL=build+'/src/libopenace64.so');daemon=subprocess.Popen([build+'/tools/serverd/openads_serverd','--config',cfg],cwd=root,env=e,stdout=open(root+'/daemon.log','w'),stderr=subprocess.STDOUT)
open(root+'/seed.php','w').write('''<?php session_id('stage7test');session_start();$_SESSION['connections']['fixture']=['connType'=>'remote','host'=>'127.0.0.1','port'=>26391];session_write_close();''')
phpflags=[php,'-n','-d','extension='+ffi,'-d','ffi.enable=true','-d','session.save_path='+root]
subprocess.run(phpflags+[root+'/seed.php'],check=True)
server=subprocess.Popen(phpflags+['-S','127.0.0.1:26393','-t',src+'/DA-Web'],env=e,stdout=open(root+'/php.log','w'),stderr=subprocess.STDOUT)
def req(path,body=None,ct='application/json',cookie=True):
 headers={'Content-Type':ct}
 if cookie:headers['Cookie']='PHPSESSID=stage7test'
 request=urllib.request.Request('http://127.0.0.1:26393/'+path,headers=headers,data=json.dumps(body).encode() if body is not None else None)
 try:
  with urllib.request.urlopen(request)as r:return r.status,json.load(r)
 except urllib.error.HTTPError as ex:return ex.code,json.load(ex)
try:
 wait(26391);wait(26392);wait(26393);time.sleep(2)
 time.sleep(2)
 status,j=req('api/server_info.php?dd=fixture');assert status==401 and j['management_required'];csrf=j['management_csrf'];print('loginrequired PASS')
 time.sleep(2)
 for token in ['', 'wrong']:
  status,j=req('api/server_info.php',{'dd':'fixture','action':'management_login','csrf':token,'management_user':'testadmin','management_password':'test-only-password'});assert status==403
 print('CSRF PASS')
 status,j=req('api/server_info.php',{'dd':'fixture'},ct='text/plain');assert status==415
 # wrong-password check follows successful login to avoid lockout throttling
 status,j=req('api/server_info.php',{'dd':'fixture','action':'management_login','csrf':csrf,'management_user':'testadmin','management_password':'test-only-password'});print('LOGIN',status,j);assert status==200 and j['ok']
 status,j=req('api/server_info.php?dd=fixture');assert status==200 and j['health']['scope']=='server' and j['health']['max_sessions']==11
 assert 'test-only-password' not in json.dumps(j);print('remotehealth/privacy PASS')
 status,j=req('api/server_info.php',{'dd':'fixture','action':'kill','csrf':'wrong','connNo':1});assert status==403;print('legacyactionCSRF PASS')
 def session_change(code):
  open(root+'/change.php','w').write("<?php session_id('stage7test');session_start();"+code+";session_write_close();")
  subprocess.run(phpflags+[root+'/change.php'],check=True)
 session_change("$_SESSION['management_auth']['fixture']['expires']=0")
 status,j=req('api/server_info.php?dd=fixture');assert status==401;print('expiry PASS')
 time.sleep(2)
 status,j=req('api/server_info.php',{'dd':'fixture','action':'management_login','csrf':csrf,'management_user':'testadmin','management_password':'test-only-password'});assert status==200
 session_change("$_SESSION['management_auth']['fixture']['endpoint']='changed-host:123'")
 status,j=req('api/server_info.php?dd=fixture');assert status==401;print('endpoint invalidation PASS')
 session_change("$_SESSION['connections']['localfixture']=['connType'=>'local']")
 status,j=req('api/server_info.php?dd=localfixture');assert status==200 and j['health']['scope']=='local_process';print('local mode PASS')

 status,j=req('api/server_info.php?dd=fixture',cookie=False);assert status==401
 session_change("unset($_SESSION['management_auth']['fixture'])")
 time.sleep(2)
 status,j=req('api/server_info.php',{'dd':'fixture','action':'management_login','csrf':csrf,'management_user':'testadmin','management_password':'wrong'});assert status==401;print('wrong credentials PASS')
 # Direct CLI request with non-loopback browser address exercises HTTPS gate.
 open(root+'/https.php','w').write("<?php session_id('stage7test');$_SERVER['REQUEST_METHOD']='POST';$_SERVER['CONTENT_TYPE']='application/json';$_SERVER['REMOTE_ADDR']='192.0.2.1';$_SERVER['HTTPS']='off';include '"+src+"/DA-Web/api/server_info.php';")
 # php://input cannot be populated by CLI, so use a sandbox copy to supply JSON.
 code=open(src+'/DA-Web/api/server_info.php').read().replace("file_get_contents('php://input')", "file_get_contents('php://stdin')")
 open(root+'/gate.php','w').write(code)
 code=open(root+'/https.php').read().replace(src+'/DA-Web/api/server_info.php',root+'/gate.php');open(root+'/https.php','w').write(code)
 import shutil;shutil.copy(src+'/DA-Web/api/common.php',root+'/common.php')
 r=subprocess.run(phpflags+[root+'/https.php'],input=json.dumps({'dd':'fixture','action':'management_login','csrf':csrf,'management_user':'testadmin','management_password':'test-only-password'}).encode(),stdout=subprocess.PIPE,check=True,env=e)
 assert json.loads(r.stdout)['error']=='Use HTTPS to enter management credentials';print('HTTPS gate PASS')

 status,j=req('api/server_info.php',{'dd':'fixture','action':'management_logout','csrf':csrf});assert j['ok'];status,j=req('api/server_info.php?dd=fixture');assert status==401;print('logout/nosession PASS')
 # Existing Studio gate checks on new read-only route.
 try:urllib.request.urlopen('http://127.0.0.1:26392/api/server/health');raise AssertionError('noauthpassed')
 except urllib.error.HTTPError as ex:assert ex.code in [401,403]
 import base64
 r=urllib.request.Request('http://127.0.0.1:26392/api/server/health',headers={'Authorization':'Basic '+base64.b64encode(b'webadmin:web-test-password').decode()})
 with urllib.request.urlopen(r)as response:
  assert response.headers['Cache-Control']=='no-store';h=json.load(response);assert h['scope']=='server' and h['max_sessions']==11
 print('studioauth/serverhealth/no-store PASS')
finally:server.terminate();daemon.terminate();server.wait();daemon.wait();print('LOGS',root)
