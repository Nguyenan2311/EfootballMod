#!/usr/bin/env python3
"""Rewrite the in-match player name (Player.bin) from "L. SUAREZ" to "L. Suárez" in a dt870 CPK.

Player.bin holds 400-byte records with three 61-byte Latin name fields: display name at 210 (upper case, shown in
match), full name at 271 ("Luis Suárez") and shirt name at 332 (kept upper case for the kit font). Each display word
takes its spelling from the matching word of the full name, otherwise it is title-cased.

  python tools/titlecase_names.py dt870_mobile_and.cpk out/dt870_mobile_and.cpk
"""
import argparse, hashlib, json, re, struct, unicodedata, zlib
from pathlib import Path

from cpk_toc import utf_table
from repair_ios_tables import unpack_wesys

RECORD, DISPLAY, FULL, FIELD = 400, 210, 271, 61


def plain(s):
    return "".join(c for c in unicodedata.normalize("NFD", s) if not unicodedata.combining(c)).upper()


def titlecase(display, full):
    spelled = {plain(w): w for w in full.split()}
    out = []
    for token in display.split(" "):
        core = token.rstrip(".")
        if len(core) <= 1 or not core.isupper():
            out.append(token)  # initials ("L."), empty, already mixed case
        elif plain(core) in spelled:
            out.append(spelled[plain(core)] + token[len(core):])
        else:
            out.append(re.sub(r"[^\W\d_]+", lambda m: m.group().capitalize(), token))  # O'NEILL -> O'Neill
    return " ".join(out)


def convert_player_bin(raw):
    data = bytearray(raw)
    changed = 0
    for base in range(0, len(data) - RECORD + 1, RECORD):
        field = lambda off: data[base + off:base + off + FIELD].split(b"\0")[0].decode("utf-8")
        display, full = field(DISPLAY), field(FULL)
        new = titlecase(display, full).encode("utf-8")
        if len(new) >= FIELD:
            new = titlecase(display, "").encode("utf-8")  # accents from the full name overflow: plain title case
        if new != display.encode("utf-8"):
            data[base + DISPLAY:base + DISPLAY + FIELD] = new.ljust(FIELD, b"\0")
            changed += 1
    return bytes(data), changed


def convert_cpk(data):
    import zopfli.zlib  # pip install zopfli: plain zlib -9 cannot fit Konami's allocation
    result = bytearray(data)
    header = utf_table(data[16:0x800])[0]
    toc = header["TocOffset"]
    length = struct.unpack_from("<Q", data, toc + 8)[0]
    row = next(r for r in utf_table(data[toc + 16:toc + 16 + length]) if r.get("FileName") == "Player.bin")
    offset = min(toc, header["ContentOffset"]) + row["FileOffset"]
    size = row["FileSize"]
    raw, changed = convert_player_bin(unpack_wesys(data[offset:offset + size]))
    payload = zopfli.zlib.compress(raw, numiterations=5)
    # Same packing as repair_ios_tables: compressed, unencrypted WESYS, in the table's own slot
    packed = b"\x00\x01\x01WESYS" + struct.pack("<II", len(payload), len(raw)) + payload
    if len(packed) > size:
        raise SystemExit(f"Player.bin replacement ({len(packed)} B) exceeds its allocation ({size} B)")
    result[offset:offset + size] = packed.ljust(size, b"\0")
    return bytes(result), changed


if __name__ == "__main__":
    assert titlecase("L. SUAREZ", "Luis Suárez") == "L. Suárez"
    assert titlecase("RONALDO", "Cristiano Ronaldo") == "Ronaldo"
    assert titlecase("DE JONG", "Frenkie de Jong") == "de Jong"
    assert titlecase("Y G KIM", "Kim Young-Gwon") == "Y G Kim"
    assert titlecase("O'NEILL", "") == "O'Neill"
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    if args.output.resolve() == args.input.resolve():
        raise SystemExit("use a separate output path")
    fixed, changed = convert_cpk(args.input.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(fixed)
    print(json.dumps({"names_changed": changed, "size": len(fixed), "sha256": hashlib.sha256(fixed).hexdigest()}, indent=2))
