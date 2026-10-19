// See ZiruFontPak.h.
//
// Earlier versions mounted Ziru's font pak through UEBridgeSystem::MountPakFile; on device FPakFile::Initialize
// rejected it ("Index offset … is invalid") and aborted. This version mounts nothing: it replaces three slots of the
// FPakPlatformFile vtable (OpenRead, FileSize, OpenAsyncRead). When the engine asks for one of the four UI font files
// (…/Platforms/_IOS/Assets/ui/Font/<name>.ufont) the request is answered from the bundle copy
// (PESmobile.app/ziru/fonts/<name>.ufont) through the pak file's own lower-level platform file; every other path goes
// to the original functions untouched.
#include "ZiruFontPak.h"

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <mutex>
#include <set>
#include <string>

namespace {

// PESmobile 11.1.1 / 11.1.101 (sha256 0cf9bacd…a0de), unslid addresses.
const uint64_t kPakVtable = 0x10662b950;          // __ZTV16FPakPlatformFile
const uint64_t kSlotFileSize = kPakVtable + 0x10 + 0x80;
const uint64_t kSlotOpenRead = kPakVtable + 0x10 + 0xc8;
const uint64_t kSlotOpenAsyncRead = kPakVtable + 0x10 + 0x110;
const uint64_t kFnFileSize = 0x1029a605c;         // FPakPlatformFile::FileSize(const TCHAR*)
const uint64_t kFnOpenRead = 0x1029a4b30;         // FPakPlatformFile::OpenRead(const TCHAR*, bool)
const uint64_t kFnOpenAsyncRead = 0x102997684;    // FPakPlatformFile::OpenAsyncRead(const TCHAR*)
const size_t kLowerLevelOffset = 0x8;             // FPakPlatformFile::LowerLevel (see FPakPlatformFile::Mount)
// IPlatformFile vtable byte offsets (same layout for every platform file)
const size_t kVtFileSize = 0x80, kVtOpenRead = 0xc8, kVtOpenAsyncRead = 0x110;

const char* const kFonts[] = {"eFootballSans-Bold", "eFootballSans-Light", "eFootballSans-Regular",
                              "eFootballStencil-Regular"};
const int kNumFonts = 4;

typedef void* (*OpenReadFn)(void* self, const char16_t* name, bool allowWrite);
typedef int64_t (*FileSizeFn)(void* self, const char16_t* name);
typedef void* (*OpenAsyncReadFn)(void* self, const char16_t* name);
OpenReadFn gOrigOpenRead;
FileSizeFn gOrigFileSize;
OpenAsyncReadFn gOrigOpenAsyncRead;

// Non-trivial state lives on the heap: zfont::install runs from ZiruOffline.mm's constructor, which dyld calls before
// this file's static initializers, so a global std::string here would be re-initialized (emptied) after install.
struct FontState {
  std::u16string path[kNumFonts];   // bundle paths as TCHAR
  std::string data[kNumFonts];      // font bytes, loaded at install time
  std::set<std::string> seen;       // font paths already logged
};
FontState* gS = nullptr;
std::atomic<int> gFullReads[kNumFonts];
std::mutex gSeenMu;
std::atomic<int> gHits[kNumFonts];
void (*gLog)(const char*) = nullptr;

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...) {
  if (!gLog) return;
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  gLog(buf);
}

std::u16string widen(const std::string& s) { return std::u16string(s.begin(), s.end()); }   // bundle paths are ASCII

std::string narrow(const char16_t* s) {
  std::string r;
  if (s)
    for (; *s && r.size() < 600; s++) r += (*s < 128) ? (char)*s : '?';
  return r;
}

// Index of the font file `name` refers to, or -1. Matches ".../Assets/ui/Font/<font>.ufont" (case-insensitive).
int fontIndex(const char16_t* name) {
  if (!name) return -1;
  std::string lower = narrow(name);
  for (auto& c : lower) {
    if (c == '\\') c = '/';
    else c = (char)tolower((unsigned char)c);
  }
  for (int i = 0; i < kNumFonts; i++) {
    std::string want = std::string("/assets/ui/font/") + kFonts[i] + ".ufont";
    for (auto& c : want) c = (char)tolower((unsigned char)c);
    if (lower.size() >= want.size() && lower.compare(lower.size() - want.size(), want.size(), want) == 0) {
      return i;
    }
  }
  // Redirect Tazugane and any other UI font fallbacks to full Vietnamese UI fonts
  if (lower.find("/font/") != std::string::npos && lower.find(".ufont") != std::string::npos) {
    if (lower.find("blk") != std::string::npos || lower.find("bold") != std::string::npos || lower.find("heavy") != std::string::npos) {
      return 0; // eFootballSans-Bold (full Vietnamese)
    }
    return 2;   // eFootballSans-Regular (full Vietnamese)
  }
  return -1;
}

