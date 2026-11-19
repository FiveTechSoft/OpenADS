"""Separate-process synthetic indexed-key Skip version matrix. No customer data."""
import argparse, ctypes as C, json, os, pathlib, socket, subprocess, sys, tempfile, time
p=argparse.ArgumentParser(); p.add_argument('--client',required=True); p.add_argument('--server'); p.add_argument('--expect-repair',type=int,required=True); p.add_argument('--child',action='store_true'); p.add_argument('--root'); p.add_argument('--port',type=int); a=p.parse_args()
if not a.child:
 with tempfile.TemporaryDirectory(prefix='openads_keyskip_compat_') as root:
  root=pathlib.Path(root); (root/'data').mkdir(); (root/'empty.ini').write_text('')
  with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
  env=os.environ.copy();env.update(OPENADS_INI=str(root/'empty.ini'),OPENADS_ERROR_LOG_PATH=str(root/'errors'))
  with (root/'server.log').open('w') as out:
   proc=subprocess.Popen([str(pathlib.Path(a.server).resolve()),'--host','127.0.0.1','--port',str(port),'--http_port','0','--data',str(root/'data'),'--error_log_path',str(root/'errors')],cwd=root,env=env,stdout=out,stderr=subprocess.STDOUT)
   try:
    for _ in range(200):
     try:
      with socket.create_connection(('127.0.0.1',port),timeout=.1):break
     except OSError:
      if proc.poll() is not None:raise RuntimeError((root/'server.log').read_text())
      time.sleep(.05)
    else:raise RuntimeError('daemon did not listen')
    subprocess.run([sys.executable,str(pathlib.Path(__file__).resolve()),'--child','--client',str(pathlib.Path(a.client).resolve()),'--root',str(root),'--port',str(port),'--expect-repair',str(a.expect_repair)],check=True,env=env,cwd=root,timeout=60)
   finally:
    proc.terminate()
    try:proc.wait(timeout=10)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
 sys.exit()
lib=C.CDLL(a.client);H=C.c_uint32;P=C.POINTER(H);S=C.c_char_p;U=C.c_uint16
specs={'AdsConnect60':[S,U,S,S,H,P],'AdsCreateTable':[H,S,S,U,U,U,U,U,S,P],'AdsOpenTable':[H,S,S,U,U,U,U,H,P],'AdsCloseTable':[H],'AdsDisconnect':[H],'AdsAppendRecord':[H],'AdsSetString':[H,S,S,H],'AdsWriteRecord':[H],'AdsLockRecord':[H,H],'AdsIsRecordLocked':[H,H,C.POINTER(U)],'AdsGotoRecord':[H,H],'AdsSkip':[H,C.c_int32],'AdsGetRecordNum':[H,U,P],'AdsCreateIndex61':[H,S,S,S,S,S,H,H,P],'AdsGetIndexHandleByOrder':[H,U,P],'AdsSetIndexOrderByHandle':[H,H]}
for n,types in specs.items():f=getattr(lib,n);f.argtypes=types;f.restype=H

def ok(n,*args):
 rc=getattr(lib,n)(*args);assert rc==0,(n,rc)

root=pathlib.Path(a.root);results=[]
for case,(row,key,step,expected) in enumerate([(1,b'AAZ',1,2),(3,b'CCB',-1,2),(1,b'BBZ',1,3),(4,b'BBA',-1,1),(1,b'AAZ',0,1)]):
 for explicit in (False,True):
  c=H();t=H();idx=H();name=f'K{case}{int(explicit)}'.encode();dbf=name+b'.DBF'
  ok('AdsConnect60',str(root/'data').encode(),1,None,None,0,C.byref(c));ok('AdsCreateTable',c,dbf,None,2,0,0,0,0,b'K,C,3;V,C,8',C.byref(t))
  for k in (b'AAA',b'BBB',b'CCC',b'DDD'):
   ok('AdsAppendRecord',t);ok('AdsSetString',t,b'K',k,3);ok('AdsWriteRecord',t)
  ok('AdsCreateIndex61',t,name+b'.CDX',b'BYK',b'K',None,None,2,0,C.byref(idx));ok('AdsCloseTable',t);ok('AdsDisconnect',c)
  uri=f'tcp://127.0.0.1:{a.port}/{root}/data'.encode();ok('AdsConnect60',uri,2,None,None,0,C.byref(c));ok('AdsOpenTable',c,dbf,None,2,0,0,0,2,C.byref(t));ok('AdsGetIndexHandleByOrder',t,1,C.byref(idx));ok('AdsSetIndexOrderByHandle',t,idx);ok('AdsGotoRecord',t,row);ok('AdsLockRecord',t,row);ok('AdsSetString',t,b'K',key,3)
  if explicit:ok('AdsWriteRecord',t)
  ok('AdsSkip',idx,step);actual=H();ok('AdsGetRecordNum',t,0,C.byref(actual));held=U();ok('AdsIsRecordLocked',t,row,C.byref(held));assert held.value==1
  raw=(root/'data'/dbf.decode()).read_bytes();hlen=int.from_bytes(raw[8:10],'little');rlen=int.from_bytes(raw[10:12],'little');assert raw[hlen+(row-1)*rlen+1:hlen+(row-1)*rlen+4]==key
  if a.expect_repair:assert actual.value==expected,(case,explicit,actual.value,expected)
  results.append({'row':row,'key':key.decode(),'step':step,'explicit_commit':explicit,'expected':expected,'actual':actual.value,'lock_retained':True,'disk_key_exact':True})
  ok('AdsCloseTable',t);ok('AdsDisconnect',c)
if not a.expect_repair:assert any(r['actual']!=r['expected'] for r in results),'old server unexpectedly repaired'
print(json.dumps({'client':pathlib.Path(a.client).name,'expect_repair':a.expect_repair,'remote_results':results}))
