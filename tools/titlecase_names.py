#!/usr/bin/env python3
"""Rewrite the in-match player name (Player.bin) from "L. SUAREZ" to "L. Suárez" in a dt870 CPK.

Player.bin holds 400-byte records with three 61-byte Latin name fields: display name at 210 (upper case, shown in
match), full name at 271 ("Luis Suárez") and shirt name at 332 (also shown in menus and on the kit). Each word
takes its spelling from the matching word of the full name, otherwise it is title-cased.

  python tools/titlecase_names.py dt870_mobile_and.cpk out/dt870_mobile_and.cpk [--shirt]
"""
import argparse, hashlib, json, re, struct, unicodedata, zlib
from pathlib import Path

from cpk_toc import utf_table
from repair_ios_tables import unpack_wesys

RECORD, DISPLAY, FULL, SHIRT, FIELD = 400, 210, 271, 332, 61


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


def convert_player_bin(raw, fields=(DISPLAY,)):
    data = bytearray(raw)
    changed = 0
    for base in range(0, len(data) - RECORD + 1, RECORD):
        field = lambda off: data[base + off:base + off + FIELD].split(b"\0")[0].decode("utf-8")
        full = field(FULL)
        for off in fields:
            name = field(off)
            new = titlecase(name, full).encode("utf-8")
            if len(new) >= FIELD:
                new = titlecase(name, "").encode("utf-8")  # accents from the full name overflow: plain title case
            if new != name.encode("utf-8"):
                data[base + off:base + off + FIELD] = new.ljust(FIELD, b"\0")
                changed += 1
    return bytes(data), changed


def player_names(raw):
    """{player base id: display name in normal case} for the Champions League screen."""
    names = {}
    for base in range(0, len(raw) - RECORD + 1, RECORD):
        field = lambda off: raw[base + off:base + off + FIELD].split(b"\0")[0].decode("utf-8")
        if field(FULL):
            names[struct.unpack_from("<I", raw, base + 8)[0] & 0xFFFFFF] = titlecase(field(DISPLAY), field(FULL))
    return names


def convert_team_bin(raw, teams):
    """Team.bin: 1600-byte records after a 12-byte header; id at 0, name at 384, 3-letter code at 874."""
    data = bytearray(raw)
    changed = 0
    for base in range(12, len(data) - 1599, 1600):
        real = teams.get(str(struct.unpack_from("<I", data, base)[0]))
        if not real:
            continue
        name, short = real[0].encode("utf-8"), real[1].encode("utf-8")
        if len(name) >= 48 or len(short) >= 8:
            raise SystemExit(f"team name too long: {real}")
        data[base + 384:base + 384 + 48] = name.ljust(48, b"\0")
        data[base + 874:base + 874 + 8] = short.ljust(8, b"\0")
        changed += 1
    return bytes(data), changed


def rewrite_table(data, name, convert):
    import zopfli.zlib  # pip install zopfli: plain zlib -9 cannot fit Konami's allocation
    result = bytearray(data)
    header = utf_table(data[16:0x800])[0]
    toc = header["TocOffset"]
    length = struct.unpack_from("<Q", data, toc + 8)[0]
    row = next(r for r in utf_table(data[toc + 16:toc + 16 + length]) if r.get("FileName") == name)
    offset = min(toc, header["ContentOffset"]) + row["FileOffset"]
    size = row["FileSize"]
    raw, changed = convert(unpack_wesys(data[offset:offset + size]))
    for iterations in (5, 50, 200):  # more zopfli passes only when the slot needs them
        payload = zopfli.zlib.compress(raw, numiterations=iterations)
        # Same packing as repair_ios_tables: compressed, unencrypted WESYS, in the table's own slot
        packed = b"\x00\x01\x01WESYS" + struct.pack("<II", len(payload), len(raw)) + payload
        if len(packed) <= size:
            break
    if len(packed) > size:
        raise SystemExit(f"{name} replacement ({len(packed)} B) exceeds its allocation ({size} B)")
    result[offset:offset + size] = packed.ljust(size, b"\0")
    return bytes(result), changed


def convert_cpk(data, fields=(DISPLAY,)):
    return rewrite_table(data, "Player.bin", lambda raw: convert_player_bin(raw, fields))


if __name__ == "__main__":
    assert titlecase("L. SUAREZ", "Luis Suárez") == "L. Suárez"
    assert titlecase("RONALDO", "Cristiano Ronaldo") == "Ronaldo"
    assert titlecase("DE JONG", "Frenkie de Jong") == "de Jong"
    assert titlecase("Y G KIM", "Kim Young-Gwon") == "Y G Kim"
    assert titlecase("O'NEILL", "") == "O'Neill"
    assert titlecase("S.CAZORLA", "Santi Cazorla") == "S.Cazorla"
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    ap.add_argument("--shirt", action="store_true", help="also the shirt name (menus, kit)")
    ap.add_argument("--names-json", type=Path, help="also write {player base id: name} (ZIRU_PLAYER_NAMES.json)")
    ap.add_argument("--teams", type=Path, help="ziru-extra/responses/ZIRU_REAL_TEAM_NAMES.json: real club names into Team.bin")
    args = ap.parse_args()
    if args.output.resolve() == args.input.resolve():
        raise SystemExit("use a separate output path")
    fixed, changed = convert_cpk(args.input.read_bytes(), (DISPLAY, SHIRT) if args.shirt else (DISPLAY,))
    teams = 0
    if args.teams:
        mapping = {k: v for k, v in json.loads(args.teams.read_text(encoding="utf-8")).items() if not k.startswith("_")}
        fixed, teams = rewrite_table(fixed, "Team.bin", lambda raw: convert_team_bin(raw, mapping))
    if args.names_json:
        header = utf_table(fixed[16:0x800])[0]
        toc = header["TocOffset"]
        length = struct.unpack_from("<Q", fixed, toc + 8)[0]
        row = next(r for r in utf_table(fixed[toc + 16:toc + 16 + length]) if r.get("FileName") == "Player.bin")
        offset = min(toc, header["ContentOffset"]) + row["FileOffset"]
        names = player_names(unpack_wesys(fixed[offset:offset + row["FileSize"]]))
        args.names_json.write_text(json.dumps(names, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(fixed)
    print(json.dumps({"names_changed": changed, "teams_renamed": teams, "size": len(fixed),
                      "sha256": hashlib.sha256(fixed).hexdigest()}, indent=2))