// IFileHandle as laid out in this engine (FIOSFileHandle vtable: D1, D0, Tell, Seek, SeekFromEnd, Read, Write, Flush,
// Truncate, Size). Serves a font from memory so nothing depends on how the lower platform file resolves bundle paths.
class ZMemFileHandle {
 public:
  ZMemFileHandle(const std::string* data, int idx) : data_(data), idx_(idx) {}
  virtual ~ZMemFileHandle() {}
  virtual int64_t Tell() { return pos_; }
  virtual bool Seek(int64_t p) {
    if (p < 0 || p > (int64_t)data_->size()) return false;
    pos_ = p;
    return true;
  }
  virtual bool SeekFromEnd(int64_t rel) { return Seek((int64_t)data_->size() + rel); }
  virtual bool Read(uint8_t* dst, int64_t n);
  virtual bool Write(const uint8_t*, int64_t) { return false; }
  virtual bool Flush(bool) { return false; }
  virtual bool Truncate(int64_t) { return false; }
  virtual int64_t Size() { return (int64_t)data_->size(); }

 private:
  const std::string* data_;
  int idx_;
  int64_t pos_ = 0;
};

inline void* lowerOf(void* self) { return *(void**)((char*)self + kLowerLevelOffset); }
template <typename F>
inline F vfn(void* obj, size_t off) {
  return *(F*)(*(char**)obj + off);
}

void noteHit(int i, const char* op, bool ok) {
  if (gHits[i].fetch_add(1) < 3) logf("[FONT] %s %s.ufont -> Ziru font from bundle: %s", op, kFonts[i], ok ? "ok" : "FAILED (using original)");
}

bool ZMemFileHandle::Read(uint8_t* dst, int64_t n) {
  if (n < 0 || pos_ + n > (int64_t)data_->size()) return false;
  memcpy(dst, data_->data() + pos_, (size_t)n);
  pos_ += n;
  if (pos_ == (int64_t)data_->size() && gFullReads[idx_].fetch_add(1) < 2)
    logf("[FONT] %s.ufont read to the end by the engine (%zu bytes, Ziru font)", kFonts[idx_], data_->size());
  return true;
}

// Log (once per path) every font-like file the engine opens through the pak layer, ours or not.
void noteFontPath(const char* op, const char16_t* name, const char* how) {
  std::string n = narrow(name), l = n;
  for (auto& c : l) c = (char)tolower((unsigned char)c);
  if (l.find("/font") == std::string::npos && l.find(".ufont") == std::string::npos && l.find(".ttf") == std::string::npos &&
      l.find(".otf") == std::string::npos)
    return;
  {
    std::lock_guard<std::mutex> g(gSeenMu);
    if (gS->seen.size() > 200 || !gS->seen.insert(std::string(op) + n).second) return;
  }
  logf("[FONT] %s %s -> %s", op, n.c_str(), how);
}

void* hookOpenRead(void* self, const char16_t* name, bool allowWrite) {
  int i = fontIndex(name);
  if (i >= 0 && !allowWrite && !gS->data[i].empty()) {
    noteFontPath("OpenRead", name, "Ziru font (memory)");
    noteHit(i, "OpenRead", true);
    return new ZMemFileHandle(&gS->data[i], i);
  }
  void* h = gOrigOpenRead(self, name, allowWrite);
  noteFontPath("OpenRead", name, h ? "original" : "not found");
  return h;
}

int64_t hookFileSize(void* self, const char16_t* name) {
  int i = fontIndex(name);
  if (i >= 0 && !gS->data[i].empty()) return (int64_t)gS->data[i].size();
  return gOrigFileSize(self, name);
}

void* hookOpenAsyncRead(void* self, const char16_t* name) {
  int i = fontIndex(name);
  if (i >= 0) {
    void* lower = lowerOf(self);
    void* h = lower ? vfn<OpenAsyncReadFn>(lower, kVtOpenAsyncRead)(lower, gS->path[i].c_str()) : nullptr;
    noteHit(i, "OpenAsyncRead", h != nullptr);
    if (h) return h;
  }
  void* h = gOrigOpenAsyncRead(self, name);
  noteFontPath("OpenAsyncRead", name, h ? "original" : "not found");
  return h;
}

