#!/usr/bin/env python3
"""Build one MinGW implementation/runtime/Harbour-glue archive.
The RDD and Harbour VM remain caller-owned. No compiler is downloaded here.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile


def run(*args, cwd=None, text=True, input=None):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=text, input=input)


def tool(root, name):
    for suffix in ('', '.exe'):
        p = pathlib.Path(root) / 'bin' / (name + suffix)
        if p.is_file():
            return p.resolve()
    raise SystemExit('Missing compiler tool: ' + name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--engine', required=True)
    p.add_argument('--runtime-root', required=True)
    p.add_argument('--link-root', required=True)
    p.add_argument('--harbour-include', required=True)
    p.add_argument('--glue', required=True)
    p.add_argument('--ace-include', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--profile', choices=('matching', 'qt492-x86'), default='matching')
    a = p.parse_args()
    out = pathlib.Path(a.output).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    engine = pathlib.Path(a.engine).resolve()
    engine_digest = hashlib.sha256(engine.read_bytes()).hexdigest()
    ar, objcopy, nm = [tool(a.runtime_root, n) for n in ('ar', 'objcopy', 'nm')]
    gcc = tool(a.runtime_root, 'gcc')
    link_gcc = tool(a.link_root, 'gcc')
    triple = run(gcc, '-dumpmachine').strip()
    link_triple = run(link_gcc, '-dumpmachine').strip()
    version = run(gcc, '-dumpversion').strip()
    link_version = run(link_gcc, '-dumpversion').strip()
    if triple != link_triple:
        raise SystemExit('Runtime/link architecture mismatch')
    old = a.profile == 'qt492-x86'
    if old:
        if triple != 'i686-w64-mingw32' or version != '10.2.0' or link_version != '4.9.2':
            raise SystemExit('qt492-x86 requires pinned i686 GCC10.2 runtime and GCC4.9.2 linker')
    elif pathlib.Path(a.runtime_root).resolve() != pathlib.Path(a.link_root).resolve():
        raise SystemExit('matching profile requires the same runtime and link compiler')
    # Copy notices from the exact installed runtime, not a newer web version.
    notice_root = pathlib.Path(a.runtime_root) / 'share' / 'licenses'
    notice_files = {
        'gcc/COPYING3': 'gcc-COPYING3.txt',
        'gcc/COPYING.RUNTIME': 'gcc-COPYING.RUNTIME.txt',
        'crt/COPYING.MinGW-w64-runtime.txt': 'MinGW-w64-runtime.txt',
        'winpthreads/COPYING': 'winpthreads-COPYING.txt',
    }
    # Legacy compiler distributions may keep these in a different directory.
    # Do not substitute unrelated versions or silently ship without notices.
    notices = out.parent / 'runtime-notices'
    notices.mkdir(exist_ok=True)
    for source, dest in notice_files.items():
        source_file = notice_root / source
        if not source_file.is_file():
            raise SystemExit('Missing exact runtime notice: ' + str(source_file))
        shutil.copyfile(source_file, notices / dest)
    archives = [engine]
    for name in ('libstdc++.a', 'libgcc.a', 'libgcc_eh.a', 'libwinpthread.a'):
        f = pathlib.Path(run(gcc, '-print-file-name=' + name).strip()).resolve()
        if not f.is_file():
            raise SystemExit('Missing static runtime: ' + name)
        archives.append(f)
    with tempfile.TemporaryDirectory(prefix='openads-kit-', dir=out.parent) as tmp:
        tmp = pathlib.Path(tmp).resolve()
        if any(' ' in str(f) for f in archives + [tmp]):
            raise SystemExit('GNU ar MRI needs space-free packaging paths')
        candidate = tmp / 'libopenace.a'
        mri = 'create ' + str(candidate).replace('\\', '/') + '\n'
        for i, f in enumerate(archives):
            staged = tmp / ('input%d.a' % i)
            shutil.copyfile(f, staged)
            mri += 'addlib ' + str(staged).replace('\\', '/') + '\n'
        run(ar, '-M', input=mri + 'save\nend\n')
        converted = []
        if old:
            names = run(ar, 't', candidate).splitlines()
            # Preserve MRI member ordering; replace only BigObj units.
            for name in dict.fromkeys(names):
                run(ar, 'x', candidate, name, cwd=tmp)
                f = tmp / name
                data = f.read_bytes()
                if data[:4] == b'\x00\x00\xff\xff' and data[4:6] == b'\x02\x00':
                    if names.count(name) != 1:
                        raise SystemExit('Ambiguous duplicate BigObj member: ' + name)
                    converted_file = tmp / (name + '.normal')
                    run(objcopy, '-O', 'pe-i386', f, converted_file)
                    converted_file.replace(f)
                    run(ar, 'r', candidate, f)
                    converted.append(name)
            for lib, name in (
                ('libmingw32.a', 'lib32_libmingw32_a-cxa_thread_atexit.o'),
                ('libmingw32.a', 'lib32_libmingw32_a-tls_atexit.o'),
                ('libmsvcrt.a', 'lib32_libmsvcrt_os_a-acrt_iob_func.o'),
                ('libmsvcrt.a', 'dembbs01050.o'),
                ('libmsvcrt.a', 'dembbh.o'),
                ('libmsvcrt.a', 'dembbt.o'),
            ):
                libfile = pathlib.Path(run(gcc, '-print-file-name=' + lib).strip()).resolve()
                run(ar, 'x', libfile, name, cwd=tmp)
                run(ar, 'r', candidate, tmp / name)
            bridge = pathlib.Path(__file__).with_name('qt492_tls_exit_bridge.c')
            run(link_gcc, '-c', bridge, '-o', tmp / 'crt_compat.o')
            run(ar, 'r', candidate, tmp / 'crt_compat.o')
        glue = pathlib.Path(a.glue).resolve()
        run(link_gcc, '-c', glue, '-I' + a.harbour_include,
            '-I' + a.ace_include,
            '-o', tmp / 'oads_hb.o')
        run(ar, 'r', candidate, tmp / 'oads_hb.o')
        # Fail closed on unresolved application wrapper names.
        required = {l.split()[-1] for l in run(nm, '-u', tmp / 'oads_hb.o').splitlines()
                    if l.split() and l.split()[-1].startswith(('_Ads', '_OAds', 'Ads', 'OAds'))}
        defined = {l.split()[-1] for l in run(nm, '--defined-only', candidate).splitlines()
                   if len(l.split()) >= 3}
        if not required or required - defined:
            raise SystemExit('Missing implementation exports: ' + repr(sorted(required - defined)))
        run(ar, 's', candidate)
        candidate.replace(out)
    manifest = {'profile': a.profile, 'runtime_target': triple, 'runtime_version': version,
                'link_version': link_version, 'engine_sha256': engine_digest,
                'archive_sha256': hashlib.sha256(out.read_bytes()).hexdigest(),
                'bytes': out.stat().st_size, 'converted_bigobj': converted,
                'glue_exports_verified': sorted(required),
                'runtime_notice_sha256': {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                                          for f in sorted(notices.iterdir()) if f.is_file()}}
    out.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
