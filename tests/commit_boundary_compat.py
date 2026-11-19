"""Isolated synthetic ACE client/server version-mix proof, no customer data."""
import argparse, ctypes as C, json, os, pathlib, re, socket, subprocess, sys, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--client',required=True);p.add_argument('--server');p.add_argument('--child',action='store_true');p.add_argument('--root');p.add_argument('--port',type=int);p.add_argument('--expect-fusion',type=int,required=True);p.add_argument('--expect-append-batch',type=int,choices=(0,1),default=0);a=p.parse_args()
if not a.child:
 with tempfile.TemporaryDirectory(prefix='openads_commit_compat_') as root:
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
   subprocess.run([sys.executable,__file__,'--child','--client',str(pathlib.Path(a.client).resolve()),'--root',str(root),'--port',str(port),'--expect-fusion',str(a.expect_fusion),'--expect-append-batch',str(a.expect_append_batch)],check=True,env=env,cwd=root,timeout=30)
  finally:
   proc.terminate()
   try:proc.wait(timeout=10)
   except subprocess.TimeoutExpired:proc.kill();proc.wait()
   out.close()
 sys.exit()
lib=C.CDLL(a.client);H=C.c_uint32;P=C.POINTER(H);S=C.c_char_p;U=C.c_uint16
specs={'AdsConnect60':[S,U,S,S,H,P], 'AdsCreateTable':[H,S,S,U,U,U,U,U,S,P], 'AdsOpenTable':[H,S,S,U,U,U,U,H,P], 'AdsCloseTable':[H], 'AdsDisconnect':[H], 'AdsAppendRecord':[H], 'AdsSetString':[H,S,S,H], 'AdsWriteRecord':[H], 'AdsUnlockTable':[H], 'AdsGotoRecord':[H,H], 'AdsGetField':[H,S,S,P,U], 'OAdsSetLogging':[H]}
for n,types in specs.items():f=getattr(lib,n);f.argtypes=types;f.restype=H
def ok(n,*args):
 rc=getattr(lib,n)(*args);assert rc==0,(n,rc)
def put(t,n,v):ok('AdsSetString',t,n,v,len(v))
root=pathlib.Path(a.root);c=H();t=H();ok('AdsConnect60',str(root/'data').encode(),1,None,None,0,C.byref(c));ok('AdsCreateTable',c,b'CF.DBF',None,2,0,0,0,0,b'ID,N,8,0;NM,C,10,0;AGE,N,3,0',C.byref(t));ok('AdsCloseTable',t);ok('AdsDisconnect',c)
ok('OAdsSetLogging',1)
uri=f'tcp://127.0.0.1:{a.port}/{root}/data'.encode();ok('AdsConnect60',uri,2,None,None,0,C.byref(c));ok('AdsOpenTable',c,b'CF.DBF',None,2,0,0,0,2,C.byref(t));ok('AdsAppendRecord',t);put(t,b'ID',b'71');ok('AdsWriteRecord',t)
# The first key commit is durable before any later fields are supplied.
raw=(root/'data'/'CF.DBF').read_bytes();hlen=int.from_bytes(raw[8:10],'little');assert raw[hlen+1:hlen+9]==b'      71',(raw[hlen:],hlen);assert raw[hlen+9:hlen+19]==b'          ',raw[hlen:]
put(t,b'NM',b'whole');put(t,b'AGE',b'42');ok('AdsWriteRecord',t);ok('AdsUnlockTable',t);ok('AdsCloseTable',t);ok('AdsDisconnect',c)
raw=(root/'data'/'CF.DBF').read_bytes();assert raw[hlen+1:hlen+9]==b'      71';assert raw[hlen+9:hlen+19]==b'whole     ';assert raw[hlen+19:hlen+22]==b' 42'
frames=[]
for line in (root/'wire.log').read_text().splitlines():
 m=re.search(r'wire  op\.(0x[0-9A-F]+).*ackb\.(\d+)',line)
 if m:frames.append((m[1],int(m[2])))
# Frozen clients probe before each commit run. 75 qualifies the successful
# append and buffers the first field in BOTH distinct commit batches.
expected_fused=a.expect_fusion*(1+a.expect_append_batch)
assert sum(op=='0x5A' for op,n in frames)==2-expected_fused,frames
assert sum(op=='0x5E' and n==1 for op,n in frames)==expected_fused,frames
assert sum(op=='0x5E' for op,n in frames)==1+a.expect_append_batch,frames
assert sum(op=='0x52' for op,n in frames)==(0 if a.expect_append_batch else 2),frames
print(json.dumps({'client':str(pathlib.Path(a.client).name),'fused_commit':a.expect_fusion,'append_batch':a.expect_append_batch,'key_commit_then_whole_record':True,'disk_bytes_exact':True,'frames':frames}))
