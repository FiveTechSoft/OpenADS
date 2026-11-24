"""OADS-CR-001 E15 regression at the raw ACE API: Skip on an empty table.

SAP ADS 11.10 (ace32, local) keeps BOTH AdsAtBOF and AdsAtEOF true after
AdsSkip(+1), AdsSkip(-1) and AdsSkip(0) on an empty table. The OpenADS
engine now preserves that Limbo state at the ACE API. Harbour's rddads
deliberately keeps DBFCDX single-flag semantics at the RDD layer (its
not-positioned branch never calls AdsSkip on an empty table), so this
test drives the ACE entry points directly, local connection, synthetic
data, no server.

Usage: empty_skip_compat.py --client libopenace64.so --root DIR
"""
import argparse, ctypes as C, pathlib, sys, tempfile

p = argparse.ArgumentParser()
p.add_argument('--client', required=True)
p.add_argument('--root')
a = p.parse_args()

root = pathlib.Path(a.root) if a.root else pathlib.Path(
    tempfile.mkdtemp(prefix='openads_emptyskip_'))
(root / 'data').mkdir(parents=True, exist_ok=True)

lib = C.CDLL(a.client)
H = C.c_uint32; P16 = C.POINTER(C.c_uint16); P32 = C.POINTER(C.c_uint32); S = C.c_char_p; U16 = C.c_uint16
specs = {
    'AdsConnect60': [S, U16, S, S, H, P32],
    'AdsCreateTable': [H, S, S, C.c_uint32] * 1 + [C.c_uint32] * 4 + [S, P32],
    'AdsOpenTable': [H, S, S, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, H, P32],
    'AdsCloseTable': [H],
    'AdsDisconnect': [H],
    'AdsSkip': [H, C.c_int32],
    'AdsAtBOF': [H, P16],
    'AdsAtEOF': [H, P16],
}
fns = {}
for n, types in specs.items():
    f = getattr(lib, n); f.argtypes = types; f.restype = H; fns[n] = f

def ok(n, *args):
    rc = fns[n](*args)
    if rc != 0:
        raise RuntimeError('%s failed: %d' % (n, rc))

def flags(h):
    b = C.c_uint16(0); e = C.c_uint16(0)
    ok('AdsAtBOF', h, C.byref(b)); ok('AdsAtEOF', h, C.byref(e))
    return bool(b.value), bool(e.value)

conn = H(0)
ok('AdsConnect60', str(root / 'data').encode(), 1, None, None, 0, C.byref(conn))
tab = C.c_uint32(0)
ok('AdsCreateTable', conn, b'EMPTY.DBF', None, 2, 0, 0, 0, 0, b'K,C,1', C.byref(tab))
ok('AdsCloseTable', tab)
tab = C.c_uint32(0)
ok('AdsOpenTable', conn, b'EMPTY.DBF', None, 2, 0, 0, 0, 0, C.byref(tab))
h = tab.value

failures = []
def check(name, got, want):
    if got != want:
        failures.append('%s: got %s want %s' % (name, got, want))

check('open (empty)', flags(h), (True, True))
ok('AdsSkip', h, 1)
check('skip(+1) empty', flags(h), (True, True))
ok('AdsSkip', h, -1)
check('skip(-1) empty', flags(h), (True, True))
ok('AdsSkip', h, 0)
check('skip(0) empty', flags(h), (True, True))

ok('AdsCloseTable', h)
ok('AdsDisconnect', conn)

if failures:
    for f in failures:
        print('FAIL', f)
    sys.exit(1)
print('OK: E15 skip-on-empty keeps the BOF+EOF phantom at the ACE API')
