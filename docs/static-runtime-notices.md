# Static runtime redistribution review

The combined kit now redistributes `libstdc++.a`, `libgcc.a`, `libgcc_eh.a`
and `libwinpthread.a`. The legacy executable profile also copies selected
MinGW CRT compatibility objects. Treat this archive as a distribution of those
components, not just an OpenADS build output.

Packaging copies the exact installed compiler's GCC GPLv3 text and Runtime
Library Exception, MinGW runtime notices and winpthreads license into
`runtime-notices/`, and hashes them in the manifest. A missing notice stops
packaging. Keep that directory with the archive and retain applicable notices
when distributing applications built from it. Existing OpenADS LICENSE/NOTICE
files and bundled engine dependency obligations are unchanged.

GCC's documentation describes libstdc++ as GPLv3 with the GCC Runtime Library
Exception 3.1. The exception permits eligible Target Code combinations with
independent modules under their chosen terms. This is not a blanket statement
that redistribution of the runtime itself has no source obligations. Before
public distribution, preserve the exact runtime package/source versions and
check the applicable corresponding-source or source-offer route. Do not
claim that the combined archive is licensed only under OpenADS's license.
No compiler executable, profiling runtime or libquadmath is included here.

The MSYS2 winpthreads package identifies MIT and BSD-3-Clause-Clear terms.
MinGW advises retaining its runtime notices with binary packages. We copy
all those runtime notices rather than guessing which individual notices
apply. Legacy Winlibs distributions need their own exact-version copies.
This review is a packaging checklist, not legal advice or final production
redistribution clearance.

Sources inspected October 13, 2026:
- https://gcc.sourceware.org/onlinedocs/libstdc++/manual/license.html
- https://github.com/msys2/MINGW-packages/blob/master/mingw-w64-gcc/PKGBUILD
- https://github.com/msys2/MINGW-packages/blob/master/mingw-w64-crt/PKGBUILD
- https://github.com/msys2/MINGW-packages/blob/master/mingw-w64-winpthreads/PKGBUILD
- https://sourceforge.net/p/mingw-w64/mingw-w64/ci/master/tree/COPYING.MinGW-w64/COPYING.MinGW-w64.txt
