"""Generated-data process matrix; no customer source or data."""
import os, pathlib, subprocess, sys, tempfile
exe=sys.argv[1]
# Fail if a newly added public entry escapes the scoped diagnostic boundary.
import re
source=pathlib.Path(__file__).resolve().parent.parent/'src/abi/ace_exports.cpp'
if source.exists():
 text=source.read_text(encoding='utf-8')
 entries=re.findall(r'(?:UNSIGNED32|UNSIGNED16|UNSIGNED8|SIGNED32|SIGNED16|SIGNED8|void|VOID|double|DOUBLE|int|BOOL)\s+ENTRYPOINT\s+(\w+)\s*\([^;{}]*\)\s*\{',text)
 scopes=re.findall(r'wire_caller::Entry wire_entry\("([A-Za-z0-9_]+)"',text)
 assert entries==scopes,('public caller boundary coverage',len(entries),len(scopes))
 reasons=re.findall(r'wire_caller::Reason wire_reason\("([A-Za-z0-9_+\-]+)"',text)
 assert max(len(api+'/'+reason) for api in entries for reason in reasons)<=48
 print('caller public_entry_scope_coverage',len(entries),'PASS')

with tempfile.TemporaryDirectory(prefix='openads_trace_contract_') as base:
 for transport in ('env','ini','both'):
  for mode in ('default','off','on','lifecycle','late'):
   root=pathlib.Path(base)/(transport+'-'+mode);root.mkdir()
   env=os.environ.copy()
   for key in list(env):
    if key.startswith('OPENADS_') and any(x in key for x in ('TRACE','LOG','DIAG','INI','OPDUMP')):env.pop(key)
   env['OPENADS_INI']=str(root/'openads.ini')
   settings={'wire_trace':'1','wire_trace_file':str(root/'wire.log'),'log_file':str(root/'audit.log'),'log':'debug','resolve_verbose':'1','arc_trace':'1','trace':'1'}
   (root/'openads.ini').write_text('\n'.join(k+'='+v for k,v in settings.items()) if transport!='env' else '')
   if transport!='ini':
    for key,val in settings.items():env['OPENADS_'+key.upper()]=val
   # Lifecycle probe gets path independently of config lookup.
   env['OPENADS_WIRE_TRACE_FILE']=str(root/'wire.log')
   for k,name in [('CREATE_INDEX_DIAG_FILE','index.log'),('CREATE_TABLE_DIAG_FILE','table.log'),('LOCK_DIAG','lock.log'),('ERROR_LOG_PATH','errors')]:env['OPENADS_'+k]=str(root/name)
   p=subprocess.run([exe,mode],env=env,capture_output=True,text=True,timeout=30)
   if p.returncode:raise AssertionError((transport,mode,p.returncode,p.stdout,p.stderr))
   texts='\n'.join(f.read_bytes().decode('latin1') for f in root.rglob('*') if f.is_file() and f.name!='openads.ini')+p.stderr
   for canary in ('PRIVATE_TABLE_CANARY','PRIVATE_ERROR_CANARY','FIELD_X','TAGX','1234567','openads_trace_gate_probe'):
    assert canary not in texts,(transport,mode,canary)
   if mode in ('default','off'):
    assert not p.stderr,(transport,mode,p.stderr)
    assert not (root/'wire.log').exists(),(transport,mode)
    assert not list((root/'errors').glob('*')),(transport,mode)
    assert not any((root/f).exists() for f in ('index.log','table.log','audit.log','lock.log')),(transport,mode)
   else:
    wire=(root/'wire.log').read_text()
    assert 'wire  op.' in wire,(transport,mode)
    assert 'SAFE_ALIAS' in wire,(transport,mode)
   print(transport,mode,'PASS',p.stdout.strip())

