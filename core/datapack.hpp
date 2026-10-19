// Data pack (PES2027-data-<data_id>.zip) inspection, streaming install and activation.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zd {

struct FileRec {
  std::string path;    // relative, e.g. "pak/pc0000_mobile_ios.pak"
  uint64_t size = 0;
  std::string sha256;  // lowercase hex
};

struct Manifest {
  int format = 0;
  std::string dataId;
  std::string clientVersion;  // CFBundleShortVersionString the data was made for
  std::string clientBuild;    // CFBundleVersion
  std::string modVersion;     // minimum dylib/mod version required
  std::vector<FileRec> files;
  uint64_t totalBytes() const { uint64_t t = 0; for (auto& f : files) t += f.size; return t; }
};

struct Layout {
  std::string gameDataRoot;  // .../Library/Application Support/PES2027/GameData
  std::string documents;     // app Documents (symlink targets live here: pak, cpk)
};

struct Progress {
  std::string phase;     // "check", "extract", "verify", "activate"
  std::string file;
  uint64_t done = 0, total = 0;
};
using ProgressFn = std::function<bool(const Progress&)>;  // return false to cancel

bool parseManifest(const std::string& json, Manifest& m, std::string& err);
bool inspectPack(const std::string& zipPath, Manifest& m, std::string& err);
// Compatibility: same client version/build, format 1, safe paths, all entries present.
bool checkCompatible(const Manifest& m, const std::string& appVersion, const std::string& appBuild, const std::string& modVersion,
                     std::string& err);
bool installPack(const std::string& zipPath, const Layout& L, uint64_t freeBytes, const ProgressFn& cb, Manifest& out, std::string& err);
bool activate(const Layout& L, const std::string& dataId, std::string& err);
// Active data id (empty = none) and a quick structural check (all manifest files present with right size).
std::string activeId(const Layout& L);
bool quickVerify(const Layout& L, const std::string& dataId, std::string& err);
bool quickVerify(const Layout& L, const std::string& dataId, const std::string& appVersion, const std::string& appBuild, const std::string& modVersion, std::string& err);
bool loadInstalledManifest(const Layout& L, const std::string& dataId, Manifest& m, std::string& err);
std::string dataDir(const Layout& L, const std::string& dataId);

bool mkdirs(const std::string& path);
bool removeTree(const std::string& path);

}  // namespace zd
