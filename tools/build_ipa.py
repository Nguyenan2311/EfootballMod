#!/usr/bin/env python3
"""Build PES2027.ipa (code + bootstrap) and PES2027-data-<id>.zip (heavy data) from local inputs.

Nothing Konami/Ziru-owned is downloaded or uploaded: every input comes from your own disk.

Inputs
  --app        Payload/PESmobile.app of eFootball iOS 11.1.1 (decrypted, build 11.1.101)
  --dylib      ZiruOffline.dylib built by CI (GitHub Actions)
  --apk        Efootball_Offline_VH.apk (Ziru) – source of assets/responses and assets/livedata
  --out        output directory

Outputs
  PES2027.ipa                   app without pak/ cpk/, with dylib + Ziru responses + patches
  PES2027-data-<data_id>.zip    pak/ cpk/ livedata/ + data-manifest.json (stored, not recompressed)
  build-report.json             hashes of inputs/outputs and every patch applied

Usage (Windows PowerShell):
  python build_ipa.py --app D:\\Telegram\\Payload\\PESmobile.app --dylib ZiruOffline.dylib `
      --apk "D:\\Efootball 2027 Offline Full Epic - Bigtime by Ziru\\Efootball_Offline_VH.apk" `
      --commentary D:\\Telegram\\dt510_eng_mobile_all.cpk --out D:\\PES2027\\out
"""
import argparse, hashlib, json, os, plistlib, shutil, struct, sys, time, zipfile

EXPECTED_EXE_SHA256 = "0cf9bacd9a9f049587f78a5fb8646b4ed5b87abe40a761d1f8f6337cafa9a0de"
EXPECTED_VERSION = ("11.1.1", "11.1.101")
DYLIB_INSTALL_NAME = b"@executable_path/Frameworks/ZiruOffline.dylib"
MOD_VERSION = "1.1.2"

# Unlock 120 FPS frame pacing for ProMotion displays while keeping native quality settings.
DEFAULT_COMMANDLINE = b"-t.MaxFPS=120\n"

