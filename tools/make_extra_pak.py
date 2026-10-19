#!/usr/bin/env python3
"""Build pc7999_mobile_ios.{utoc,ucas,pak}: the UI art that Ziru's Android data has and the iOS data lacks.

Ziru's Android base paks (assetpacks/pad_*/.../assets/*_mobile_and.utoc) carry ~360 UI packages that are missing from
the iOS 11.1.1 data: standard player card photos (ui/Data/Thumbnail/Player/<id>_), team emblems, kit thumbnails,
coach symbols. Without them those cards render black and badges grey. This tool copies exactly those packages (and
their .ubulk chunks) into one extra IoStore container that the iOS client mounts next to its own paks. Nothing that
already exists in the iOS data is copied, so no iOS asset is overridden.

    python3 make_extra_pak.py --assetpacks <Ziru>/assetpacks --ios-pak <data>/pak [--ios-pak <data>/livedata] --out DIR
"""
import argparse, glob, hashlib, os, struct, sys, zlib

KEY = bytes.fromhex("4552d45005dfe94964893f4925ec747d3d591401e060ed8b3d58be5721c81295")
MAGIC = b"-==--==--==--==-"
WANT = ("/ui/data/thumbnail/player/", "/ui/data/symbol/emblem/", "/ui/data/symbol/emblemlc/",
        "/ui/data/thumbnail/uniform/", "/ui/data/symbol/coach/", "/ui/data/symbol/other/")
NAME = "pc7999_mobile_ios"
BLOCK = 65536


def aes_ecb_dec(b):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    d = Cipher(algorithms.AES(KEY), modes.ECB()).decryptor()
    return d.update(b) + d.finalize()


def rstr(b, p):
    n, = struct.unpack_from("<i", b, p); p += 4
    if n < 0:
        return b[p:p - 2 * n].decode("utf-16-le").rstrip("\0"), p - 2 * n
    return b[p:p + n].decode("latin1").rstrip("\0"), p + n


def wstr(s):
    b = s.encode("latin1") + b"\0"
    return struct.pack("<i", len(b)) + b


