"""Isolated synthetic ACE client/server version-mix proof, no customer data."""
import argparse, ctypes as C, json, os, pathlib, re, socket, subprocess, sys, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--client',required=True);p.add_argument('--server');p.add_argument('--child',action='store_true');p.add_argument('--root');p.add_argument('--port',type=int);p.add_argument('--expect-bottom',type=int,required=True);a=p.parse_args()
if not a.child:
 with tempfile.TemporaryDirectory(prefix='openads_bottom_compat_') as root:
  root=pathlib.Path(root); data=root/'data';data.mkdir()
  with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
  env=os.environ.copy();env['OPENADS_ERROR_LOG_PATH']=str(root/'errors');env['OPENADS_INI']=str(root/'empty.ini');(root/'empty.ini').write_text('')
  out=open(root/'server.log','w')
  proc=subprocess.Popen([str(pathlib.Path(a.server).resolve()),'--host','127.0.0.1','--port',str(port),'--http_port','0','--data',str(data),'--error_log_path',str(root/'errors')],cwd=root,env=env,stdout=out,stderr=subprocess.STDOUT)
  try:
   for _ in range(200):
    try:
     with socket.create_connection(('127.0.0.1',port),timeout=.1):break
    except OSError:
     if proc.poll() is not None:raise RuntimeError((root/'server.log').read_text())
     time.sleep(.05)
   else:raise RuntimeError('daemon did not listen')
   env.update(OPENADS_WIRE_TRACE='1',OPENADS_WIRE_TRACE_COMPACT='1',OPENADS_WIRE_TRACE_FILE=str(root/'wire.log'))
   subprocess.run([sys.executable,__file__,'--child','--client',str(pathlib.Path(a.client).resolve()),'--root',str(root),'--port',str(port),'--expect-bottom',str(a.expect_bottom)],check=True,env=env,cwd=root,timeout=30)
  finally:
   proc.terminate()
   try:proc.wait(timeout=10)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
   out.close()
 sys.exit()
lib=C.CDLL(a.client);H=C.c_uint32;P=C.POINTER(H);S=C.c_char_p;U=C.c_uint16
specs={'AdsConnect60':[S,U,S,S,H,P], 'AdsCreateTable':[H,S,S,U,U,U,U,U,S,P], 'AdsOpenTable':[H,S,S,U,U,U,U,H,P], 'AdsCloseTable':[H], 'AdsDisconnect':[H], 'AdsAppendRecord':[H], 'AdsSetString':[H,S,S,H], 'AdsWriteRecord':[H], 'AdsUnlockTable':[H], 'AdsGotoRecord':[H,H], 'AdsDeleteRecord':[H], 'AdsCreateIndex61':[H,S,S,S,S,S,H,H,P], 'AdsGetIndexHandleByOrder':[H,U,P], 'AdsSetIndexOrderByHandle':[H,H], 'AdsSetScope':[H,U,S,U,U], 'AdsShowDeleted':[U], 'AdsGotoBottom':[H], 'AdsGetKeyNum':[H,U,P], 'AdsGetKeyCount':[H,U,P], 'AdsGetRecordCount':[H,U,P], 'OAdsSetLogging':[H]}
for n,types in specs.items():f=getattr(lib,n);f.argtypes=types;f.restype=H
def ok(n,*args):
 rc=getattr(lib,n)(*args);assert rc==0,(n,rc)
root=pathlib.Path(a.root);c=H();t=H();ix=H();ok('AdsConnect60',str(root/'data').encode(),1,None,None,0,C.byref(c));ok('AdsCreateTable',c,b'BC.DBF',None,2,0,0,0,0,b'K,C,1;V,C,8',C.byref(t))
for key in (b'A',b'B',b'C'):
 ok('AdsAppendRecord',t);ok('AdsSetString',t,b'K',key,1);ok('AdsWriteRecord',t)
ok('AdsGotoRecord',t,2);ok('AdsDeleteRecord',t);ok('AdsWriteRecord',t)
ok('AdsCreateIndex61',t,b'BC.CDX',b'BYK',b'K',None,None,8,0,C.byref(ix));ok('AdsCloseTable',t);ok('AdsDisconnect',c)
ok('OAdsSetLogging',1);uri=f'tcp://127.0.0.1:{a.port}/{root}/data'.encode();ok('AdsConnect60',uri,2,None,None,0,C.byref(c));ok('AdsOpenTable',c,b'BC.DBF',None,2,0,0,0,2,C.byref(t));ok('AdsGetIndexHandleByOrder',t,1,C.byref(ix));ok('AdsSetIndexOrderByHandle',t,ix)
k=H();ok('AdsGetKeyCount',ix,0,C.byref(k));ok('AdsSetScope',ix,1,b'A',1,1);ok('AdsSetScope',ix,2,b'C',1,1);ok('AdsShowDeleted',0)
wire=root/'wire.log';start=len(wire.read_text().splitlines());before=(root/'data'/'BC.DBF').read_bytes();ok('AdsGotoBottom',ix);ok('AdsGetKeyNum',ix,0,C.byref(k));assert k.value==2,k.value
lines=wire.read_text().splitlines()[start:];frames=[]
for line in lines:
 m=re.search(r'wire  op\.(0x[0-9A-F]+).*req\.(\d+).*ackb\.(\d+)',line)
 if m:frames.append((m[1],int(m[2]),int(m[3])))
assert sum(x[0]=='0x64' for x in frames)==1,frames
assert sum(x[0]=='0xB0' for x in frames)==(0 if a.expect_bottom else 1),frames
assert not any(x[0] in ('0x5A','0x52','0x5E','0x78') for x in frames),frames
assert (root/'data'/'BC.DBF').read_bytes()==before
# A later explicit table-handle key count remains a distinct request.
start=len(wire.read_text().splitlines());ok('AdsGetKeyCount',t,0,C.byref(k));assert k.value==2
assert any('op.0xB0' in x for x in wire.read_text().splitlines()[start:])
os.environ['OPENADS_FRESH_COUNTS']='1';ok('AdsGetRecordCount',t,0,C.byref(k));assert k.value==3,k.value
ok('AdsCloseTable',t);ok('AdsDisconnect',c)
print(json.dumps({'client':str(pathlib.Path(a.client).name),'bottom_metadata':a.expect_bottom,'scoped_live_keys':2,'physical_rows':3,'explicit_later_count_live':True,'disk_unchanged':True,'frames':frames}))