# basic::PathGet(PathType): types 6 (cpk) and 7 (pak) are built from GetAppBundlePath() (x20).
# Switch them to GetDocumentsPath() (x21) so the game reads Documents/cpk and Documents/pak,
# which the dylib links to Library/Application Support/PES2027/GameData/<data_id>/.
TEXT_VMADDR = 0x100000000
PATCHES = [
    # basic::PathGet(PathType): types 6 (cpk) and 7 (pak) redirected to Documents/
    (0x1000155BC, 0xF9000FF4, 0xF9000FF5, "PathGet cpk: str x20,[sp,#0x18] -> str x21"),
    (0x1000155C0, 0xAA1403E0, 0xAA1503E0, "PathGet cpk: mov x0,x20 -> mov x0,x21"),
    (0x10001561C, 0xF9000FF4, 0xF9000FF5, "PathGet pak: str x20,[sp,#0x18] -> str x21"),
    (0x100015620, 0xAA1403E0, 0xAA1503E0, "PathGet pak: mov x0,x20 -> mov x0,x21"),

    # Permanently bypass EULA & Privacy Policy on startup
    # 1. Client UI skip check: UUEMenuEulaAgree::IsSkip() unconditionally returns true (1)
    (0x100CD1618, 0xA9BF7BFD, 0x52800020, "IsSkip: stp -> mov w0, #1"),
    (0x100CD161C, 0x910003FD, 0xD65F03C0, "IsSkip: mov fp, sp -> ret"),
    # 2. Login subtask check: onlinemode::UtilityEula::SetupBeforeEulaAgree() unconditionally sets *out = false (0)
    (0x1048AB730, 0xD10143FF, 0x3900001F, "SetupBeforeEulaAgree: sub sp -> strb wzr, [x0]"),
    (0x1048AB734, 0xA9034FF4, 0xD65F03C0, "SetupBeforeEulaAgree: stp -> ret"),

    # FrameRateController table: Mode 0 = 120 FPS, Mode 1 = 90 FPS, Mode 2 = 60 FPS, Mode 3 = 30 FPS
    (0x1057461F8, 0x0000003C, 0x00000078, "FrameRateController table: 60 -> 120 fps"),
    (0x1057461FC, 0x0000001E, 0x0000005A, "FrameRateController table: 30 -> 90 fps"),
    (0x105746200, 0x00000014, 0x0000003C, "FrameRateController table: 20 -> 60 fps"),
    (0x105746204, 0x0000000F, 0x0000001E, "FrameRateController table: 15 -> 30 fps"),

    # Strings "120 fps\0", "90 fps\0", "60 fps\0", "30 fps\0" placed in dead code space of SetupBeforeEulaAgree
    (0x1048AB738, 0xA9047BFD, 0x20303231, "120fps string: '120 '"),
    (0x1048AB73C, 0x910103FD, 0x00737066, "120fps string: 'fps\\0'"),
    (0x1048AB740, 0xAA0003F3, 0x66203039, "90fps string: '90 f'"),
    (0x1048AB744, 0x9404A059, 0x00007370, "90fps string: 'ps\\0\\0'"),
    (0x1048AB748, 0xB40003E0, 0x66203036, "60fps string: '60 f'"),
    (0x1048AB74C, 0xF9400414, 0x00007370, "60fps string: 'ps\\0\\0'"),
    (0x1048AB750, 0xA901FFFF, 0x66203033, "30fps string: '30 f'"),
    (0x1048AB754, 0xF90017FF, 0x00007370, "30fps string: 'ps\\0\\0'"),

    # Trampoline for stringdb::StringDataManager::GetString: check IDs for 120, 90, 60, 30 fps
    (0x1048AB758, 0xA9007FFF, 0x52802D28, "trampoline: mov w8, #0x169"),
    (0x1048AB75C, 0xF9000BFF, 0x72A08E28, "trampoline: movk w8, #0x471, lsl #16"),
    (0x1048AB760, 0x910063E0, 0x6B08003F, "trampoline: cmp w1, w8"),
    (0x1048AB764, 0x97FFFE10, 0x540001A0, "trampoline: b.eq is_120"),
    (0x1048AB768, 0x910063E0, 0x11000508, "trampoline: add w8, w8, #1"),
    (0x1048AB76C, 0x910003E1, 0x6B08003F, "trampoline: cmp w1, w8"),
    (0x1048AB770, 0x97FFFE2D, 0x54000180, "trampoline: b.eq is_90"),
    (0x1048AB774, 0xAA1403E0, 0x52804A88, "trampoline: mov w8, #0x254"),
    (0x1048AB778, 0x9404B3E0, 0x72A08E28, "trampoline: movk w8, #0x471, lsl #16"),
    (0x1048AB77C, 0xA94023E0, 0x6B08003F, "trampoline: cmp w1, w8"),
    (0x1048AB780, 0xEB00011F, 0x54000140, "trampoline: b.eq is_60"),
    (0x1048AB784, 0x540000E0, 0x51000508, "trampoline: sub w8, w8, #1"),
    (0x1048AB788, 0x910003E1, 0x6B08003F, "trampoline: cmp w1, w8"),
    (0x1048AB78C, 0xAA1403E0, 0x54000120, "trampoline: b.eq is_30"),
    (0x1048AB790, 0x9404B5D8, 0xD10103FF, "trampoline: sub sp, sp, #0x40"),
    (0x1048AB794, 0x52800008, 0x140B20FC, "trampoline: b 0x104b73b84"),
    (0x1048AB798, 0xF94003E0, 0x10FFFD00, "trampoline is_120: adr x0, 0x1048ab738"),
    (0x1048AB79C, 0x14000002, 0xD65F03C0, "trampoline is_120: ret"),
    (0x1048AB7A0, 0x52800028, 0x10FFFD00, "trampoline is_90: adr x0, 0x1048ab740"),
    (0x1048AB7A4, 0x39000268, 0xD65F03C0, "trampoline is_90: ret"),
    (0x1048AB7A8, 0xB4000060, 0x10FFFD00, "trampoline is_60: adr x0, 0x1048ab748"),
    (0x1048AB7AC, 0xF90007E0, 0xD65F03C0, "trampoline is_60: ret"),
    (0x1048AB7B0, 0x96DDE7F8, 0x10FFFD00, "trampoline is_30: adr x0, 0x1048ab750"),
    (0x1048AB7B4, 0xF9400FE0, 0xD65F03C0, "trampoline is_30: ret"),

    # Hook entry of stringdb::StringDataManager::GetString -> jump to trampoline (0x1048AB758)
    (0x104B73B80, 0xD10103FF, 0x17F4DEF6, "GetString: sub sp -> b 0x1048ab758"),

    # Prevent match start crash in IMatchDataStoreObserver::execMatchCameraTypeChanged on null observer Context (x19 == 0)
    (0x1014D52E0, 0xF9400268, 0x14CF5936, "CameraChanged: ldr x8,[x19] -> b 0x1048ab7b8"),
    (0x1048AB7B8, 0xB4000060, 0xB40000B3, "camera trampoline: cbz x19, 0x1048ab7cc"),
    (0x1048AB7BC, 0xF90013E0, 0xF9400268, "camera trampoline: ldr x8, [x19]"),
    (0x1048AB7C0, 0x96DDE7F4, 0xF9400108, "camera trampoline: ldr x8, [x8]"),
    (0x1048AB7C4, 0xA9447BFD, 0xAA1303E0, "camera trampoline: mov x0, x19"),
    (0x1048AB7C8, 0xA9434FF4, 0xD63F0100, "camera trampoline: blr x8"),
    (0x1048AB7CC, 0x910143FF, 0x1730A6C9, "camera trampoline: b 0x1014d52f0"),

    # Prevent main menu banner / talent point crash in onlinemode::ParameterMyClubMisc::GetCareerPlanCommonParameterList (0x1047decec)
    (0x1047DECD8, 0xF9400808, 0xD65F03C0, "GetCareerPlanCommonParameterList: entry -> ret"),

    # Prevent AddStadiumDlTask crash at 0x1046fe0b4 on null interrupt size by branching directly to return success
    (0x1046FE0B0, 0xB4000060, 0x14000003, "AddStadiumDlTask::Bind: branch over null SetInterruptGameSize"),

    # ACharacterActor::SetUniformConfigType(bool), 0x1008f2fa8, selects
    # licensed/edit font mode (0/2). Preserve it and BackNumberStep's load of
    # that mode at 0x10089f4a4: forcing 2 redirects BOTH names and numbers.
    # Enable only the actual name visibility flag, stored at actor + 0xf10.
    (0x10162331C, 0x1A9F07E1, 0x52800021, "SetupUniformConfig: cset w1, ne -> mov w1, #1"),
    (0x100895F44, 0x397C4268, 0x52800028, "APlayerActorMobile::SetUniformName: ldrb w8,[x19,#0xf10] -> mov w8, #1"),
    (0x100895F50, 0x540008C1, 0xD503201F, "APlayerActorMobile::SetUniformName: b.ne -> nop"),
    (0x100896058, 0x397C4268, 0x52800028, "APlayerActorMobile::SetUniformName (init): ldrb w8,[x19,#0xf10] -> mov w8, #1"),
    (0x100896064, 0x54FFF780, 0x17FFFFBC, "APlayerActorMobile::SetUniformName (init): b.eq -> b 0x100895f54"),
    (0x100911418, 0x397C426A, 0x5280002A, "APlayerActor::SetUniformName: ldrb w10,[x19,#0xf10] -> mov w10, #1"),
    (0x10091141C, 0x36001A2A, 0xD503201F, "APlayerActor::SetUniformName: tbz -> nop"),
    (0x100911448, 0x397C4268, 0x52800028, "APlayerActor::SetUniformName: ldrb w8,[x19,#0xf10] -> mov w8, #1"),
    (0x100911450, 0x54001881, 0xD503201F, "APlayerActor::SetUniformName: b.ne -> nop"),
    (0x10089F61C, 0x392ABF3F, 0xD503201F, "BackNumberStep: do not clear nameTexActor enable flag 0x1aaf"),

    # Unlock selectable real stadiums in all match modes (Authentic, Exhibition, Tour, etc.)
    (0x104892B08, 0x540003A1, 0xD503201F, "GetSelectableStadium: b.ne -> nop"),

    # Format in-game player display names in Title Case ("Viết hoa chữ đầu thôi") in ConvertAllCaps (0x1046D2E0C)
    # Copies src to dst via basic_string::operator= (0x1000222C8) and formats words to Title Case in-place.
    (0x1046D2E0C, 0xD10743FF, 0xA9BE7BFD, "ConvertAllCaps: stp x29, x30, [sp, #-0x20]!"),
    (0x1046D2E10, 0xA91867FA, 0x910003FD, "ConvertAllCaps: mov x29, sp"),
    (0x1046D2E14, 0xA9195FF8, 0xA9014FF4, "ConvertAllCaps: stp x20, x19, [sp, #0x10]"),
    (0x1046D2E18, 0xA91A57F6, 0xAA0003F3, "ConvertAllCaps: mov x19, x0 (dst)"),
    (0x1046D2E1C, 0xA91B4FF4, 0x96E541EB, "ConvertAllCaps: bl basic_string::operator= (0x1000222C8)"),
    (0x1046D2E20, 0xA91C7BFD, 0x39C05E68, "ConvertAllCaps: ldrsb w8, [x19, #0x17]"),
    (0x1046D2E24, 0x910703FD, 0xF9400269, "ConvertAllCaps: ldr x9, [x19]"),
    (0x1046D2E28, 0xAA0203F4, 0x7100011F, "ConvertAllCaps: cmp w8, #0"),
    (0x1046D2E2C, 0xAA0103F5, 0x9A93B128, "ConvertAllCaps: csel x8, x9, x19, lt"),
    (0x1046D2E30, 0xAA0003F3, 0x52800029, "ConvertAllCaps: mov w9, #1 (newWord)"),
    (0x1046D2E34, 0x9102C3F6, 0x3940010A, "ConvertAllCaps: ldrb w10, [x8]"),
    (0x1046D2E38, 0xF000B568, 0x3400038A, "ConvertAllCaps: cbz w10, done"),
    (0x1046D2E3C, 0xF9464D08, 0x7100815F, "ConvertAllCaps: cmp w10, #' '"),
    (0x1046D2E40, 0xF9400108, 0x540002E0, "ConvertAllCaps: b.eq is_space"),
    (0x1046D2E44, 0xF81B83A8, 0x7100B95F, "ConvertAllCaps: cmp w10, #'.'"),
    (0x1046D2E48, 0x910183E8, 0x540002A0, "ConvertAllCaps: b.eq is_space"),
    (0x1046D2E4C, 0x940000C2, 0x7100B55F, "ConvertAllCaps: cmp w10, #'-'"),
    (0x1046D2E50, 0xA904FFFF, 0x54000260, "ConvertAllCaps: b.eq is_space"),
    (0x1046D2E54, 0xF9002FFF, 0x71009D5F, "ConvertAllCaps: cmp w10, #'''"),
    (0x1046D2E58, 0x52800500, 0x54000220, "ConvertAllCaps: b.eq is_space"),
    (0x1046D2E5C, 0x943CF7F7, 0x35000109, "ConvertAllCaps: cbnz w9, tc_first"),
    (0x1046D2E60, 0xD2800001, 0x7101055F, "ConvertAllCaps: cmp w10, #'A'"),
    (0x1046D2E64, 0x943CF7AD, 0x540001E3, "ConvertAllCaps: b.lo tc_next"),
    (0x1046D2E68, 0x12BFFDE8, 0x7101695F, "ConvertAllCaps: cmp w10, #'Z'"),
    (0x1046D2E6C, 0xF9000C08, 0x540001A8, "ConvertAllCaps: b.hi tc_next"),
    (0x1046D2E70, 0xB900201F, 0x1100814A, "ConvertAllCaps: add w10, w10, #0x20"),
    (0x1046D2E74, 0x90011A68, 0x3900010A, "ConvertAllCaps: strb w10, [x8]"),
    (0x1046D2E78, 0x91150108, 0x1400000A, "ConvertAllCaps: b tc_next"),
    (0x1046D2E7C, 0x91004108, 0x7101855F, "ConvertAllCaps: cmp w10, #'a'"),
    (0x1046D2E80, 0xF9000008, 0x540000A3, "ConvertAllCaps: b.lo tc_not_lower"),
    (0x1046D2E84, 0x4F00E400, 0x7101E95F, "ConvertAllCaps: cmp w10, #'z'"),
    (0x1046D2E88, 0xAD0583E0, 0x54000068, "ConvertAllCaps: b.hi tc_not_lower"),
    (0x1046D2E8C, 0x3D8037E0, 0x5100814A, "ConvertAllCaps: sub w10, w10, #0x20"),
    (0x1046D2E90, 0x3C8382C0, 0x3900010A, "ConvertAllCaps: strb w10, [x8]"),
    (0x1046D2E94, 0x3C8482C0, 0x52800009, "ConvertAllCaps: mov w9, #0"),
    (0x1046D2E98, 0x3C8582C0, 0x14000002, "ConvertAllCaps: b tc_next"),
    (0x1046D2E9C, 0x3C8682C0, 0x52800029, "ConvertAllCaps: mov w9, #1"),
    (0x1046D2EA0, 0x3C8782C0, 0x91000508, "ConvertAllCaps: add x8, x8, #1"),
    (0x1046D2EA4, 0x3C8882C0, 0x17FFFFE4, "ConvertAllCaps: b tc_loop"),
    (0x1046D2EA8, 0x3C8982C0, 0x52800020, "ConvertAllCaps: mov w0, #1"),
    (0x1046D2EAC, 0x3C8A82C0, 0xA9414FF4, "ConvertAllCaps: ldp x20, x19, [sp, #0x10]"),
    (0x1046D2EB0, 0x39C05EA8, 0xA8C27BFD, "ConvertAllCaps: ldp x29, x30, [sp], #0x20"),
    (0x1046D2EB4, 0xF94002A9, 0xD65F03C0, "ConvertAllCaps: ret"),
]


