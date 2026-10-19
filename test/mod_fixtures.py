import pathlib
import sys
import zipfile

root = pathlib.Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
good = {f"My Stadium/pc3099_mobile_ios.{ext}": f"new-{ext}".encode() for ext in ("pak", "ucas", "utoc")}
fixtures = {
    "good": good,
    "traversal": {"../pc3099_mobile_ios.pak": b"bad"},
    "partial": {"pc3099_mobile_ios.pak": b"partial"},
    "empty": {"README.txt": b"no game resources"},
    "duplicate": {"a/pc3099_mobile_ios.pak": b"a", "b/pc3099_mobile_ios.pak": b"b"},
    "corrupt": good,
}
for name, files in fixtures.items():
    with zipfile.ZipFile(root / f"{name}.zip", "w") as z:
        for path, data in files.items():
            z.writestr(path, data)
p = root / "corrupt.zip"
p.write_bytes(p.read_bytes().replace(b"new-ucas", b"bad-ucas", 1))
