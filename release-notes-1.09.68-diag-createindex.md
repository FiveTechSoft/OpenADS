Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.68-diag-createindex - temporary client-side index-create probe

This diagnostic build starts from v1.09.68-mtfix12 and adds an opt-in trace at the ACE client ABI boundary. It is for one fresh VouchADS org creation, not normal use. No server replacement is needed.

Set `OPENADS_CREATE_INDEX_DIAG_FILE=C:\tmp\createindex_diag.log` before starting VouchADS.exe. Create C:\tmp first if needed. The DLL writes ENTRY records for AdsCreateIndex, AdsCreateIndex61 and AdsCreateIndex90, including the handle, file, tag, expression, optional condition and options; it records null-argument and unknown-handle 5000 exits, native or remote routing, and CreateIndex wire send and ACK/error. Only index-create calls are recorded. The log may contain application table paths and index expressions; share it privately, not in a public issue. Unset the environment variable after the run and restore the original DLL.

Use the Windows x86 archive's ace32.dll for a 32-bit VouchADS.exe, or Windows x64 archive's ace64.dll only for a 64-bit executable. Put the diagnostic DLL alongside VouchADS.exe in place of its existing ACE DLL, after saving a backup. The version string in this build is 1.09.68-diag-createindex. Do not switch the server binary for this client-side test.
