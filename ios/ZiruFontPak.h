// Vietnamese UI font for eFootball iOS 11.1.1.
//
// The iOS fonts (PesMobile/Content/Platforms/_IOS/Assets/ui/Font/eFootballSans-*.ufont, eFootballStencil-Regular.ufont
// in pc0000_mobile_ios.pak, OpenType CFF) have no glyphs for U+1EA0–U+1EF9, so "Tiếng Việt" renders as "Ting Vit".
// Ziru's Android mod ships the same four fonts with full Vietnamese coverage (ZFont_Orginal.pak). build_ipa.py
// extracts them to PESmobile.app/ziru/fonts/*.ufont; this module makes FPakPlatformFile serve those files whenever the
// engine opens the matching font path (vtable hook of OpenRead/FileSize/OpenAsyncRead). No pak is mounted and the
// imported game data is not touched.
#pragma once

namespace zfont {
// fontDir: bundle directory holding the four <name>.ufont files; documentsDir: app Documents (used only to remove
// files left by the earlier pak-mount attempt). log: line sink (may be null).
void install(const char* fontDir, const char* documentsDir, void (*log)(const char* line));
}  // namespace zfont
