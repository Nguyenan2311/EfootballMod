#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "jvalue.hpp"

#include <sys/stat.h>

struct sqlite3;

namespace zs {

// android.content.res.AssetManager over a directory (responses/, livedata/, ...)
struct Assets {
  std::string root;
  std::string livedataRoot;  // optional: "livedata/<x>" -> livedataRoot + "/<x>"
  bool exists(const std::string& name) const;
  bool read(const std::string& name, std::string& out) const;  // false = IOException
  std::vector<std::string> list(const std::string& dir) const;
  std::string path(const std::string& name) const {
    if (!livedataRoot.empty() && name.rfind("livedata/", 0) == 0) {
      std::string p = livedataRoot + "/" + name.substr(9);
      struct stat st;
      if (stat(p.c_str(), &st) == 0) return p;
      size_t pos = p.find("_mobile_ios");
      if (pos != std::string::npos) {
        std::string fallback = p;
        fallback.replace(pos, 11, "_mobile_and");
        if (stat(fallback.c_str(), &st) == 0) return fallback;
      }
      return p;
    }
    if (!livedataRoot.empty() && name == "livedata") return livedataRoot;
    return root + "/" + name;
  }
};

// SharedPreferences (typed values, persisted as JSON on commit)
struct Prefs {
  std::string file;
  zj::ObjP data;  // {"key": {"t":"s|i|l|b", "v": ...}}
  explicit Prefs(std::string f);
  std::string getString(const std::string& k, const std::string& def) const;
  bool hasString(const std::string& k) const;
  int32_t getInt(const std::string& k, int32_t def) const;
  int64_t getLong(const std::string& k, int64_t def) const;
  bool getBoolean(const std::string& k, bool def) const;
  struct Editor {
    Prefs* p;
    std::vector<std::pair<std::string, zj::Value>> ch;  // Java null = remove
    bool clr = false;
    Editor& putString(const std::string& k, const std::string& v);
    Editor& putInt(const std::string& k, int32_t v);
    Editor& putLong(const std::string& k, int64_t v);
    Editor& putBoolean(const std::string& k, bool v);
    Editor& remove(const std::string& k);
    Editor& clear() { clr = true; return *this; }
    bool commit();
    void apply() { commit(); }
  };
  Editor edit() { return Editor{this, {}, false}; }
 private:
  zj::ObjP entry(const std::string& k) const;
};

// com.offline.server.ZiruDb (ziru_save.db, schema version 4)
struct ZiruDb {
  sqlite3* db = nullptr;
  explicit ZiruDb(const std::string& path);
  ~ZiruDb();
  void deletePlayerState(int64_t serial);
  std::string loadAllPlayersJson();
  std::string loadLeagueHistory();
  bool loadMeta(const std::string& k, std::string& out);   // false = Java null
  bool loadPlayer(int64_t serial, std::string& out);
  bool loadProgression(int64_t serial, std::string& out);
  bool loadSquad(int32_t id, std::string& out);
  void saveLeagueHistory(int64_t storageId, const std::string& json);
  void saveMatchState(const std::string& state, const std::string& k, const std::string& v);
  void saveMeta(const std::string& k, const std::string& v);
  void savePlayer(int64_t serial, int64_t playerId, const std::string& json);
  void saveProgression(int64_t serial, const std::string& json);
  void saveSquad(int32_t id, const std::string& json);
  void deleteSquad(int32_t id);
 private:
  void exec(const std::string& sql);
  bool queryOne(const std::string& sql, const std::vector<std::string>& args, std::string& out);
};

// Log + offline_server.log equivalent
void logi(const std::string& tag, const std::string& msg);
void logw(const std::string& tag, const std::string& msg);
void loge(const std::string& tag, const std::string& msg);
void setLogSink(void (*sink)(int level, const char* tag, const char* msg));
void setVerbose(bool v);

}  // namespace zs
