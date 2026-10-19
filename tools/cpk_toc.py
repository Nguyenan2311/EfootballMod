#!/usr/bin/env python3
"""List files inside CRI CPK archives (reads only the header/TOC). usage: cpk_toc.py a.cpk [b.cpk] -> json"""
import struct, sys, json

def decrypt_utf(buf):
    out = bytearray(buf); m, t = 0x655f, 0x4115
    for i in range(len(out)):
        out[i] ^= m & 0xff; m = (m * t) & 0xffffffff
    return bytes(out)

def utf_table(buf):
    if buf[:4] != b'@UTF':
        buf = decrypt_utf(buf)
        if buf[:4] != b'@UTF': raise ValueError('no @UTF')
    tsize = struct.unpack('>I', buf[4:8])[0]
    t = buf[8:8+tsize]
    rows_off, str_off, data_off, name_off = struct.unpack('>IIII', t[0:16])
    ncols, rowlen, nrows = struct.unpack('>HHI', t[16:24])
    def cstr(o):
        s = str_off + o; e = t.index(b'\0', s); return t[s:e].decode('utf8', 'replace')
    cols = []; p = 24
    for _ in range(ncols):
        flags = t[p]; p += 1
        nm = cstr(struct.unpack('>I', t[p:p+4])[0]); p += 4
        storage = flags & 0xf0; typ = flags & 0x0f; const = None
        if storage == 0x30:
            const, p = readval(t, p, typ, cstr, data_off)
        cols.append((nm, storage, typ, const))
    rows = []
    for r in range(nrows):
        q = rows_off + r * rowlen; row = {}
        for nm, storage, typ, const in cols:
            if storage == 0x50:
                row[nm], q = readval(t, q, typ, cstr, data_off)
            elif storage == 0x30:
                row[nm] = const
            else:
                row[nm] = None
        rows.append(row)
    return rows

def readval(t, p, typ, cstr, data_off):
    fm = {0:'>B',1:'>b',2:'>H',3:'>h',4:'>I',5:'>i',6:'>Q',7:'>q',8:'>f'}
    if typ in fm:
        sz = struct.calcsize(fm[typ]); return struct.unpack(fm[typ], t[p:p+sz])[0], p+sz
    if typ == 0xa:
        return cstr(struct.unpack('>I', t[p:p+4])[0]), p+4
    if typ == 0xb:
        o, l = struct.unpack('>II', t[p:p+8]); return ('data', o, l), p+8
    raise ValueError('type %x' % typ)

def toc(path):
    with open(path, 'rb') as f:
        h = f.read(0x800)
        if h[:4] != b'CPK ': raise ValueError('not CPK')
        hdr = utf_table(h[0x10:])[0]
        out = {'header': {k: v for k, v in hdr.items() if not isinstance(v, tuple)}, 'files': []}
        tocoff = hdr.get('TocOffset')
        if tocoff:
            f.seek(tocoff); b = f.read(16)
            sz = struct.unpack('<Q', b[8:16])[0]
            f.seek(tocoff + 16); tb = f.read(sz)
            for r in utf_table(tb):
                out['files'].append({'dir': r.get('DirName'), 'name': r.get('FileName'), 'size': r.get('FileSize'), 'extract': r.get('ExtractSize'), 'id': r.get('ID'), 'crc': r.get('CRC')})
        return out

if __name__ == '__main__':
    res = {p: toc(p) for p in sys.argv[1:]}
    json.dump(res, sys.stdout, indent=0, default=str)