def sha256_file(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


class Partial(Exception):
    pass


DEADLINE = [None]


def over_budget():
    return DEADLINE[0] is not None and time.time() > DEADLINE[0]


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ------------------------------------------------------------------ Mach-O patching
def patch_executable(data: bytearray, report):
    magic, cputype, _, _, ncmds, sizeofcmds, flags, _ = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != 0xFEEDFACF or cputype != 0x0100000C:
        raise SystemExit("PESmobile is not a thin arm64 Mach-O")
    # locate __TEXT fileoff to map vmaddr->file offset, and check for existing load
    off = 32
    text_fileoff = None
    first_sect = 1 << 62
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:
            seg = data[off + 8:off + 24].rstrip(b"\0")
            vmaddr, vmsize, fileoff = struct.unpack_from("<QQQ", data, off + 24)
            if seg == b"__TEXT":
                text_fileoff = fileoff
            nsects = struct.unpack_from("<I", data, off + 64)[0]
            for s in range(nsects):
                so = off + 72 + s * 80
                o = struct.unpack_from("<I", data, so + 48)[0]
                if o:
                    first_sect = min(first_sect, o)
        if cmd in (0xC, 0x18, 0x8000001F):
            name = data[off + struct.unpack_from("<I", data, off + 8)[0]:off + size].split(b"\0")[0]
            if name == DYLIB_INSTALL_NAME:
                raise SystemExit("Executable already contains ZiruOffline load command (patched twice?)")
        off += size
    for vm, old, new, desc in PATCHES:
        fo = vm - TEXT_VMADDR + text_fileoff
        cur = struct.unpack_from("<I", data, fo)[0]
        if cur != old:
            raise SystemExit(f"Unexpected bytes at {hex(vm)}: {cur:#010x} (wanted {old:#010x}) – wrong executable?")
        struct.pack_into("<I", data, fo, new)
        report["patches"].append({"vmaddr": hex(vm), "old": f"{old:#010x}", "new": f"{new:#010x}", "desc": desc})
    # append LC_LOAD_DYLIB
    name = DYLIB_INSTALL_NAME + b"\0"
    cmdsize = (24 + len(name) + 7) & ~7
    end = 32 + sizeofcmds
    if end + cmdsize > first_sect:
        raise SystemExit("No room for an extra load command")
    lc = struct.pack("<IIIIII", 0xC, cmdsize, 24, 2, 0x00010000, 0x00010000) + name
    lc += b"\0" * (cmdsize - len(lc))
    if any(data[end:end + cmdsize]):
        raise SystemExit("Load-command padding is not empty")
    data[end:end + cmdsize] = lc
    struct.pack_into("<II", data, 16, ncmds + 1, sizeofcmds + cmdsize)
    report["patches"].append({"load_dylib": DYLIB_INSTALL_NAME.decode(), "cmdsize": cmdsize})


# ------------------------------------------------------------------ IPA
def build_ipa(app, dylib, apk, out, report, bundle_id, display_name, extra_pak=None, online_server="", online_config=None, native_livedata=None):
    exe = os.path.join(app, "PESmobile")
    info_path = os.path.join(app, "Info.plist")
    with open(info_path, "rb") as f:
        info = plistlib.load(f)
    ver = (info.get("CFBundleShortVersionString"), info.get("CFBundleVersion"))
    if ver != EXPECTED_VERSION:
        raise SystemExit(f"App version {ver} != expected {EXPECTED_VERSION}")
    exe_sha = sha256_file(exe)
    report["inputs"]["PESmobile_sha256"] = exe_sha
    if exe_sha != EXPECTED_EXE_SHA256:
        raise SystemExit(f"PESmobile SHA-256 {exe_sha} does not match the analysed binary {EXPECTED_EXE_SHA256}")
    data = bytearray(open(exe, "rb").read())
    patch_executable(data, report)

    info["UIFileSharingEnabled"] = True
    info["LSSupportsOpeningDocumentsInPlace"] = True
    info["CADisableMinimumFrameDurationOnPhone"] = True
    info["CADisableMinimumFrameDuration"] = True
    if bundle_id:
        info["CFBundleIdentifier"] = bundle_id
    if display_name:
        info["CFBundleDisplayName"] = display_name
        info["CFBundleName"] = display_name

    ipa = os.path.join(out, "PES2027.ipa")
    tmp = ipa + ".tmp"
    skip_names = {"pak", "cpk", "_CodeSignature", "SC_Info", "embedded.mobileprovision"}
    n = 0
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
        root = "Payload/PESmobile.app/"
        added_dirs = set()

        def ensure_dir(d):
            if not d.endswith("/"):
                d += "/"
            cur = ""
            for p in d.rstrip("/").split("/"):
                cur += p + "/"
                if cur not in added_dirs:
                    zi = zipfile.ZipInfo(cur, time.localtime()[:6])
                    zi.external_attr = 0o040755 << 16
                    z.writestr(zi, b"")
                    added_dirs.add(cur)

        ensure_dir("Payload/")
        ensure_dir(root)
        ensure_dir(root + "Frameworks/")
        ensure_dir(root + "ziru/")
        ensure_dir(root + "ziru/responses/")

        for dirpath, dirnames, filenames in os.walk(app):
            rel = os.path.relpath(dirpath, app).replace("\\", "/")
            parts = rel.split("/")
            if any(p in skip_names for p in parts):
                dirnames[:] = []
                continue
            dirnames[:] = [d for d in dirnames if d not in skip_names]
            if rel != ".":
                ensure_dir(root + rel)
            for fn in filenames:
                if fn in {"embedded.mobileprovision", ".DS_Store"}:
                    continue
                p = os.path.join(dirpath, fn)
                arc = root + (fn if rel == "." else f"{rel}/{fn}")
                is_exec = (fn == "PESmobile" or fn.endswith(".dylib") or fn == "gadsme")
                mode = 0o100755 if is_exec else 0o100644
                zi = zipfile.ZipInfo(arc, time.localtime()[:6])
                zi.external_attr = mode << 16
                zi.compress_type = zipfile.ZIP_DEFLATED
                if arc == root + "PESmobile":
                    z.writestr(zi, bytes(data))
                elif arc == root + "Info.plist":
                    z.writestr(zi, plistlib.dumps(info, fmt=plistlib.FMT_XML))
                elif fn in ("ue4commandline.txt", "uecommandline.txt"):
                    z.writestr(zi, DEFAULT_COMMANDLINE)
                elif fn.endswith(".plist"):
                    try:
                        with open(p, "rb") as pf:
                            sub_pl = plistlib.load(pf)
                        z.writestr(zi, plistlib.dumps(sub_pl, fmt=plistlib.FMT_XML))
                    except Exception:
                        with open(p, "rb") as pf:
                            z.writestr(zi, pf.read())
                else:
                    with open(p, "rb") as f:
                        z.writestr(zi, f.read())
                n += 1
        for cl_name in ("ue4commandline.txt", "uecommandline.txt"):
            cl_arc = root + cl_name
            if cl_arc not in z.namelist():
                zi = zipfile.ZipInfo(cl_arc, time.localtime()[:6])
                zi.external_attr = 0o100644 << 16
                zi.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(zi, DEFAULT_COMMANDLINE)
                n += 1
        zi = zipfile.ZipInfo(root + "Frameworks/ZiruOffline.dylib", time.localtime()[:6])
        zi.external_attr = 0o100755 << 16
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, open(dylib, "rb").read())
        extra_names = set()
        for extra_dir in (os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ziru-extra", "responses"),
                          os.path.join(os.path.dirname(os.path.abspath(__file__)), "ziru-extra", "responses")):
            if os.path.isdir(extra_dir):
                for root_dir, dirs, files in os.walk(extra_dir):
                    for fn in files:
                        rel = os.path.relpath(os.path.join(root_dir, fn), extra_dir).replace("\\", "/")
                        extra_names.add(rel)
                break
        # Ziru responses (JSON templates) from the APK
        with zipfile.ZipFile(apk) as a:
            for e in a.infolist():
                if e.filename.startswith("assets/responses/") and not e.is_dir():
                    rel_apk = e.filename[len("assets/responses/"):]
                    if rel_apk in extra_names or os.path.basename(e.filename) in extra_names:
                        continue   # overridden by ziru-extra/responses
                    arc = root + "ziru/responses/" + rel_apk
                    ensure_dir(os.path.dirname(arc))
                    zi = zipfile.ZipInfo(arc, time.localtime()[:6])
                    zi.external_attr = 0o100644 << 16
                    zi.compress_type = zipfile.ZIP_DEFLATED
                    z.writestr(zi, a.read(e))
                    n += 1
        # Responses written for this port (e.g. commands new in 11.1.1); added after Ziru's, so they win
        for extra_dir in (os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ziru-extra", "responses"),
                          os.path.join(os.path.dirname(os.path.abspath(__file__)), "ziru-extra", "responses")):
            if not os.path.isdir(extra_dir):
                continue
            for root_dir, dirs, files in os.walk(extra_dir):
                for fn in sorted(files):
                    src = os.path.join(root_dir, fn)
                    rel = os.path.relpath(src, extra_dir).replace("\\", "/")
                    arc = root + "ziru/responses/" + rel
                    ensure_dir(os.path.dirname(arc))
                    zi = zipfile.ZipInfo(arc, time.localtime()[:6])
                    zi.external_attr = 0o100644 << 16
                    zi.compress_type = zipfile.ZIP_DEFLATED
                    z.writestr(zi, open(src, "rb").read())
                    report.setdefault("extra_responses", []).append(rel)
                    n += 1
            break
        # Built-in fallbacks for commands Ziru's server (client 11.0.1) never answered; ziru-extra/ overrides them
        for fn, body in BUILTIN_RESPONSES.items():
            if fn in extra_names:
                continue
            zi = zipfile.ZipInfo(root + "ziru/responses/" + fn, time.localtime()[:6])
            zi.external_attr = 0o100644 << 16
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, body.encode())
            report.setdefault("builtin_responses", []).append(fn)
            n += 1
        # Embed livedata (all 55 Epic/BigTime paks/ucas/utoc and dt870) directly into the IPA
        table_fixes = {}
        with zipfile.ZipFile(apk) as a:
            for e in a.infolist():
                if e.filename.startswith("assets/livedata/") and not e.is_dir():
                    arc = root + "ziru/livedata/" + e.filename[len("assets/livedata/"):]
                    ensure_dir(os.path.dirname(arc))
                    zi = zipfile.ZipInfo(arc, time.localtime()[:6])
                    zi.external_attr = 0o100644 << 16
                    zi.compress_type = zipfile.ZIP_DEFLATED
                    content = a.read(e)
                    if os.path.basename(e.filename) == "dt870_mobile_and.cpk":
                        from repair_ios_tables import repair_cpk_bytes
                        original_hash = hashlib.sha256(content).hexdigest()
                        content, changes = repair_cpk_bytes(content)
                        if changes:
                            table_fixes[os.path.basename(e.filename)] = {"original_sha256": original_hash,
                                "sha256": hashlib.sha256(content).hexdigest(), "changes": changes}
                            report["ios_table_fixes"] = table_fixes
                    z.writestr(zi, content)
                    n += 1
        if table_fixes:
            z.writestr(root + "ziru/table-fixes.json", json.dumps(table_fixes))
            n += 1
        if native_livedata:
            entries = json.load(open(os.path.join(native_livedata, "native-manifest.json"), encoding="utf-8"))
            for entry in entries:
                name = entry["name"]
                if os.path.basename(name) != name or "_mobile_ios." not in name:
                    raise SystemExit("invalid native livedata name")
                src = os.path.join(native_livedata, name)
                if os.path.getsize(src) != entry["size"] or sha256_file(src) != entry["sha256"]:
                    raise SystemExit("native livedata integrity failed: " + name)
                z.write(src, root + "ziru/livedata/" + name, compress_type=zipfile.ZIP_DEFLATED)
                n += 1
            report["native_livedata"] = entries
            z.writestr(root + "ziru/native-livedata.json", json.dumps(entries))
            n += 1
        # Extra UI container (tools/make_extra_pak.py): card photos, emblems and kit thumbnails that Ziru's Android
        # data has and the iOS data lacks. The dylib links ziru/pak_extra/* into Documents/pak at launch.
        if extra_pak and os.path.isdir(extra_pak):
            for fn in sorted(os.listdir(extra_pak)):
                if not fn.endswith((".pak", ".utoc", ".ucas")):
                    continue
                zi = zipfile.ZipInfo(root + "ziru/pak_extra/" + fn, time.localtime()[:6])
                zi.external_attr = 0o100644 << 16
                zi.compress_type = zipfile.ZIP_STORED
                z.writestr(zi, open(os.path.join(extra_pak, fn), "rb").read())
                report.setdefault("extra_pak", []).append(fn)
                n += 1
        # Online (1.1): default server for Friend Match; players can override it in Documents/PES2027-online.json
        if online_server:
            zi = zipfile.ZipInfo(root + "ziru/online.json", time.localtime()[:6])
            zi.external_attr = 0o100644 << 16
            z.writestr(zi, json.dumps({**(online_config or {}), "server": online_server, "enabled": True}))
            report["online_server"] = online_server
            n += 1
        fonts = make_ios_fonts(apk, report)
        if fonts:
            ensure_dir(root + "ziru/fonts/")
        for fname, fdata in sorted(fonts.items()):
            zi = zipfile.ZipInfo(root + "ziru/fonts/" + fname, time.localtime()[:6])
            zi.external_attr = 0o100644 << 16
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, fdata)
            n += 1
    os.replace(tmp, ipa)
    report["outputs"]["ipa"] = {"path": ipa, "size": os.path.getsize(ipa), "sha256": sha256_file(ipa), "files": n}
    log(f"IPA written: {ipa} ({os.path.getsize(ipa) / 1e6:.1f} MB)")


