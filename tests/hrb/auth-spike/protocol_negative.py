import socket,ssl,struct,sys
port=int(sys.argv[1])
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);ctx.check_hostname=False;ctx.verify_mode=ssl.CERT_NONE;ctx.minimum_version=ssl.TLSVersion.TLSv1_3;ctx.maximum_version=ssl.TLSVersion.TLSv1_3

def frame(op,data=b''):
 return struct.pack('>IB',len(data),op)+data

def recv_frame(s):
 def exact(n):
  b=b''
  while len(b)<n:
   t=s.recv(n-len(b))
   if not t:raise EOFError()
   b+=t
  return b
 h=exact(5);return h[4],exact(struct.unpack('>I',h[:4])[0])
# No DB operations or ordinary login can enter through required challenge gate.
for op in [0x20,0x10,0x05]:
 with ctx.wrap_socket(socket.create_connection(('127.0.0.1',port)),server_hostname='localhost') as s:
  s.sendall(frame(op));reply,data=recv_frame(s);assert reply==0xff,(op,reply)
  print('prelogin/ordinary mode denied',hex(op))
old=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);old.check_hostname=False;old.verify_mode=ssl.CERT_NONE;old.minimum_version=ssl.TLSVersion.TLSv1_2;old.maximum_version=ssl.TLSVersion.TLSv1_2
try:
 with old.wrap_socket(socket.create_connection(('127.0.0.1',port)),server_hostname='localhost'):pass
 raise AssertionError('TLS1.2 downgrade accepted')
except ssl.SSLError:print('TLS1.2 downgrade denied')
s=socket.create_connection(('127.0.0.1',port));s.settimeout(2);s.sendall(frame(0x3d,b'/tmp/hrb-auth-data'));assert not s.recv(4096);s.close();print('plaintext challenge denied')
