"""Regenerate MinGW ACE imports from the just-built DLL, not stale exports.
Run in the matching MinGW environment: python3 script DLL OLD_LIB OUTPUT BITS.
Retain the old symbols for legacy shims, add every actual ACE/VFS export,
and require a PE link probe for the new health function before staging.
"""
import pathlib, re, shutil, subprocess, sys, tempfile

def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)

def parse_export_names(text):
    # Binutils before 2.44 prints [ordinal] name; newer versions print
    # [ordinal] +base[base] name-index name. Match only name-table lines.
    return re.findall(r'^\s*\[\s*\d+\]\s+(?:\+base\[\s*\d+\]\s+[0-9a-fA-F]+\s+)?(\S+)\s*$', text, re.M)


def main():
    dll, old, output, bits = sys.argv[1:]
    dll, old, output = map(lambda p: pathlib.Path(p).resolve(), (dll, old, output))
    text = run('objdump', '-p', str(dll), capture_output=True).stdout
    names = parse_export_names(text)
    plain = [n for n in names if re.match(r'^(Ads|OAds|oads_)', n)]
    std = [n for n in names if re.match(r'^_(Ads|OAds|oads_)', n)] if bits == '32' else []
    assert 'OAdsGetServerStats' in plain, 'Built DLL lacks health export'
    if bits == '32':
        assert '_OAdsGetServerStats@12' in std, 'Built DLL lacks stdcall health export'
    with tempfile.TemporaryDirectory() as temp:
        work = pathlib.Path(temp)
        libs = [old] if old.exists() else []
        for kind, exports in [('cdecl', plain), ('stdcall', std)]:
            if not exports:
                continue
            definition = work / (kind + '.def')
            definition.write_text('LIBRARY ace' + bits + '.dll\nEXPORTS\n' + '\n'.join(exports) + '\n', encoding='ascii')
            library = work / (kind + '.a')
            flags = ['--no-leading-underscore'] if kind == 'stdcall' else []
            run('dlltool', *flags, '--input-def', str(definition), '--dllname', 'ace' + bits + '.dll', '--output-lib', str(library))
            libs.append(library)
        output.parent.mkdir(parents=True, exist_ok=True)
        # MRI scripts accept forward-slash paths; CI paths have no spaces.
        def mri_path(path):
            if shutil.which('cygpath'):
                return run('cygpath', '-m', str(path), capture_output=True).stdout.strip()
            return path.as_posix()
        script = 'CREATE ' + mri_path(output) + '\n' + ''.join('ADDLIB ' + mri_path(p) + '\n' for p in libs) + 'SAVE\nEND\n'
        run('ar', '-M', input=script)
        symbols = run('nm', '-g', str(output), capture_output=True).stdout
        required = ['_OAdsGetServerStats', '_OAdsGetServerStats@12'] if bits == '32' else ['OAdsGetServerStats']
        for name in required:
            assert re.search(r'\bT ' + re.escape(name) + r'$', symbols, re.M), 'Missing import: ' + name
        # Linking both conventions checks more than symbol-string presence.
        source = '.text\n.globl probe_entry\nprobe_entry:\n' + ''.join(' call ' + n + '\n' for n in required) + ' ret\n'
        asm, obj, exe = work / 'probe.s', work / 'probe.o', work / 'probe.exe'
        asm.write_text(source, encoding='ascii')
        run('as', str(asm), '-o', str(obj))
        run('ld', str(obj), str(output), '-e', 'probe_entry', '-o', str(exe))
        imports = run('objdump', '-p', str(exe), capture_output=True).stdout
        assert 'ace' + bits + '.dll' in imports and 'OAdsGetServerStats' in imports
        print('Fresh import library and health link probe passed:', output)

if __name__ == "__main__":
    main()
