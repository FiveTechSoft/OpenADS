# Compile the exact new wrapper with MSVCx86, verify both calling conventions.
import pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
s=(root/'src/abi/ace_stdcall_x86.c').read_text()
a=s.index('/* ---- AdsFlushPendingFields ---- */');b=s.index('/* ---- AdsFlushFileBuffers ---- */',a)
out=root/'output/msvc-abi';out.mkdir(parents=True,exist_ok=True)
(out/'wrapper.c').write_text('''#include <stdint.h>
typedef unsigned long UNSIGNED32;
typedef uintptr_t ADSHANDLE;
#define ENTRYPOINT
static void arc_log_rc(const char*n,unsigned r,int c,...) {(void)n;(void)r;(void)c;}
'''+s[a:b])
(out/'impl.c').write_text('unsigned long __cdecl AdsFlushPendingFields(unsigned long h){return h+73;}\n')
(out/'caller.c').write_text('''#include <windows.h>
#include <stdio.h>
typedef unsigned long (__stdcall *S)(unsigned long);
typedef unsigned long (__cdecl *C)(unsigned long);
int main(void){HMODULE m=LoadLibraryA("probe.dll");S s;C c;int i;
if(!m)return 1;s=(S)GetProcAddress(m,"_AdsFlushPendingFields@4");c=(C)GetProcAddress(m,"AdsFlushPendingFields");
if(!s||!c)return 2;for(i=0;i<100000;i++){if(s(i)!=(unsigned long)i+73||c(i)!=(unsigned long)i+73)return 3;}
puts("MSVC-X86-STDCALL-CDECL-PASS");return 0;}
''')
subprocess.run(['cl','/nologo','/LD','/W4','wrapper.c','impl.c','/link','/out:probe.dll'],cwd=out,check=True)
subprocess.run(['cl','/nologo','/W4','caller.c','/Fe:caller.exe'],cwd=out,check=True)
with (out/'exports.txt').open('w') as f:subprocess.run(['dumpbin','/exports','probe.dll'],cwd=out,stdout=f,check=True)
t=(out/'exports.txt').read_text();assert '_AdsFlushPendingFields@4' in t and ' AdsFlushPendingFields' in t
subprocess.run([str(out/'caller.exe')],cwd=out,check=True)