# ------------------------------------------------------------------ Vietnamese UI font (from Ziru's Android mod)
# Responses for commands new in client 11.1.1 (see ziru-extra/README.md). Not taken from Ziru's APK.
BUILTIN_RESPONSES = {
    "CMD_GET_AGE_GATE_REQUIREMENTS.json":
        '{"msgid":"CMD_GET_AGE_GATE_REQUIREMENTS","result":"NOERR","is_eula_country":"NO","country_age_kind":"NONE"}',
}
ZIRU_OBB_IN_APK = "assets/pc0000_mobile_and.pak"            # a ZIP (Android "OBB" layout) despite the name
ZIRU_FONT_PAK = "PesMobile/Content/Paks/ZFont_Orginal.pak"   # UE pak v3: eFootballSans-*/eFootballStencil .ufont


def make_ios_fonts(apk, report):
    """The four UI fonts (OpenType, full Vietnamese) from Ziru's ZFont_Orginal.pak, as {name: bytes}.
    The dylib serves them in place of the iOS fonts; no pak is mounted on the device."""
    import struct, zlib
    apks_to_try = [apk]
    d = None
    for cand in apks_to_try:
        if not cand or not os.path.exists(cand): continue
        try:
            with zipfile.ZipFile(cand) as a:
                if ZIRU_OBB_IN_APK in a.namelist():
                    with zipfile.ZipFile(a.open(ZIRU_OBB_IN_APK)) as o:
                        if ZIRU_FONT_PAK in o.namelist():
                            d = o.read(ZIRU_FONT_PAK)
                            break
        except Exception:
            pass
    if not d:
        repo_fonts = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fonts_vn")
        font_dirs = [repo_fonts]
        for fdir in font_dirs:
            if os.path.isdir(fdir):
                fonts = {}
                for fn in sorted(os.listdir(fdir)):
                    if fn.endswith(".ufont"):
                        fonts[fn] = open(os.path.join(fdir, fn), "rb").read()
                if fonts:
                    log(f"font: loaded {len(fonts)} full Vietnamese UI fonts from {fdir}")
                    report["fonts"] = {k: {"size": len(v), "sha256": hashlib.sha256(v).hexdigest()} for k, v in sorted(fonts.items())}
                    return fonts
        log("font: ZFont_Orginal.pak not found – Vietnamese font fix skipped")
        return {}
    magic = d.rfind(struct.pack("<I", 0x5A6F12E1))
    ver, = struct.unpack_from("<I", d, magic + 4)
    ioff, isz = struct.unpack_from("<QQ", d, magic + 8)
    if ver != 3 or d[magic - 1] != 0:
        raise SystemExit(f"font pak: unexpected version {ver} / encrypted index")
    idx = d[ioff:ioff + isz]
    n, = struct.unpack_from("<i", idx, 0)
    p = 4 + n
    count, = struct.unpack_from("<i", idx, p); p += 4
    fonts = {}
    for _ in range(count):
        ln, = struct.unpack_from("<i", idx, p); p += 4
        name = idx[p:p + ln].rstrip(b"\0").decode("latin1"); p += ln
        off, size, usize, comp = struct.unpack_from("<QQQI", idx, p); p += 28 + 20
        blocks = []
        if comp:
            nb, = struct.unpack_from("<I", idx, p); p += 4
            for _ in range(nb):
                blocks.append(struct.unpack_from("<QQ", idx, p)); p += 16
        encrypted = idx[p]; p += 1 + 4
        if encrypted or comp not in (0, 1):
            raise SystemExit(f"font pak: {name} uses unsupported compression/encryption")
        if comp:
            data = b"".join(zlib.decompress(d[s0:e0]) for s0, e0 in blocks)
        else:
            hdr = 8 + 8 + 8 + 4 + 20 + 1 + 4
            data = d[off + hdr:off + hdr + size]
        if len(data) != usize or data[:4] not in (b"OTTO", b"\0\1\0\0"):
            raise SystemExit(f"font pak: {name} did not extract to a font file")
        fonts[name] = data
    want = {"eFootballSans-Bold.ufont", "eFootballSans-Light.ufont", "eFootballSans-Regular.ufont",
            "eFootballStencil-Regular.ufont"}
    if set(fonts) != want:
        raise SystemExit(f"font pak: unexpected contents {sorted(fonts)}")
    report["fonts"] = {k: {"size": len(v), "sha256": hashlib.sha256(v).hexdigest()} for k, v in sorted(fonts.items())}
    log(f"font: {len(fonts)} Vietnamese UI fonts from {ZIRU_FONT_PAK}")
    return fonts

