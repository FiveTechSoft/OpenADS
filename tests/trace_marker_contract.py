"""Synthetic data only. A marker is not a wire frame or database operation."""
import os,pathlib,re,subprocess,sys,tempfile
local,remote=sys.argv[1:3]
def env_for(root,compact,wire=True,transport='env'):
 env=os.environ.copy()
 for k in list(env):
  if k.startswith('OPENADS_') and any(x in k for x in ('TRACE','LOG','DIAG','INI','OPDUMP')):env.pop(k)
 settings={'wire_trace':str(int(wire)),'wire_trace_compact':str(int(compact)),'wire_trace_file':str(root/'wire.log')}
 env['OPENADS_INI']=str(root/'openads.ini')
 (root/'openads.ini').write_text('\n'.join(k+'='+v for k,v in settings.items()) if transport=='ini' else '')
 if transport=='env':
  for k,v in settings.items():env['OPENADS_'+k.upper()]=v
 return env
for transport in ('env','ini'):
 for compact in (False,True):
  for mode in ('default','master-off','wire-off','on','unwritable'):
   with tempfile.TemporaryDirectory(prefix='oads_mark_') as base:
    root=pathlib.Path(base);env=env_for(root,compact,mode!='wire-off',transport)
    if mode=='unwritable':(root/'wire.log').mkdir()
    p=subprocess.run([local,mode],env=env,capture_output=True,text=True,timeout=30)
    assert p.returncode==0,(transport,compact,mode,p.returncode,p.stdout,p.stderr)
    path=root/'wire.log'
    if mode=='on':
     lines=path.read_text().splitlines();marks=[x for x in lines if '  M  ' in x]
     assert len(marks)==108,(len(marks),lines)
     assert not any('wire  op.' in x for x in lines)
     ticks=[];threads=set();labels=[];positions=[];ids=[]
     for x in marks:
      assert x[25:35].strip()=='',x
      assert re.match(r'^.{23}  .{10}  .{10}  .{20}  Marker +M  .{48}  ',x),x
      m=re.search(r'tick_us\.(.{20})  auto_thread\.(.{20})  instance\.(.{16})  thread\.(.{16})  marker\.(.*)$',x);assert m,x
      positions.append(tuple(x.index(k) for k in ('instance.','thread.', 'marker.')))
      ids.append((int(m[3]),int(m[4])))
      threads.add(int(m[2]));ticks.append(int(m[1]));labels.append(m[5])
     assert len(threads)==5 and 0 not in threads,threads
     assert ticks==sorted(ticks),ticks
     assert labels[0]=='SAVE_BEGIN' and labels[-1]=='SAVE_END'
     assert 'L'*120 in labels
     assert len(set(positions))==1,positions
     assert (9999999999999999,9999999999999999) in ids,ids
     assert ids[0]==ids[-1]==(0,0)
     assert [(label,ids[i]) for i,label in enumerate(labels) if label.startswith('INSTANCE_')]==[
      ('INSTANCE_A_BEGIN',(1,7)),('INSTANCE_B_BEGIN',(2,7)),('INSTANCE_A_END',(1,7)),('INSTANCE_B_END',(2,7))]
     for i,label in enumerate(labels):
      if label.startswith('THREAD_'):assert ids[i]==(3,int(label.split('_')[1]))
     assert not any('CANARY' in x for x in lines)
    else:assert not path.is_file(),(mode,path)
    print(transport,'compact' if compact else 'full',mode,'PASS')
# Inserting markers around pending writes cannot flush them, change navigation,
# unlock records, or add network frames. Compare the deterministic full sequence.
for compact in (False,True):
 sequences=[]
 for mode in ('caller','markers'):
  with tempfile.TemporaryDirectory(prefix='oads_mark_remote_') as base:
   root=pathlib.Path(base);env=env_for(root,compact)
   p=subprocess.run([remote,mode],env=env,capture_output=True,text=True,timeout=40)
   assert p.returncode==0,(mode,p.returncode,p.stdout,p.stderr)
   lines=(root/'wire.log').read_text().splitlines()
   frames=[x for x in lines if 'wire  op.' in x]
   sequences.append([re.search(r'wire  op\..*?  dur_us\.',x)[0] for x in frames])
   markers=[x for x in lines if '  M  ' in x]
   assert len(markers)==(4 if mode=='markers' else 0),(mode,markers)
   if compact:assert len(lines)==len(frames)+len(markers)
 assert sequences[0]==sequences[1],('wire sequence changed',sequences)
 print('remote',compact,'identical wire frames',len(sequences[0]),'PASS')

# Two actual application processes append to the same diagnostic destination.
# IDs are application-supplied; steady tick origins cannot be compared across them.
for compact in (False,True):
 with tempfile.TemporaryDirectory(prefix='oads_instances_') as base:
  root=pathlib.Path(base);env=env_for(root,compact)
  for instance in ('1','2'):
   p=subprocess.run([local,'scope-only',instance],env=env,capture_output=True,text=True,timeout=30)
   assert p.returncode==0,(instance,p.returncode,p.stdout,p.stderr)
  rows=(root/'wire.log').read_text().splitlines()
  assert len(rows)==4,rows
  scopes=[]
  for x in rows:
   m=re.search(r'instance\.(.{16})  thread\.(.{16})  marker\.(.*)$',x);assert m,x
   scopes.append((int(m[1]),int(m[2]),m[3]))
  assert scopes==[(1,7,'SAVE_BEGIN'),(1,7,'SAVE_END'),(2,7,'SAVE_BEGIN'),(2,7,'SAVE_END')],scopes
  print('two processes same file',compact,'PASS')