# Compact means frame rows only, including Seek: no duplicate API [W].
# Compare deterministic wire operation/size sequences in separate processes;
# timing and connection fingerprints are deliberately not compared.
import re
for transport in ('env','ini','both'):
 sequences=[]
 for compact in (False,True):
  with tempfile.TemporaryDirectory(prefix='openads_compact_contract_') as base:
   root=pathlib.Path(base);env=os.environ.copy()
   for key in list(env):
    if key.startswith('OPENADS_') and any(x in key for x in ('TRACE','LOG','DIAG','INI','OPDUMP')):env.pop(key)
   settings={'wire_trace':'1','wire_trace_file':str(root/'wire.log'),'wire_trace_compact':str(int(compact))}
   env['OPENADS_INI']=str(root/'openads.ini')
   (root/'openads.ini').write_text('\n'.join(k+'='+v for k,v in settings.items()) if transport!='env' else '')
   if transport!='ini':
    for key,val in settings.items():env['OPENADS_'+key.upper()]=val
   env['OPENADS_ERROR_LOG_PATH']=str(root/'errors')
   p=subprocess.run([exe,'on'],env=env,capture_output=True,text=True,timeout=30)
   assert p.returncode==0,(transport,compact,p.returncode,p.stdout,p.stderr)
   wire=(root/'wire.log').read_text();lines=wire.splitlines()
   frames=[line for line in lines if 'wire  op.' in line]
   assert all('[' not in line[:85] and ']' not in line[:85] for line in lines),(transport,compact,'decorative column brackets')
   assert frames,(transport,compact)
   assert [line for line in lines if re.search(r'  W  .{48}  wire  op.',line)]==frames,(transport,compact,'W not exclusive to frames')
   for row_index,line in enumerate(lines):
    duration=line[25:35].strip();gap=line[37:47].strip()
    assert (gap=='' if row_index==0 else gap.isdigit()),('row gap',line)
    if line in frames:
     us=int(re.search(r'dur_us\.(\d+)',line).group(1))
     assert duration==str(us//1000),('wire duration',line)
     for key in ('op.','tid.','req.','ack.','ackb.','dur_us.','conn.'):
      assert not line[line.index(key)+len(key)].isspace(),('split pair',key,line)
    else:
     assert duration=='',('unmeasured summary duration',line)
    assert re.search(r'^.{23}  .{10}  .{10}  .{20}  .{20}  (W| )  .{48}  ',line),(transport,compact,'nonblank non-W flag',line)
   assert any('wire  op.0x90 ' in line for line in frames),(transport,compact,'missing Seek frame')
   if compact:
    assert lines==frames,(transport,'non-frame row in compact',wire)
    assert 'AdsSeek ' not in wire,(transport,'duplicate Seek summary')
   else:
    assert 'AdsSeek ' in wire,(transport,'full API summary missing')
    assert any('AdsSeek ' in line and re.search(r'  AdsSeek +   ',line) for line in lines),(transport,'Seek flag not blank')
   for canary in ('PRIVATE_TABLE_CANARY','PRIVATE_ERROR_CANARY','FIELD_X','TAGX','1234567','openads_trace_gate_probe'):
    assert canary not in wire+p.stderr,(transport,compact,canary)
   sequences.append([re.search(r'wire  op.(.*?) dur_us.',line).group(1) for line in frames])
 assert sequences[0]==sequences[1],(transport,'wire sequence changed',sequences)
 print(transport,'full_compact_same_frames',len(sequences[0]),'PASS')

# Explicit master ON must not itself turn on an unselected wire channel.
for value in ('0', 'false', 'off', 'no'):
 with tempfile.TemporaryDirectory(prefix='openads_trace_false_') as base:
  root=pathlib.Path(base);env=os.environ.copy()
  env['OPENADS_INI']=str(root/'openads.ini');(root/'openads.ini').write_text('wire_trace=1\n')
  env['OPENADS_WIRE_TRACE']=value;env['OPENADS_WIRE_TRACE_FILE']=str(root/'wire.log')
  env['OPENADS_ERROR_LOG_PATH']=str(root/'errors')
  p=subprocess.run([exe,'on'],env=env,capture_output=True,text=True,timeout=30)
  assert p.returncode==0,(value,p.returncode,p.stderr)
  assert not (root/'wire.log').exists(),value
  assert '[wire]' not in p.stderr,value
  print('env_false_overrides_ini',value,'PASS')

# Standalone daemon keeps necessary operational replies, not diagnostics.
if len(sys.argv)>2:
 import time
 with tempfile.TemporaryDirectory(prefix='PRIVATE_SERVER_CANARY_') as base:
  # The daemon's existing data jail rejects symlink path components.
  # macOS tempfile roots may be under /var -> /private/var. Give it the
  # physical root; do not weaken the jail to accommodate a test fixture.
  root=pathlib.Path(base).resolve();cfg=root/'server.ini'
  cfg.write_text('host=127.0.0.1\nport=0\ndata='+str(root)+'\ndiagnostics=false\n')
  env=os.environ.copy();env['OPENADS_WIRE_TRACE']='1';env['OPENADS_OPDUMP']='1'
  env['OPENADS_ERROR_LOG_PATH']=str(root/'errors')
  env['OPENADS_LOG_FILE']=str(root/'audit.log')
  server=subprocess.Popen([sys.argv[2],'--config',str(cfg)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   time.sleep(1)
   if server.poll() is not None:
    out,err=server.communicate(timeout=10)
    raise AssertionError(('server failed startup',server.returncode,out,err))
   server.terminate();out,err=server.communicate(timeout=10)
  finally:
   if server.poll() is None:server.kill();server.communicate()
  assert 'PRIVATE_SERVER_CANARY' not in out+err
  assert '[wire]' not in err
  assert not list((root/'errors').glob('*'))
  assert not (root/'audit.log').exists()
  print('standalone_server_OFF_env_OPDUMP_and_wire PASS')

# Provenance describes the synchronous ACE trigger, not PRG/RDD source lines.
with tempfile.TemporaryDirectory(prefix='openads_caller_contract_') as base:
 root=pathlib.Path(base);env=os.environ.copy()
 env.update(OPENADS_INI=str(root/'empty.ini'),OPENADS_WIRE_TRACE='1',OPENADS_WIRE_TRACE_COMPACT='1',OPENADS_WIRE_TRACE_FILE=str(root/'wire.log'),OPENADS_NO_BOTTOM_KEYCOUNT_CAP='1')
 (root/'empty.ini').write_text('')
 p=subprocess.run([exe,'caller'],env=env,capture_output=True,text=True,timeout=30)
 assert p.returncode==0,('caller',p.returncode,p.stdout,p.stderr)
 rows=(root/'wire.log').read_text().splitlines()
 assert rows and all('wire  op.' in x for x in rows)
 assert any('AdsWriteRecord/batch+commit' in x and 'op.0x5E' in x for x in rows),rows
 assert any('AdsUnlockTable/buffered-sets' in x and 'op.0x5E' in x for x in rows),rows
 assert any('AdsGetRecordCount/request' in x and 'op.0x46' in x for x in rows),rows
 assert any('AdsGotoRecord/request' in x and 'op.0x58' in x for x in rows),rows
 assert any('AdsGotoBottom/internal-keycount' in x and 'op.0xB0' in x for x in rows),rows
 unknown=[line for line in rows if '  unknown ' in line]
 assert len(unknown)==2,unknown
 assert all('call.0  thread.0  api.unknown' in line for line in unknown),unknown
 for line in rows:
  if line in unknown:continue
  assert re.search(r'call\.[1-9][0-9]*  thread\.[1-9][0-9]*  api\.[A-Za-z0-9_]+$',line),line
  assert 'unknown' not in line,line
 print('caller trigger_batch_commit_unlock_count_bottom_unknown PASS')