class Toc:
    def __init__(s, path):
        d = open(path, "rb").read(); s.path = path; s.raw = d
        assert d[:16] == MAGIC and d[16] == 2, path
        hs, ec, bc, bs, nc, nl, cbs, dis, pc = struct.unpack_from("<IIIIIIIII", d, 20)
        s.hs, s.flags, s.cbs = hs, d[80], cbs
        p = hs
        s.ids = [d[p + 12 * i:p + 12 * i + 12] for i in range(ec)]; p += 12 * ec
        s.ol = [(int.from_bytes(d[p + 10 * i:p + 10 * i + 5], "big"), int.from_bytes(d[p + 10 * i + 5:p + 10 * i + 10], "big"))
                for i in range(ec)]; p += 10 * ec
        s.blocks = []
        for i in range(bc):
            e = d[p + bs * i:p + bs * i + 12]
            s.blocks.append((int.from_bytes(e[0:5], "little"), int.from_bytes(e[5:8], "little"), int.from_bytes(e[8:11], "little"), e[11]))
        p += bc * bs
        s.methods = [d[p + i * nl:p + (i + 1) * nl].rstrip(b"\0") for i in range(nc)]; p += nc * nl
        di = d[p:p + dis]; p += dis
        if s.flags & 2:
            di = aes_ecb_dec(di)
        s.metas = [d[p + 33 * i:p + 33 * i + 33] for i in range(ec)]
        q = 0; mp, q = rstr(di, q); s.mount = mp
        n, = struct.unpack_from("<I", di, q); q += 4
        dirs = [struct.unpack_from("<IIII", di, q + 16 * i) for i in range(n)]; q += 16 * n
        n, = struct.unpack_from("<I", di, q); q += 4
        fil = [struct.unpack_from("<III", di, q + 12 * i) for i in range(n)]; q += 12 * n
        n, = struct.unpack_from("<I", di, q); q += 4
        strs = []
        for i in range(n):
            x, q = rstr(di, q); strs.append(x)
        s.files = {}
        def walk(i, pre):
            nm, fc, ns, ff = dirs[i]
            name = strs[nm] if nm != 0xFFFFFFFF else ""
            path = pre + name + "/" if name else pre
            f = ff
            while f != 0xFFFFFFFF:
                fn, nx, ud = fil[f]; s.files[path + strs[fn]] = ud; f = nx
            c = fc
            while c != 0xFFFFFFFF:
                walk(c, path); c = dirs[c][2]
        walk(0, "")

    def read(s, idx):
        off, ln = s.ol[idx]
        out = bytearray()
        with open(s.path[:-5] + ".ucas", "rb") as u:
            for bi in range(off // s.cbs, (off + ln - 1) // s.cbs + 1):
                o, cs, us, m = s.blocks[bi]
                u.seek(o); raw = u.read((cs + 15) & ~15 if s.flags & 2 else cs)
                if s.flags & 2:
                    raw = aes_ecb_dec(raw)
                raw = raw[:cs]
                out += zlib.decompress(raw) if m else raw[:us]
        st = off - (off // s.cbs) * s.cbs
        return bytes(out[st:st + ln])


def rel(path):
    return path.split("Content/", 1)[-1].lower() if "Content/" in path else path.lower()


def parse_header(b):
    """FContainerHeader: id, package count, names, name hashes, package ids, store entries, culture map, redirects."""
    p = 0
    cid, cnt = struct.unpack_from("<QI", b, p); p += 12
    n, = struct.unpack_from("<i", b, p); p += 4; names = b[p:p + n]; p += n
    n, = struct.unpack_from("<i", b, p); p += 4; hashes = b[p:p + n]; p += n
    n, = struct.unpack_from("<i", b, p); p += 4; pids = list(struct.unpack_from("<%dQ" % n, b, p)); p += 8 * n
    n, = struct.unpack_from("<i", b, p); p += 4; store = b[p:p + n]; p += n
    rest = b[p:]
    return dict(cid=cid, cnt=cnt, names=names, hashes=hashes, pids=pids, store=store, rest=rest)


ENTRY = 16  # FPackageStoreEntry in this build: ExportCount i32, ExportBundleCount i32,
           # ImportedPackages (count u32, offset u32 relative to that field); the imported ids follow all entries


def store_entry(store, i):
    base = ENTRY * i
    fixed = store[base:base + 8]
    n, off = struct.unpack_from("<II", store, base + 8)
    imports = list(struct.unpack_from("<%dQ" % n, store, base + 8 + off)) if n else []
    return fixed, imports


def build_store(entries):
    n = len(entries)
    head = bytearray(); tail = bytearray(); tail_base = ENTRY * n
    for i, (fixed, imports) in enumerate(entries):
        e = bytearray(fixed)
        field = ENTRY * i + 8
        if imports:
            pos = tail_base + len(tail); tail += struct.pack("<%dQ" % len(imports), *imports)
            e += struct.pack("<II", len(imports), pos - field)
        else:
            e += struct.pack("<II", 0, 0)
        head += e
    return bytes(head + tail)


def dir_index(mount, paths_to_index):
    """FIoDirectoryIndexResource with the given relative paths -> toc entry index."""
    strings, sidx = [], {}
    def S(x):
        if x not in sidx:
            sidx[x] = len(strings); strings.append(x)
        return sidx[x]
    dirs = [[0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF]]  # root
    files = []
    children = {0: {}}
    for path, idx in sorted(paths_to_index.items()):
        parts = path.split("/")
        d = 0
        for part in parts[:-1]:
            ch = children[d]
            if part not in ch:
                nd = len(dirs); dirs.append([S(part), 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF]); children[nd] = {}
                # prepend as first child
                dirs[nd][2] = dirs[d][1]; dirs[d][1] = nd; ch[part] = nd
            d = ch[part]
        fi = len(files); files.append([S(parts[-1]), dirs[d][3], idx]); dirs[d][3] = fi
    out = bytearray(wstr(mount))
    out += struct.pack("<I", len(dirs)) + b"".join(struct.pack("<IIII", *x) for x in dirs)
    out += struct.pack("<I", len(files)) + b"".join(struct.pack("<III", *x) for x in files)
    out += struct.pack("<I", len(strings)) + b"".join(wstr(x) for x in strings)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--assetpacks", required=True)
    ap.add_argument("--ios-pak", action="append", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    ios, ios_pids = set(), set()
    for d in a.ios_pak:
        for f in glob.glob(os.path.join(d, "*.utoc")):
            try:
                it = Toc(f)
                ios.update(rel(k) for k in it.files)
                hi = [i for i, c in enumerate(it.ids) if c[11] == 10]
                if hi:
                    ios_pids.update(parse_header(it.read(hi[0]))["pids"])
            except Exception as e:
                print("skip", f, e)
    srcs = [Toc(f) for f in sorted(glob.glob(os.path.join(a.assetpacks, "*", "*", "*", "assets", "*_mobile_and.utoc")))]
    mount = "../../../"
    where = {}           # package id -> (toc, header, position)
    want = []
    for t in srcs:
        hdr_idx = [i for i, c in enumerate(t.ids) if c[11] == 10]
        if not hdr_idx:
            continue
        hdr = parse_header(t.read(hdr_idx[0]))
        t.hdr = hdr
        t.bypid, t.idx_path = {}, {}
        for i, c in enumerate(t.ids):
            t.bypid.setdefault(c[:8], []).append(i)
        for k, i in t.files.items():
            t.idx_path[i] = k
        for pos, pid in enumerate(hdr["pids"]):
            where.setdefault(pid, (t, pos))
        for k, i in t.files.items():
            r = rel(k)
            if r.endswith(".uasset") and any(w in "/" + r for w in WANT) and r not in ios:
                pid = struct.unpack("<Q", t.ids[i][:8])[0]
                if pid not in ios_pids:
                    want.append(pid)
    chunks, paths, entries, pids, seen_pid = [], {}, [], [], set()
    queue = list(dict.fromkeys(want))
    missing_imports = 0
    while queue:
        pid = queue.pop(0)
        if pid in seen_pid or pid in ios_pids:
            continue
        if pid not in where:
            missing_imports += 1
            continue
        seen_pid.add(pid)
        t, pos = where[pid]
        fixed, imports = store_entry(t.hdr["store"], pos)
        queue.extend(imports)
        pidb = struct.pack("<Q", pid)
        for ci in t.bypid.get(pidb, []):
            if t.ids[ci][11] not in (2, 3, 4, 5):
                continue
            ni = len(chunks); chunks.append((t.ids[ci], t.read(ci), t.metas[ci]))
            if ci in t.idx_path:
                paths[t.idx_path[ci]] = ni
        entries.append((fixed, imports)); pids.append(pid)
    print("selected %d packages (%d from the wanted UI set), %d imports not found anywhere" % (len(pids), len(set(want)), missing_imports))
    if not chunks:
        print("nothing to add"); return
    cid = struct.unpack("<Q", hashlib.sha1(NAME.encode()).digest()[:8])[0]
    hb = bytearray(struct.pack("<QI", cid, len(pids)))
    hb += struct.pack("<i", 0)                                   # names (empty, as in the source headers)
    hb += struct.pack("<i", 8) + struct.pack("<Q", 0xC1640000)  # name hashes: algorithm id only
    hb += struct.pack("<i", len(pids)) + struct.pack("<%dQ" % len(pids), *pids)
    st = build_store(entries); hb += struct.pack("<i", len(st)) + st
    hb += struct.pack("<i", 0) + struct.pack("<i", 0)            # culture package map, package redirects
    hdr_id = struct.pack("<Q", cid) + b"\0\0\0\x0a"
    chunks.append((hdr_id, bytes(hb), hashlib.sha1(bytes(hb)).digest() + b"\0" * 13))
    os.makedirs(a.out, exist_ok=True)
    ucas = open(os.path.join(a.out, NAME + ".ucas"), "wb")
    ids, ols, blocks = [], [], []
    voff = 0
    for cid_b, data, meta in chunks:
        ids.append(cid_b); ols.append((voff, len(data)))
        for p in range(0, len(data), BLOCK):
            raw = data[p:p + BLOCK]; comp = zlib.compress(raw, 9)
            pos = ucas.tell()
            if len(comp) < len(raw):
                ucas.write(comp); blocks.append((pos, len(comp), len(raw), 1))
            else:
                ucas.write(raw); blocks.append((pos, len(raw), len(raw), 0))
        voff += (len(data) + BLOCK - 1) // BLOCK * BLOCK
    ucas.close()
    di = dir_index(mount, paths)
    src = srcs[0].raw
    hdr = bytearray(src[:srcs[0].hs])
    struct.pack_into("<IIIIIIIII", hdr, 20, srcs[0].hs, len(ids), len(blocks), 12, 1, 32, BLOCK, len(di), 0)
    struct.pack_into("<Q", hdr, 56, cid)
    hdr[64:80] = b"\0" * 16          # no encryption key
    hdr[80] = 1 | 8                   # Compressed | Indexed
    toc = bytearray(hdr)
    toc += b"".join(ids)
    toc += b"".join(o.to_bytes(5, "big") + l.to_bytes(5, "big") for o, l in ols)
    toc += b"".join(o.to_bytes(5, "little") + c.to_bytes(3, "little") + u.to_bytes(3, "little") + bytes([m]) for o, c, u, m in blocks)
    toc += b"Zlib".ljust(32, b"\0")
    toc += di
    toc += b"".join(m if len(m) == 33 else (m + b"\0" * 33)[:33] for _, _, m in chunks)
    open(os.path.join(a.out, NAME + ".utoc"), "wb").write(toc)
    # IoStore containers are mounted through a companion .pak of the same name; Ziru's are empty paks.
    pak = sorted(glob.glob(os.path.join(a.assetpacks, "*", "*", "*", "assets", "pc4000_mobile_and.pak")) or
                 glob.glob(os.path.join(a.assetpacks, "*", "*", "*", "assets", "*_mobile_and.pak")))[0]
    open(os.path.join(a.out, NAME + ".pak"), "wb").write(open(pak, "rb").read())
    # self-check: read every chunk back
    t = Toc(os.path.join(a.out, NAME + ".utoc"))
    for i, (cid_b, data, meta) in enumerate(chunks):
        assert t.ids[i] == cid_b and t.read(i) == data
    print("%s: %d packages, %d chunks, %d files, ucas %.1f MB" % (NAME, len(pids), len(chunks), len(paths),
          os.path.getsize(os.path.join(a.out, NAME + ".ucas")) / 1e6))


if __name__ == "__main__":
    main()
