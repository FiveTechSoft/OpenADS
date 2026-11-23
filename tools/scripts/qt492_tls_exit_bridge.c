/* Old MinGW executable startup compatibility: TLS callbacks run before
 * the CRT is ready. Record the modern cleanup callback there; register
 * its adapter only from the old CRT's constructor phase. */
#include <windows.h>
#include <stdlib.h>
int __mingw_module_is_dll = 0;
static void (WINAPI *tls_exit_callback)(void *, DWORD, void *);
static void run_tls_exit(void) {
    if (tls_exit_callback) tls_exit_callback(NULL, DLL_PROCESS_DETACH, NULL);
    tls_exit_callback = NULL;
}
void _register_thread_local_exe_atexit_callback(void (WINAPI *callback)(void *, DWORD, void *)) {
    tls_exit_callback = callback;
}
static void __attribute__((constructor)) register_tls_exit_after_crt(void) {
    if (tls_exit_callback && atexit(run_tls_exit) != 0) abort();
}
