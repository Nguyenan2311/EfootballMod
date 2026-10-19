#include "datapack.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "jvalue.hpp"
#include "zipread.hpp"

namespace zd {

static const char* kManifestName = "data-manifest.json";

bool mkdirs(const std::string& path) {
  std::string cur;
  for (size_t i = 0; i < path.size(); i++) {
    cur += path[i];
    if ((path[i] == '/' && i > 0) || i + 1 == path.size()) {
      std::string d = cur;
      if (d.size() > 1 && d.back() == '/') d.pop_back();
      if (mkdir(d.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
  }
  return true;
}

bool removeTree(const std::string& path) {
  struct stat st;
  if (lstat(path.c_str(), &st) != 0) return true;
  if (S_ISDIR(st.st_mode)) {
    DIR* d = opendir(path.c_str());
    if (d) {
      while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        removeTree(path + "/" + n);
      }
      closedir(d);
    }
    return rmdir(path.c_str()) == 0;
  }
  return unlink(path.c_str()) == 0;
}

static bool safeRel(const std::string& p) {
  if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos) return false;
  size_t s = 0;
  while (s <= p.size()) {
    size_t e = p.find('/', s);
    if (e == std::string::npos) e = p.size();
    std::string part = p.substr(s, e - s);
    if (part.empty() || part == "." || part == "..") return false;
    s = e + 1;
  }
  // Only the directories the game reads from the pack
  return p.rfind("pak/", 0) == 0 || p.rfind("cpk/", 0) == 0 || p.rfind("livedata/", 0) == 0 || p.rfind("download/", 0) == 0;
}

bool parseManifest(const std::string& json, Manifest& m, std::string& err) {
  try {
    zj::ObjP o = zj::Object::parse(json);
    m.format = o->optInt("format", 0);
    m.dataId = o->optString("data_id");
    m.clientVersion = o->optString("client_version");
    m.clientBuild = o->optString("client_build");
    m.modVersion = o->optString("mod_version");
    zj::ArrP fl = o->optJSONArray("files");
    if (!fl) { err = "Manifest thiếu danh sách files"; return false; }
    m.files.clear();
    for (int i = 0; i < fl->length(); i++) {
      zj::ObjP f = fl->getJSONObject(i);
      FileRec r;
      r.path = f->getString("path");
      r.size = (uint64_t)f->getLong("size");
      r.sha256 = f->getString("sha256");
      m.files.push_back(r);
    }
    return true;
  } catch (const std::exception& e) {
    err = std::string("Manifest không hợp lệ: ") + e.what();
    return false;
  }
}

bool inspectPack(const std::string& zipPath, Manifest& m, std::string& err) {
  zz::Reader z;
  if (!z.open(zipPath, err)) return false;
  const zz::Entry* me = z.find(kManifestName);
  if (!me) { err = "Gói không có data-manifest.json"; return false; }
  std::string js;
  if (!z.readAll(*me, js, err)) return false;
  if (!parseManifest(js, m, err)) return false;
  for (auto& f : m.files) {
    const zz::Entry* e = z.find(f.path);
    if (!e) { err = "Gói thiếu file: " + f.path; return false; }
    if (e->size != f.size) { err = "Sai dung lượng trong ZIP: " + f.path; return false; }
  }
  return true;
}

static int compareSemver(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    while (i < a.size() && (a[i] == 'v' || a[i] == 'V')) i++;
    while (j < b.size() && (b[j] == 'v' || b[j] == 'V')) j++;
    long long numA = 0, numB = 0;
    while (i < a.size() && a[i] >= '0' && a[i] <= '9') {
      numA = numA * 10 + (a[i] - '0');
      i++;
    }
    while (j < b.size() && b[j] >= '0' && b[j] <= '9') {
      numB = numB * 10 + (b[j] - '0');
      j++;
    }
    if (numA < numB) return -1;
    if (numA > numB) return 1;
    if (i < a.size() && (a[i] == '.' || a[i] == '-')) i++;
    if (j < b.size() && (b[j] == '.' || b[j] == '-')) j++;
  }
  return 0;
}

bool checkCompatible(const Manifest& m, const std::string& appVersion, const std::string& appBuild, const std::string& modVersion,
                     std::string& err) {
  if (m.format != 1) { err = "Định dạng gói data không hỗ trợ (format " + std::to_string(m.format) + ")"; return false; }
  if (m.dataId.empty() || m.dataId.find('/') != std::string::npos || m.dataId[0] == '.') { err = "data_id không hợp lệ"; return false; }
  if (!appVersion.empty() && m.clientVersion != appVersion) { err = "Gói data dành cho bản " + m.clientVersion + ", ứng dụng là " + appVersion; return false; }
  if (!appBuild.empty() && !m.clientBuild.empty() && m.clientBuild != appBuild) { err = "Gói data dành cho build " + m.clientBuild + ", ứng dụng là " + appBuild; return false; }
  if (!m.modVersion.empty() && !modVersion.empty() && compareSemver(m.modVersion, modVersion) > 0) { err = "Gói data cần bản mod " + m.modVersion + " trở lên"; return false; }
  std::set<std::string> seen;
  bool havePak = false, haveCpk = false;
  for (auto& f : m.files) {
    if (!safeRel(f.path)) { err = "Đường dẫn không an toàn trong manifest: " + f.path; return false; }
    if (!seen.insert(f.path).second) { err = "Trùng file trong manifest: " + f.path; return false; }
    if (f.sha256.size() != 64) { err = "SHA-256 không hợp lệ: " + f.path; return false; }
    if (f.path.rfind("pak/", 0) == 0) havePak = true;
    if (f.path.rfind("cpk/", 0) == 0) haveCpk = true;
  }
  if (!havePak || !haveCpk) { err = "Gói data thiếu thư mục pak/ hoặc cpk/"; return false; }
  return true;
}

std::string dataDir(const Layout& L, const std::string& id) { return L.gameDataRoot + "/" + id; }

static bool writeFile(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp";
  {
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o << data;
    if (!o.good()) return false;
  }
  return rename(tmp.c_str(), path.c_str()) == 0;
}
static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool installPack(const std::string& zipPath, const Layout& L, uint64_t freeBytes, const ProgressFn& cb, Manifest& m, std::string& err) {
  Progress pr;
  pr.phase = "check";
  if (cb && !cb(pr)) { err = "Đã hủy"; return false; }
  zz::Reader z;
  if (!z.open(zipPath, err)) return false;
  const zz::Entry* me = z.find(kManifestName);
  if (!me) { err = "Gói không có data-manifest.json"; return false; }
  std::string js;
  if (!z.readAll(*me, js, err) || !parseManifest(js, m, err)) return false;
  uint64_t total = m.totalBytes();
  const uint64_t margin = 512ull << 20;  // keep 512 MB free for the game itself
  if (freeBytes && freeBytes < total + margin) {
    err = "Không đủ dung lượng trống: cần " + std::to_string((total + margin) >> 20) + " MB, còn " + std::to_string(freeBytes >> 20) + " MB";
    return false;
  }
  if (!mkdirs(L.gameDataRoot)) { err = "Không tạo được thư mục GameData"; return false; }
  std::string staging = L.gameDataRoot + "/.staging-" + m.dataId;
  removeTree(staging);
  if (!mkdirs(staging)) { err = "Không tạo được thư mục tạm"; return false; }
  pr.phase = "extract";
  pr.total = total;
  uint64_t base = 0;
  for (auto& f : m.files) {
    const zz::Entry* e = z.find(f.path);
    if (!e || e->size != f.size) { removeTree(staging); err = "Gói thiếu hoặc sai file: " + f.path; return false; }
    std::string dst = staging + "/" + f.path;
    mkdirs(dst.substr(0, dst.rfind('/')));
    pr.file = f.path;
    std::string sha;
    bool ok = z.extract(*e, dst, sha, err, [&](uint64_t d) {
      pr.done = base + d;
      return cb ? cb(pr) : true;
    });
    if (!ok) { removeTree(staging); return false; }
    if (sha != f.sha256) { removeTree(staging); err = "Sai SHA-256: " + f.path; return false; }
    base += f.size;
  }
  if (!writeFile(staging + "/" + kManifestName, js)) { removeTree(staging); err = "Không ghi được manifest"; return false; }
  pr.phase = "activate";
  if (cb) cb(pr);
  std::string final = dataDir(L, m.dataId);
  removeTree(final);
  if (rename(staging.c_str(), final.c_str()) != 0) { removeTree(staging); err = "Không kích hoạt được thư mục data"; return false; }
  if (!activate(L, m.dataId, err)) return false;
  // Remove other installed packs (keep save data elsewhere untouched)
  DIR* d = opendir(L.gameDataRoot.c_str());
  if (d) {
    std::vector<std::string> old;
    while (dirent* de = readdir(d)) {
      std::string n = de->d_name;
      if (n == "." || n == ".." || n == m.dataId || n == "active.json") continue;
      old.push_back(n);
    }
    closedir(d);
    for (auto& n : old) removeTree(L.gameDataRoot + "/" + n);
  }
  return true;
}

static bool relink(const std::string& link, const std::string& target, std::string& err) {
  struct stat st;
  if (lstat(link.c_str(), &st) == 0) {
    if (S_ISLNK(st.st_mode)) unlink(link.c_str());
    else if (S_ISDIR(st.st_mode)) {
      // a real directory left by an older build: move it aside instead of deleting user files
      std::string aside = link + ".old";
      removeTree(aside);
      rename(link.c_str(), aside.c_str());
    } else unlink(link.c_str());
  }
  if (symlink(target.c_str(), link.c_str()) != 0) { err = "Không tạo được liên kết " + link; return false; }
  return true;
}

// Files shipped under download/ (e.g. commentary dt510_eng_mobile_all.cpk) belong in the game's
// download path (basic::PathGet type 5 = Documents/Download/); link each file individually so the
// game can keep writing its own downloads (livedata) into the real directory.
static bool linkDownloads(const Layout& L, const std::string& id, std::string& err) {
  Manifest m;
  if (!loadInstalledManifest(L, id, m, err)) return false;
  std::string dl = L.documents + "/Download";
  mkdirs(dl);
  char buf[4096];
  for (auto& f : m.files) {
    if (f.path.rfind("download/", 0) != 0) continue;
    std::string name = f.path.substr(9);
    if (name.find('/') != std::string::npos) continue;
    std::string link = dl + "/" + name, want = dataDir(L, id) + "/" + f.path;
    ssize_t r = readlink(link.c_str(), buf, sizeof buf - 1);
    if (r > 0 && std::string(buf, (size_t)r) == want) continue;
    if (!relink(link, want, err)) return false;
  }
  return true;
}

bool activate(const Layout& L, const std::string& id, std::string& err) {
  std::string dir = dataDir(L, id);
  if (!relink(L.documents + "/pak", dir + "/pak", err)) return false;
  if (!relink(L.documents + "/cpk", dir + "/cpk", err)) return false;
  if (!linkDownloads(L, id, err)) return false;
  std::string a = "{\"data_id\":" + zj::quote(id) + "}";
  if (!writeFile(L.gameDataRoot + "/active.json", a)) { err = "Không ghi được active.json"; return false; }
  return true;
}

std::string activeId(const Layout& L) {
  std::string s;
  if (!readFile(L.gameDataRoot + "/active.json", s)) return "";
  try { return zj::Object::parse(s)->optString("data_id"); } catch (...) { return ""; }
}

bool loadInstalledManifest(const Layout& L, const std::string& id, Manifest& m, std::string& err) {
  std::string js;
  if (!readFile(dataDir(L, id) + "/" + kManifestName, js)) { err = "Không thấy manifest đã cài"; return false; }
  return parseManifest(js, m, err);
}

bool quickVerify(const Layout& L, const std::string& id, const std::string& appVersion, const std::string& appBuild, const std::string& modVersion, std::string& err) {
  if (id.empty()) { err = "Chưa nhập data"; return false; }
  Manifest m;
  if (!loadInstalledManifest(L, id, m, err)) return false;
  if (!checkCompatible(m, appVersion, appBuild, modVersion, err)) return false;
  for (auto& f : m.files) {
    struct stat st;
    std::string p = dataDir(L, id) + "/" + f.path;
    if (stat(p.c_str(), &st) != 0 || (uint64_t)st.st_size != f.size) { err = "Data thiếu/sai: " + f.path; return false; }
  }
  // links must point into this data set
  char buf[4096];
  for (const char* n : {"pak", "cpk"}) {
    std::string link = L.documents + "/" + n;
    ssize_t r = readlink(link.c_str(), buf, sizeof buf - 1);
    std::string want = dataDir(L, id) + "/" + n;
    if (r <= 0 || std::string(buf, (size_t)r) != want) {
      std::string e2;
      if (!relink(link, want, e2)) { err = e2; return false; }
    }
  }
  return linkDownloads(L, id, err);
}

bool quickVerify(const Layout& L, const std::string& id, std::string& err) {
  return quickVerify(L, id, "", "", "", err);
}

}  // namespace zd
