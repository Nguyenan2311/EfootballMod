#!/usr/bin/env python3
"""Differential test: Ziru OfflineServer bytecode (JVM oracle) vs C++ port.

Both sides receive identical encrypted requests (TSV script) and their raw responses
are decrypted/decoded here and compared value-by-value (including int/float typing
and map key order)."""
import base64, gzip, json, os, shutil, subprocess, sys, zlib
import msgpack
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding

KEY = bytes.fromhex("43740981523cdc171e71de2ccab1a5a9b86f4b833196c55facd4bd25846c33f5")
HOME = os.path.expanduser("~")
ASSETS = os.environ.get("ZASSETS", "/tmp/x_assets")
ORACLE = f"{HOME}/oracle/run_oracle.sh"
CPP = f"{HOME}/z27/build/runcpp"


def enc_request(obj):
    raw = zlib.compress(msgpack.packb(obj, use_bin_type=True))
    iv = os.urandom(16)
    p = padding.PKCS7(128).padder()
    data = p.update(raw) + p.finalize()
    e = Cipher(algorithms.AES(KEY), modes.CBC(iv)).encryptor()
    return iv + e.update(data) + e.finalize()


def dec_response(body):
    iv, ct = body[:16], body[16:]
    d = Cipher(algorithms.AES(KEY), modes.CBC(iv)).decryptor()
    pt = d.update(ct) + d.finalize()
    u = padding.PKCS7(128).unpadder()
    pt = u.update(pt) + u.finalize()
    mp = gzip.decompress(pt)
    return msgpack.unpackb(mp, raw=False, strict_map_key=False, object_pairs_hook=lambda kv: ("__map__", kv))


def script_line(method, uri, body=None):
    if body is None:
        b = "-"
    else:
        b = base64.b64encode(enc_request(body)).decode()
    return f"{method}\t{uri}\t{b}"


def run(prog_args, script_path, out_path):
    subprocess.run(prog_args + [script_path, out_path], check=True)
    rows = []
    for line in open(out_path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line:
            continue
        st, mime, b = line.split("\t")
        rows.append((st, mime, b))
    return rows


def typed(v):
    """Canonical comparable form keeping int/float distinction and map order."""
    if isinstance(v, tuple) and len(v) == 2 and v[0] == "__map__":
        return {"$map": [[typed(k), typed(x)] for k, x in v[1]]}
    if isinstance(v, list):
        return [typed(x) for x in v]
    if isinstance(v, bool):
        return v
    if isinstance(v, float):
        return {"$f": repr(v)}
    return v


def first_diff(a, b, path="$"):
    if type(a) != type(b):
        return f"{path}: type {type(a).__name__} vs {type(b).__name__}: {str(a)[:200]} | {str(b)[:200]}"
    if isinstance(a, dict):
        if "$map" in a:
            ka = [json.dumps(k) for k, _ in a["$map"]]
            kb = [json.dumps(k) for k, _ in b["$map"]]
            if ka != kb:
                sa, sb = set(ka), set(kb)
                if sa != sb:
                    return f"{path}: keys differ only_java={sorted(sa - sb)[:10]} only_cpp={sorted(sb - sa)[:10]}"
                return f"{path}: key order differs {ka[:12]} vs {kb[:12]}"
            for (k, x), (_, y) in zip(a["$map"], b["$map"]):
                d = first_diff(x, y, f"{path}.{k}")
                if d:
                    return d
            return None
        if a != b:
            return f"{path}: {a} vs {b}"
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return f"{path}: len {len(a)} vs {len(b)}"
        for i, (x, y) in enumerate(zip(a, b)):
            d = first_diff(x, y, f"{path}[{i}]")
            if d:
                return d
        return None
    if a != b:
        return f"{path}: {str(a)[:200]} vs {str(b)[:200]}"
    return None


def compare(name, steps, seed=1000):
    work = f"/tmp/zdiff/{name}"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(f"{work}/j", exist_ok=True)
    os.makedirs(f"{work}/c", exist_ok=True)
    lines = []
    labels = []
    for s in steps:
        if s[0] == "CLOCK":
            lines.append(f"CLOCK\t{s[1]}")
            continue
        method, uri, body = s
        lines.append(script_line(method, uri, body))
        labels.append(uri)
    sp = f"{work}/script.tsv"
    open(sp, "w").write("\n".join(lines) + "\n")
    jr = run([ORACLE, ASSETS, f"{work}/j"], sp, f"{work}/j.out") if True else None
    # run_oracle.sh takes seed as 5th arg after script/out
    cr = run([CPP, ASSETS, f"{work}/c"], sp, f"{work}/c.out")
    ok = True
    for i, (lab, a, b) in enumerate(zip(labels, jr, cr)):
        if a[0] != b[0]:
            print(f"  [{name}#{i}] {lab}: status {a[0]} vs {b[0]} ({a[2][:80]} | {b[2][:80]})")
            ok = False
            continue
        if a[2] == "-" or b[2] == "-" or not a[1].startswith("application/x-msgpack"):
            if a[2] != b[2]:
                ra = base64.b64decode(a[2]) if a[2] != "-" else b""
                rb = base64.b64decode(b[2]) if b[2] != "-" else b""
                if ra != rb:
                    print(f"  [{name}#{i}] {lab}: raw body differs {ra[:80]} | {rb[:80]}")
                    ok = False
            continue
        va = typed(dec_response(base64.b64decode(a[2])))
        vb = typed(dec_response(base64.b64decode(b[2])))
        d = first_diff(va, vb)
        if d:
            print(f"  [{name}#{i}] {lab}: {d}")
            ok = False
    if len(jr) != len(cr):
        print(f"  [{name}] response count {len(jr)} vs {len(cr)}")
        ok = False
    print(f"{'PASS' if ok else 'FAIL'} {name} ({len(labels)} requests)")
    return ok


if __name__ == "__main__":
    import scenarios
    names = sys.argv[1:] or list(scenarios.ALL.keys())
    allok = True
    for n in names:
        allok &= compare(n, scenarios.ALL[n]())
    sys.exit(0 if allok else 1)
