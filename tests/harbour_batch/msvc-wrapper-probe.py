# Compile the exact new wrapper with MSVCx86, verify both calling conventions.
import pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
s=(root/'src/abi/ace_stdcall_x86.c').read_text()
a=s.index('/* ---- AdsFlushPendingFields ---- */');b=s.index('/* ---- AdsFlushFileBuffers ---- */',a)
ma=s.index('/* Local marker:');mb=s.index('/* ---- OAdsSetLogging ---- */',ma)
out=root/'output/msvc-abi';out.mkdir(parents=True,exist_ok=True)
(out/'wrapper.c').write_text('''#include <stdint.h>
typedef unsigned long UNSIGNED32;
typedef unsigned char UNSIGNED8;
typedef unsigned long long UNSIGNED64;
typedef uintptr_t ADSHANDLE;
#define ENTRYPOINT
static void arc_log_rc(const char*n,unsigned r,int c,...) {(void)n;(void)r;(void)c;}
'''+s[a:b]+s[ma:mb])
(out/'impl.c').write_text('unsigned long __cdecl AdsFlushPendingFields(unsigned long h){return h+73;}\nunsigned long __cdecl OAdsTraceMark(const unsigned char* s,unsigned long n){return s ? n+76 : 0;}\nunsigned long __cdecl OAdsTraceMarkEx(const unsigned char* s,unsigned long n,unsigned long long i,unsigned long long t){return s && i==9999999999999999ULL && t==9007199254740991ULL ? n+77 : 0;}\n')
(out/'caller.c').write_text('''#include <windows.h>
#include <stdio.h>
typedef unsigned long (__stdcall *S)(unsigned long);
typedef unsigned long (__cdecl *C)(unsigned long);
typedef unsigned long (__stdcall *MS)(const unsigned char*,unsigned long);
typedef unsigned long (__cdecl *MC)(const unsigned char*,unsigned long);
typedef unsigned long (__stdcall *MXS)(const unsigned char*,unsigned long,unsigned long long,unsigned long long);
typedef unsigned long (__cdecl *MXC)(const unsigned char*,unsigned long,unsigned long long,unsigned long long);
int main(void){HMODULE m=LoadLibraryA("probe.dll");S s;C c;MS ms;MC mc;MXS mxs;MXC mxc;int i;
if(!m)return 1;s=(S)GetProcAddress(m,"_AdsFlushPendingFields@4");c=(C)GetProcAddress(m,"AdsFlushPendingFields");
ms=(MS)GetProcAddress(m,"_OAdsTraceMark@8");mc=(MC)GetProcAddress(m,"OAdsTraceMark");
mxs=(MXS)GetProcAddress(m,"_OAdsTraceMarkEx@24");mxc=(MXC)GetProcAddress(m,"OAdsTraceMarkEx");
if(!s||!c||!ms||!mc||!mxs||!mxc)return 2;for(i=0;i<100000;i++){if(s(i)!=(unsigned long)i+73||c(i)!=(unsigned long)i+73)return 3;if(ms((const unsigned char*)"X",i)!=(unsigned long)i+76||mc((const unsigned char*)"X",i)!=(unsigned long)i+76)return 4;if(mxs((const unsigned char*)"X",i,9999999999999999ULL,9007199254740991ULL)!=(unsigned long)i+77||mxc((const unsigned char*)"X",i,9999999999999999ULL,9007199254740991ULL)!=(unsigned long)i+77)return 5;}
puts("MSVC-X86-STDCALL-CDECL-PASS");return 0;}
''')
subprocess.run(['cl','/nologo','/LD','/W4','wrapper.c','impl.c','/link','/out:probe.dll'],cwd=out,check=True)
subprocess.run(['cl','/nologo','/W4','caller.c','/Fe:caller.exe'],cwd=out,check=True)
with (out/'exports.txt').open('w') as f:subprocess.run(['dumpbin','/exports','probe.dll'],cwd=out,stdout=f,check=True)
t=(out/'exports.txt').read_text();assert '_AdsFlushPendingFields@4' in t and ' AdsFlushPendingFields' in t and '_OAdsTraceMark@8' in t and ' OAdsTraceMark' in t and '_OAdsTraceMarkEx@24' in t and ' OAdsTraceMarkEx' in t
subprocess.run([str(out/'caller.exe')],cwd=out,check=True)
