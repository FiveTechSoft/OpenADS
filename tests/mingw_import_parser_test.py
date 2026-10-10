"""Binutils export output formats seen locally and on current MSYS2."""
from importlib.util import spec_from_file_location, module_from_spec
from pathlib import Path
spec = spec_from_file_location("imports", Path(__file__).resolve().parents[1] / "tools/scripts/regenerate_mingw_imports.py")
module = module_from_spec(spec)
spec.loader.exec_module(module)
expected = ["OAdsGetServerStats", "_OAdsGetServerStats@12"]
for text in ["\t[ 123] OAdsGetServerStats\n\t[124] _OAdsGetServerStats@12\n",
             "\t[ 123] +base[ 124] 007b OAdsGetServerStats\n\t[124] +base[125] 007c _OAdsGetServerStats@12\n"]:
    assert module.parse_export_names(text) == expected
assert module.parse_export_names("\t[ 123] +base[124] 000ffff Export RVA\n") == []
print("MinGW export parser contracts passed")