# ------------------------------------------------------------------ data pack
def _dos_time(t=None):
    lt = time.localtime(t)
    return ((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec // 2),
            ((max(lt.tm_year, 1980) - 1980) << 9) | (lt.tm_mon << 5) | lt.tm_mday)


def write_pack_resumable(tmp, final, manifest, files, recs, apk):
    """STORED ZIP64 writer whose progress survives being killed: state is saved after every
    chunk, and a restart truncates the file to the last saved position and continues."""
    import struct
    import zlib
    state_path = tmp + ".state.json"
    mbytes = json.dumps(manifest, indent=1).encode()
    jobs = [("data-manifest.json", ("bytes", None), len(mbytes))]
    sizes = {r["path"]: r["size"] for r in recs}
    jobs += [(arc, spec, sizes[arc]) for arc, spec in files]
    st = None
    if os.path.exists(state_path) and os.path.exists(tmp):
        st = json.load(open(state_path))
        if st.get("data_id") != manifest["data_id"]:
            st = None
    if st is None:
        st = {"data_id": manifest["data_id"], "entries": [], "cur": None, "pos": 0}
    tm, dt = _dos_time()

    def save():
        with open(state_path + ".new", "w") as f:
            json.dump(st, f)
        os.replace(state_path + ".new", state_path)

    f = open(tmp, "r+b" if os.path.exists(tmp) else "w+b")
    f.truncate(st["pos"])
    f.seek(st["pos"])
    done_names = {e["name"] for e in st["entries"]}
    apkz = None
    for name, (kind, src), size in jobs:
        if name in done_names:
            continue
        cur = st["cur"]
        if cur is None or cur["name"] != name:
            if over_budget():
                f.close(); save(); raise Partial("writing")
            nb = name.encode()
            hdr = struct.pack("<IHHHHHIIIHH", 0x04034b50, 45, 0x0800, 0, tm, dt, 0, 0xFFFFFFFF, 0xFFFFFFFF,
                              len(nb), 20) + nb + struct.pack("<HHQQ", 1, 16, size, size)
            cur = {"name": name, "offset": f.tell(), "hdr": len(hdr), "done": 0, "crc": 0}
            f.write(hdr)
            st["cur"] = cur; st["pos"] = f.tell(); f.flush(); save()
            log("  +", name, f"({size / 1048576:.1f} MB)")
        if kind == "bytes":
            data_iter = iter([mbytes])
        elif kind == "apk":
            # small entries: restart from the beginning of the entry if interrupted
            if cur["done"]:
                f.truncate(cur["offset"] + cur["hdr"]); f.seek(cur["offset"] + cur["hdr"])
                cur["done"] = 0; cur["crc"] = 0
            apkz = apkz or zipfile.ZipFile(apk)
            fi = apkz.open(src)
            data_iter = iter(lambda: fi.read(1 << 20), b"")
        else:
            fi = open(src, "rb")
            fi.seek(cur["done"])
            data_iter = iter(lambda: fi.read(8 << 20), b"")
        last_save = time.time()
        for chunk in data_iter:
            f.write(chunk)
            cur["crc"] = zlib.crc32(chunk, cur["crc"])
            cur["done"] += len(chunk)
            if time.time() - last_save > 5 or over_budget():
                f.flush(); st["pos"] = f.tell(); save(); last_save = time.time()
                if over_budget() and kind == "fs" and cur["done"] < size:
                    f.close(); raise Partial("writing " + name)
        if cur["done"] != size:
            raise SystemExit(f"size changed while packing: {name}")
        end = f.tell()
        f.seek(cur["offset"] + 14)
        f.write(struct.pack("<I", cur["crc"]))
        f.seek(end)
        st["entries"].append({"name": name, "offset": cur["offset"], "size": size, "crc": cur["crc"]})
        st["cur"] = None; st["pos"] = end; f.flush(); save()
    # central directory + ZIP64 end records
    cd_off = f.tell()
    for e in st["entries"]:
        nb = e["name"].encode()
        f.write(struct.pack("<IHHHHHHIIIHHHHHII", 0x02014b50, 45, 45, 0x0800, 0, tm, dt, e["crc"],
                            0xFFFFFFFF, 0xFFFFFFFF, len(nb), 28, 0, 0, 0, 0, 0xFFFFFFFF) + nb +
                struct.pack("<HHQQQ", 1, 24, e["size"], e["size"], e["offset"]))
    cd_end = f.tell()
    n = len(st["entries"])
    f.write(struct.pack("<IQHHIIQQQQ", 0x06064b50, 44, 45, 45, 0, 0, n, n, cd_end - cd_off, cd_off))
    f.write(struct.pack("<IIQI", 0x07064b50, 0, cd_end, 1))
    f.write(struct.pack("<IHHHHIIH", 0x06054b50, 0, 0, min(n, 0xFFFF), min(n, 0xFFFF), 0xFFFFFFFF, 0xFFFFFFFF, 0))
    f.close()
    os.replace(tmp, final)
    with open(state_path, "w") as sf:   # deletion may be blocked; leave a tiny marker instead
        sf.write("{}")
    log("data pack written:", final)


def build_data(app, apk, out, report, include_livedata=True, ziru_cpk=("dt500",), commentary=None):
    files = []  # (arcname, source spec)
    apk_names = set(zipfile.ZipFile(apk).namelist())
    for sub in ("pak", "cpk"):
        d = os.path.join(app, sub)
        for fn in sorted(os.listdir(d)):
            stem = fn.split("_mobile_")[0]
            src = ("fs", os.path.join(d, fn))
            if sub == "cpk" and stem in ziru_cpk and fn.endswith("_mobile_all.cpk") and f"assets/{fn}" in apk_names:
                # Ziru's CPK (platform-independent "mobile_all"): dt500 carries 604 extra uniform files
                src = ("apk", f"assets/{fn}")
                report.setdefault("cpk_from_ziru", []).append(fn)
            files.append((f"{sub}/{fn}", src))
    if commentary:
        files.append(("download/" + os.path.basename(commentary), ("fs", commentary)))
        report["commentary"] = os.path.abspath(commentary)
    if include_livedata:
        with zipfile.ZipFile(apk) as a:
            for e in a.infolist():
                if e.filename.startswith("assets/livedata/") and not e.is_dir():
                    files.append(("livedata/" + e.filename[len("assets/livedata/"):], ("apk", e.filename)))
    # ---- hashing (cached, resumable) ----
    cache_path = os.path.join(out, ".hash-cache.json")
    cache = json.load(open(cache_path)) if os.path.exists(cache_path) else {}
    log(f"hashing {len(files)} data files…")
    recs = []
    apkz = zipfile.ZipFile(apk)
    for arc, (kind, src) in files:
        key = f"{kind}:{src}"
        st = os.stat(src if kind == "fs" else apk)
        stamp = [st.st_size, int(st.st_mtime)]
        c = cache.get(key)
        if c and c["stamp"] == stamp:
            size, sha = c["size"], c["sha256"]
        else:
            if over_budget():
                json.dump(cache, open(cache_path, "w"))
                raise Partial("hashing")
            h = hashlib.sha256(); size = 0
            fi = open(src, "rb") if kind == "fs" else apkz.open(src)
            with fi:
                for b in iter(lambda: fi.read(1 << 20), b""):
                    h.update(b); size += len(b)
            sha = h.hexdigest()
            cache[key] = {"stamp": stamp, "size": size, "sha256": sha}
            json.dump(cache, open(cache_path, "w"))
        recs.append({"path": arc, "size": size, "sha256": sha})
    digest = hashlib.sha256(json.dumps(recs, sort_keys=True).encode()).hexdigest()[:12]
    data_id = f"pes2027-{EXPECTED_VERSION[1]}-{digest}"
    manifest = {"format": 1, "data_id": data_id, "client_version": EXPECTED_VERSION[0], "client_build": EXPECTED_VERSION[1],
                "mod_version": MOD_VERSION, "files": recs}
    path = os.path.join(out, f"PES2027-data-{data_id}.zip")
    tmp = path + ".tmp"
    # ---- writing (custom ZIP64 STORED writer, resumable at chunk level) ----
    if os.path.exists(path):
        log("data pack already complete:", path)
    else:
        write_pack_resumable(tmp, path, manifest, files, recs, apk)
    with open(os.path.join(out, "data-manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    total = sum(r["size"] for r in recs)
    report["outputs"]["data"] = {"path": path, "data_id": data_id, "files": len(recs), "payload_bytes": total, "zip_size": os.path.getsize(path)}
    log(f"data pack written: {path} ({os.path.getsize(path) / 1e9:.2f} GB, {len(recs)} files)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True)
    ap.add_argument("--dylib", required=True)
    ap.add_argument("--apk", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--bundle-id", default="", help="optional new CFBundleIdentifier (keep empty to let your signer decide)")
    ap.add_argument("--display-name", default="PES 2027")
    ap.add_argument("--skip-data", action="store_true", help="only build the IPA")
    ap.add_argument("--skip-ipa", action="store_true", help="only build/continue the data pack")
    ap.add_argument("--time-budget", type=float, default=0, help="seconds; stop cleanly and exit 3 when exceeded (run again to continue)")
    ap.add_argument("--no-livedata", action="store_true", help="do not ship Ziru's Android livedata in the data pack")
    ap.add_argument("--online-server", default="", help="default online server URL for Friend Match (mod 1.1), e.g. https://1.2.3.4:8443")
    ap.add_argument("--online-config", default="", help="local JSON with server/token/cert_sha256; keep this file outside Git")
    ap.add_argument("--extra-pak", default="", help="folder with pc7999_mobile_ios.{pak,utoc,ucas} from tools/make_extra_pak.py, bundled as ziru/pak_extra")
    ap.add_argument("--native-livedata", default="", help="verified native iOS livedata folder with native-manifest.json")
    ap.add_argument("--ziru-cpk", default="dt500", help="comma list of CPK stems taken from the Ziru APK instead of the IPA (default dt500; '' = none)")
    ap.add_argument("--commentary", default="", help="commentary CPK to install into the game's Download folder, e.g. D:\\Telegram\\dt510_eng_mobile_all.cpk (Vietnamese)")
    a = ap.parse_args()
    online_config = json.load(open(a.online_config, encoding="utf-8")) if a.online_config else {}
    a.online_server = a.online_server or online_config.get("server", "")
    os.makedirs(a.out, exist_ok=True)
    if a.time_budget:
        DEADLINE[0] = time.time() + a.time_budget
    report = {"tool": "build_ipa.py", "mod_version": MOD_VERSION, "time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
              "inputs": {"app": os.path.abspath(a.app), "dylib": os.path.abspath(a.dylib), "dylib_sha256": sha256_file(a.dylib),
                         "apk": os.path.abspath(a.apk)},
              "patches": [], "outputs": {}}
    report_path = os.path.join(a.out, "build-report.json")
    if not a.skip_ipa:
        build_ipa(a.app, a.dylib, a.apk, a.out, report, a.bundle_id, a.display_name, extra_pak=a.extra_pak or None, online_server=a.online_server, online_config=online_config, native_livedata=a.native_livedata or None)
        with open(report_path, "w") as f:
            json.dump(report, f, indent=1)
    else:
        # keep the IPA part of an earlier report; otherwise describe the existing IPA
        prev = json.load(open(report_path)) if os.path.exists(report_path) else {}
        if prev.get("outputs", {}).get("ipa"):
            report["patches"] = prev.get("patches", [])
            report["outputs"]["ipa"] = prev["outputs"]["ipa"]
            report["inputs"]["PESmobile_sha256"] = prev.get("inputs", {}).get("PESmobile_sha256")
        elif os.path.exists(os.path.join(a.out, "PES2027.ipa")):
            exe = os.path.join(a.app, "PESmobile")
            report["inputs"]["PESmobile_sha256"] = sha256_file(exe)
            patch_executable(bytearray(open(exe, "rb").read()), report)
            ipa = os.path.join(a.out, "PES2027.ipa")
            with zipfile.ZipFile(ipa) as zi:
                nfiles = len(zi.namelist())
            report["outputs"]["ipa"] = {"path": ipa, "size": os.path.getsize(ipa), "sha256": sha256_file(ipa), "files": nfiles,
                                        "note": "described afterwards (built by an earlier run)"}
    if not a.skip_data:
        try:
            build_data(a.app, a.apk, a.out, report, include_livedata=not a.no_livedata,
                       ziru_cpk=tuple(x for x in a.ziru_cpk.split(",") if x), commentary=a.commentary or None)
        except Partial as e:
            log(f"PARTIAL ({e}) – time budget reached, run the same command again to continue")
            sys.exit(3)
    with open(report_path, "w") as f:
        json.dump(report, f, indent=1)
    log("done. Sign PES2027.ipa with your usual tool (Sideloadly/ESign/…), install, then import the data zip in the app.")


if __name__ == "__main__":
    main()