void patchSlot(intptr_t slide, uint64_t slotVA, void* repl, void** orig) {
  void** slot = (void**)(uintptr_t)(slotVA + slide);
  *orig = *slot;
  *slot = repl;
}

}  // namespace

namespace zfont {

void install(const char* fontDir, const char* documentsDir, void (*log)(const char* line)) {
  gLog = log;
  // Leftovers of the previous (pak-mount) attempt: our own small files, never game data.
  if (documentsDir) {
    std::string old = std::string(documentsDir) + "/pak/ZiruFont_IOS_P.pak";
    std::string guard = std::string(documentsDir) + "/PES2027-logs/font-mount.pending";
    if (unlink(old.c_str()) == 0) logf("[FONT] removed old %s", old.c_str());
    unlink(guard.c_str());
  }
  if (!fontDir) return;
  gS = new FontState();
  for (int i = 0; i < kNumFonts; i++) {
    std::string p = std::string(fontDir) + "/" + kFonts[i] + ".ufont";
    struct stat st;
    if (stat(p.c_str(), &st) != 0 || st.st_size < 1024) {
      logf("[FONT] %s missing in the app bundle – Vietnamese font fix disabled", p.c_str());
      return;
    }
    gS->path[i] = widen(p);
    FILE* f = fopen(p.c_str(), "rb");
    if (f) {
      gS->data[i].resize((size_t)st.st_size);
      size_t got = fread(&gS->data[i][0], 1, gS->data[i].size(), f);
      fclose(f);
      if (got != gS->data[i].size()) gS->data[i].clear();
    }
    if (gS->data[i].size() < 4 || (memcmp(gS->data[i].data(), "OTTO", 4) != 0 && memcmp(gS->data[i].data(), "\0\1\0\0", 4) != 0)) {
      logf("[FONT] %s unreadable – Vietnamese font fix disabled", p.c_str());
      return;
    }
  }
  intptr_t slide = 0;
  bool found = false;
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const struct mach_header* h = _dyld_get_image_header(i);
    if (h && h->filetype == MH_EXECUTE) {
      slide = _dyld_get_image_vmaddr_slide(i);
      found = true;
      break;
    }
  }
  if (!found) {
    logf("[FONT] main executable not found");
    return;
  }
  // All three slots must match the analysed binary before anything is changed (all-or-nothing).
  const uint64_t slots[3] = {kSlotOpenRead, kSlotFileSize, kSlotOpenAsyncRead};
  const uint64_t fns[3] = {kFnOpenRead, kFnFileSize, kFnOpenAsyncRead};
  for (int k = 0; k < 3; k++) {
    void* cur = *(void**)(uintptr_t)(slots[k] + slide);
    if (cur != (void*)(uintptr_t)(fns[k] + slide)) {
      logf("[FONT] FPakPlatformFile vtable slot %d holds %p, expected %p – font fix disabled", k, cur,
           (void*)(uintptr_t)(fns[k] + slide));
      return;
    }
  }
  // The three slots are within 0x90 bytes; protect the page range covering them.
  vm_address_t lo = (vm_address_t)(kSlotFileSize + slide) & ~(vm_address_t)(vm_page_size - 1);
  vm_address_t hi = (vm_address_t)(kSlotOpenAsyncRead + slide + 8);
  vm_size_t len = hi - lo;
  if (vm_protect(mach_task_self(), lo, len, false, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
    logf("[FONT] vm_protect failed – font fix disabled");
    return;
  }
  patchSlot(slide, kSlotOpenRead, (void*)hookOpenRead, (void**)&gOrigOpenRead);
  patchSlot(slide, kSlotFileSize, (void*)hookFileSize, (void**)&gOrigFileSize);
  patchSlot(slide, kSlotOpenAsyncRead, (void*)hookOpenAsyncRead, (void**)&gOrigOpenAsyncRead);
  vm_protect(mach_task_self(), lo, len, false, VM_PROT_READ);
  logf("[FONT] installed: FPakPlatformFile OpenRead/FileSize/OpenAsyncRead serve %d UI fonts from memory (%s)", kNumFonts, fontDir);
}

}  // namespace zfont
