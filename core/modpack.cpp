#include "modpack.hpp"
#include "datapack.hpp"
#include "jvalue.hpp"
#include "zipread.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace zm {
using namespace zj;
static bool safePath(const std::string& p) {
  if (p.empty() || p.front() == '/' || p.find_first_of("\\:") != std::string::npos) return false;
  size_t a = 0;
  while (a < p.size()) {
    size_t b = p.find('/', a); if (b == std::string::npos) b = p.size();
    auto s = p.substr(a, b - a);
    if (s.empty() || s == "." || s == "..") return false;
    a = b + 1;
  }
  return true;
}
bool stadiumName(const std::string& n) {
  return n.size() > 6 && n.compare(0, 4, "pc30") == 0 &&
      n.compare(0, 6, "pc3001") != 0 && n.compare(0, 6, "pc3011") != 0 &&
      n.find_first_not_of("0123456789", 3) == 6 && n[6] == '_';
}
static std::string destination(const std::string& source) {
  auto slash = source.rfind('/');
  std::string name = source.substr(slash == std::string::npos ? 0 : slash + 1);
  if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos) return "";
  auto dot = name.rfind('.'); if (dot == std::string::npos) return "";
  std::string ext = name.substr(dot);
  if (ext != ".pak" && ext != ".utoc" && ext != ".ucas" && ext != ".cpk") return "";
  if (stadiumName(name) || name.rfind("dt510", 0) == 0) return "Download/" + name;
  if (name.rfind("dt870", 0) == 0 || source.find("livedata/") != std::string::npos) return "livedata/" + name;
  if (source.find("Download/") != std::string::npos || source.find("download/") != std::string::npos) return "Download/" + name;
  return (ext == ".cpk" ? "cpk/" : "pak/") + name;
}
static bool targetSafe(const std::string& p) {
  return safePath(p) && destination(p) == p;
}
static std::vector<std::string> subdirs(const std::string& p) {
  std::vector<std::string> out;
  DIR* d = opendir(p.c_str()); if (!d) return out;
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name; struct stat st;
    if (n.empty() || n[0] == '.') continue;
    if (lstat((p + "/" + n).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) out.push_back(n);
  }
  closedir(d); std::sort(out.begin(), out.end()); return out;
}
bool stageZip(const std::string& path, const std::string& pending, uint64_t freeBytes,
              std::string& summary, std::string& err) {
  zz::Reader zip; if (!zip.open(path, err)) return false;
  std::vector<std::pair<const zz::Entry*, std::string>> files;
  std::set<std::string> targets;
  uint64_t total = 0;
  for (const auto& e : zip.entries()) {
    if (!safePath(e.name)) { err = "Duong dan ZIP khong hop le: " + e.name; return false; }
    if (e.isDir()) continue;
    auto target = destination(e.name);
    if (target.empty()) continue; // README, manifests, screenshots are not game resources.
    if (!targets.insert(target).second) { err = "ZIP trung ten tep: " + target; return false; }
    if (!e.size || e.size > UINT64_MAX - total) { err = "Kich thuoc tep khong hop le"; return false; }
    total += e.size; files.push_back({&e, target});
  }
  if (files.empty()) { err = "ZIP khong co file .pak/.ucas/.utoc/.cpk"; return false; }
  for (const auto& f : files) {
    auto target = f.second; auto dot = target.rfind('.'); auto ext = target.substr(dot);
    auto stem = target.substr(0, dot);
    if (ext == ".ucas" || ext == ".utoc" || stadiumName(target.substr(target.rfind('/') + 1))) {
      for (const char* part : {".pak", ".ucas", ".utoc"}) if (!targets.count(stem + part)) {
        err = "Bo mod thieu tep di kem: " + stem + part; return false;
      }
    }
  }
  if (freeBytes < total || freeBytes - total < 64 * 1024 * 1024) { err = "Khong du dung luong de giai nen mod"; return false; }
  if (!zd::mkdirs(pending)) { err = "Khong tao duoc thu muc mod"; return false; }
  std::string pattern = pending + "/.extract-XXXXXX";
  std::vector<char> buf(pattern.begin(), pattern.end()); buf.push_back(0);
  char* temp = mkdtemp(buf.data()); if (!temp) { err = "Khong tao duoc vung tam"; return false; }
  std::string stage(temp); auto manifest = Object::make(); auto entries = Array::make();
  for (const auto& f : files) {
    std::string dst = stage + "/" + f.second, hash;
    if (!zd::mkdirs(dst.substr(0, dst.rfind('/'))) || !zip.extract(*f.first, dst, hash, err)) {
      zd::removeTree(stage); return false;
    }
    entries->put(Object::make()->put("path", f.second)->put("size", (long long)f.first->size)->put("sha256", hash));
  }
  manifest->put("files", entries);
  std::ofstream meta(stage + "/manifest.json"); meta << manifest->toString(); meta.close();
  if (!meta) { err = "Khong ghi duoc danh sach mod"; zd::removeTree(stage); return false; }
  // Monotonic sortable batch IDs, made unique by mkdtemp's suffix.
  std::string batch = std::to_string((long long)time(nullptr)) + "-" + stage.substr(stage.size() - 6);
  if (rename(stage.c_str(), (pending + "/" + batch).c_str()) != 0) { err = "Khong luu duoc mod"; zd::removeTree(stage); return false; }
  summary = std::to_string(files.size()) + " tep da san sang. Dong han game va mo lai de ap dung.";
  return true;
}
bool activatePending(const std::string& pending, const std::string& docs, std::string& summary, std::string& err) {
  int installed = 0;
  for (const auto& batch : subdirs(pending)) {
    std::string stage = pending + "/" + batch;
    ObjP m;
    try { std::ifstream in(stage + "/manifest.json"); std::string text((std::istreambuf_iterator<char>(in)), {}); m = Object::parse(text); }
    catch (...) { err = "Khong doc duoc manifest mod " + batch; return false; }
    auto files = m->optJSONArray("files"); if (!files || !files->length()) { err = "Mod rong"; return false; }
    for (int i = 0; i < files->length(); ++i) if (!targetSafe(files->getJSONObject(i)->getString("path"))) {
      err = "Duong dan mod khong hop le"; return false;
    }
    std::string backup = docs + "/ModBackups/" + batch;
    struct stat journalStat;
    if (lstat((stage + "/applying").c_str(), &journalStat) == 0) {
      // Recover a process termination between two renames, before retrying.
      for (int i = files->length() - 1; i >= 0; --i) {
        std::string rel = files->getJSONObject(i)->getString("path"); struct stat st;
        std::string src = stage + "/" + rel, dst = docs + "/" + rel, old = backup + "/" + rel;
        if (lstat(src.c_str(), &st) != 0 && lstat(dst.c_str(), &st) == 0 && rename(dst.c_str(), src.c_str()) != 0) {
          err = "Khong khoi phuc duoc mod dang do: " + rel; return false;
        }
        if (lstat(old.c_str(), &st) == 0 && rename(old.c_str(), dst.c_str()) != 0) {
          err = "Khong khoi phuc duoc ban cu: " + rel; return false;
        }
      }
      if (unlink((stage + "/applying").c_str()) != 0) { err = "Khong cap nhat duoc trang thai mod"; return false; }
    }
    // Validate every staged resource before replacing anything. A previous failed
    // activation keeps its resources in the staging folder for a later retry.
    for (int i = 0; i < files->length(); ++i) {
      auto f = files->getJSONObject(i); std::string rel = f->getString("path"); struct stat st;
      if (!targetSafe(rel) || lstat((stage + "/" + rel).c_str(), &st) || !S_ISREG(st.st_mode) ||
          st.st_size != f->getLong("size")) { err = "Tep mod bi thieu/hong: " + rel; return false; }
    }
    { std::ofstream journal(stage + "/applying"); journal << "1\n"; journal.close();
      if (!journal) { err = "Khong ghi duoc trang thai mod"; return false; } }
    std::vector<std::pair<std::string, bool>> done;
    bool ok = true;
    for (int i = 0; i < files->length(); ++i) {
      std::string rel = files->getJSONObject(i)->getString("path"), dst = docs + "/" + rel, old = backup + "/" + rel;
      if (!zd::mkdirs(dst.substr(0, dst.rfind('/'))) || !zd::mkdirs(old.substr(0, old.rfind('/')))) { ok = false; break; }
      struct stat st; bool exists = lstat(dst.c_str(), &st) == 0;
      if (exists && (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode))) { ok = false; break; }
      // Preserve the old link/file, including a custom user's existing mod.
      if (exists && rename(dst.c_str(), old.c_str()) != 0) { ok = false; break; }
      if (rename((stage + "/" + rel).c_str(), dst.c_str()) != 0) {
        if (exists) rename(old.c_str(), dst.c_str()); ok = false; break;
      }
      done.push_back({rel, exists});
    }
    if (!ok) {
      bool recovered = true;
      for (auto i = done.rbegin(); i != done.rend(); ++i) {
        if (rename((docs + "/" + i->first).c_str(), (stage + "/" + i->first).c_str())) recovered = false;
        if (i->second && rename((backup + "/" + i->first).c_str(), (docs + "/" + i->first).c_str())) recovered = false;
      }
      if (recovered) unlink((stage + "/applying").c_str());
      err = "Khong kich hoat duoc mod " + batch + (recovered ? "; da khoi phuc cac tep cu" : "; se thu khoi phuc khi mo lai game"); return false;
    }
    std::ifstream in(stage + "/manifest.json"); std::ofstream out(backup + "/manifest.json"); out << in.rdbuf(); out.close();
    // Commit by hiding the entire batch atomically. A crash during cleanup
    // must not leave a visible batch with a half-deleted recovery journal.
    if (rename(stage.c_str(), (pending + "/.done-" + batch).c_str()) != 0) {
      err = "Chua ghi xong trang thai mod; se khoi phuc va thu lai khi mo game"; return false;
    }
    installed += (int)done.size(); zd::removeTree(pending + "/.done-" + batch);
  }
  summary = std::to_string(installed) + " tep mod da kich hoat";
  return true;
}
}
