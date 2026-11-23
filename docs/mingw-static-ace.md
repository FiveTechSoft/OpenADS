# MinGW static client kit

`libopenace32.a` or `libopenace64.a` is the combined implementation archive.
It contains the OpenADS engine, its bundled dependencies, the matching static
C++/GCC/pthread runtime, and compiled Harbour wrappers. `libace32.a` and
`libace64.a` are DLL import libraries, not substitutes.

Use your matching Harbour VM and ADS RDD. Rebuild the ADS driver against the
supplied `ace.h` (cdecl for MinGW). Link the implementation archive after that
RDD and keep Windows system libraries `ws2_32` and `psapi`. Do not compile a
second `oads_hb.c`, add another runtime archive, or add an ACE DLL import library.
Keep only one implementation archive visible in linker search directories.

The release runs `tools/scripts/test_harbour_static_mingw.sh` before staging
the combined archive, compiler/hash manifest and runtime notices. The script
is repository-only: it needs the source tree, packager, pinned Harbour source,
and a matching MSYS2 compiler. A downloaded kit is not a source-build tree.
The smoke rejects ACE, C++, GCC and pthread runtime DLL imports.

## Profiles

`matching` requires the same compiler for runtime and wrapper objects. The
standard release retains its existing MSYS2 compiler route. It does not change
the owner's compiler. Its current static configuration disables TLS and HTTP;
no TLS-enabled static kit is claimed by this change.

`qt492-x86` checks pinned i686 GCC10.2 runtime and Qt MinGW4.9.2 wrapper/link
compiler. It changes only BigObj containers, not machine code. The executable
TLS-exit bridge records the callback before CRT startup and registers cleanup
in the normal CRT constructor phase, avoiding the old CRT exit-lock deadlock.
It must not be reused for DLLs or unrelated toolchains. The matching-toolchain release smoke does not exercise this legacy profile.
Qualify it separately on the exact compiler pair and application before use.

The legacy profile also needs the exact compiler-distribution runtime notices
under `share/licenses`. Missing notices stop packaging; do not substitute a
newer compiler's notices. See `static-runtime-notices.md` before distribution.

Static linking removes the ACE DLL dependency. It does not prevent executable
patching, injected code or a rebuilt application. A passing smoke is not
production approval, a Vouch pass, or permission to merge to main.
