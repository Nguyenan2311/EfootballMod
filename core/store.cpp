#include "store.hpp"

#include <dirent.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "codec.hpp"

namespace zs {

// ---------------- logging ----------------
static void (*g_sink)(int, const char*, const char*) = nullptr;
static bool g_verbose = false;
void setLogSink(void (*sink)(int, const char*, const char*)) { g_sink = sink; }
void setVerbose(bool v) { g_verbose = v; }
static void emit(int lvl, const std::string& tag, const std::string& msg) {
  if (g_sink) { g_sink(lvl, tag.c_str(), msg.c_str()); return; }
  if (g_verbose) fprintf(stderr, "%c/%s: %s\n", "DIWE"[lvl & 3], tag.c_str(), msg.c_str());
}
void logi(const std::string& t, const std::string& m) { emit(1, t, m); }
void logw(const std::string& t, const std::string& m) { emit(2, t, m); }
void loge(const std::string& t, const std::string& m) { emit(3, t, m); }

// ---------------- assets ----------------
bool Assets::exists(const std::string& name) const {
  struct stat st;
  return stat(path(name).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool Assets::read(const std::string& name, std::string& out) const {
  std::ifstream f(path(name), std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}
std::vector<std::string> Assets::list(const std::string& dir) const {
  std::vector<std::string> r;
  DIR* d = opendir(path(dir).c_str());
  if (!d) return r;
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n != "." && n != "..") r.push_back(n);
  }
  closedir(d);
  std::sort(r.begin(), r.end());
  return r;
}

// ---------------- prefs ----------------
Prefs::Prefs(std::string f) : file(std::move(f)) {
  data = zj::Object::make();
  std::ifstream in(file, std::ios::binary);
  if (in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    try { data = zj::Object::parse(ss.str()); } catch (...) { data = zj::Object::make(); }
  }
}
zj::ObjP Prefs::entry(const std::string& k) const { return data->optJSONObject(k); }
std::string Prefs::getString(const std::string& k, const std::string& def) const {
  auto e = entry(k);
  if (!e || e->optString("t") != "s") return def;
  return e->optString("v");
}
bool Prefs::hasString(const std::string& k) const { auto e = entry(k); return e && e->optString("t") == "s"; }
int32_t Prefs::getInt(const std::string& k, int32_t def) const { auto e = entry(k); return (e && e->optString("t") == "i") ? e->optInt("v") : def; }
int64_t Prefs::getLong(const std::string& k, int64_t def) const { auto e = entry(k); return (e && e->optString("t") == "l") ? e->optLong("v") : def; }
bool Prefs::getBoolean(const std::string& k, bool def) const { auto e = entry(k); return (e && e->optString("t") == "b") ? e->optBoolean("v") : def; }

static zj::ObjP typed(const char* t, const zj::Value& v) { auto o = zj::Object::make(); o->put("t", t); o->put("v", v); return o; }
Prefs::Editor& Prefs::Editor::putString(const std::string& k, const std::string& v) { ch.emplace_back(k, zj::Value(typed("s", zj::Value(v)))); return *this; }
Prefs::Editor& Prefs::Editor::putInt(const std::string& k, int32_t v) { ch.emplace_back(k, zj::Value(typed("i", zj::Value((int)v)))); return *this; }
Prefs::Editor& Prefs::Editor::putLong(const std::string& k, int64_t v) { ch.emplace_back(k, zj::Value(typed("l", zj::Value((long long)v)))); return *this; }
Prefs::Editor& Prefs::Editor::putBoolean(const std::string& k, bool v) { ch.emplace_back(k, zj::Value(typed("b", zj::Value(v)))); return *this; }
Prefs::Editor& Prefs::Editor::remove(const std::string& k) { ch.emplace_back(k, zj::Value()); return *this; }
bool Prefs::Editor::commit() {
  if (clr) p->data = zj::Object::make();
  for (auto& c : ch) {
    if (c.second.isJavaNull()) p->data->remove(c.first);
    else p->data->put(c.first, c.second);
  }
  std::string tmp = p->file + ".tmp";
  std::string bytes = p->data->toString();
  FILE* out = fopen(tmp.c_str(), "wb");
  if (!out) throw std::runtime_error("Cannot open preferences: " + p->file);
  bool ok = fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
  if (fflush(out) != 0 || fsync(fileno(out)) != 0) ok = false;
  if (fclose(out) != 0) ok = false;
  if (!ok || std::rename(tmp.c_str(), p->file.c_str()) != 0)
    throw std::runtime_error("Cannot persist preferences: " + p->file);
  return true;
}

// ---------------- sqlite ----------------
ZiruDb::ZiruDb(const std::string& path) {
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    std::string error = sqlite3_errmsg(db);
    sqlite3_close(db); db = nullptr;
    throw std::runtime_error("Cannot open save DB: " + error);
  }
  sqlite3_busy_timeout(db, 5000);
  exec("PRAGMA synchronous=FULL");
  std::string ver;
  int cur = 0;
  if (queryOne("PRAGMA user_version", {}, ver)) cur = atoi(ver.c_str());
  if (cur < 4) {
    exec("CREATE TABLE IF NOT EXISTS squads (squad_id INTEGER PRIMARY KEY, squad_json TEXT NOT NULL, updated_at INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS players (serial INTEGER PRIMARY KEY, player_id INTEGER NOT NULL, player_json TEXT NOT NULL, updated_at INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS progression (serial INTEGER PRIMARY KEY, progression_json TEXT, updated_at INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT)");
    exec("CREATE TABLE IF NOT EXISTS league_matches (game_id INTEGER PRIMARY KEY, division INTEGER NOT NULL DEFAULT 8, result TEXT NOT NULL DEFAULT '', home_score INTEGER NOT NULL DEFAULT 0, away_score INTEGER NOT NULL DEFAULT 0, cpu_level TEXT NOT NULL DEFAULT '', played_at INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS league_history (storage_id INTEGER PRIMARY KEY, history_json TEXT NOT NULL, played_at INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS wc_tournament (tournament_id INTEGER PRIMARY KEY AUTOINCREMENT, created_at INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL DEFAULT 'NEW', stage TEXT NOT NULL DEFAULT 'GROUP', current_matchday INTEGER NOT NULL DEFAULT 1, current_fixture INTEGER NOT NULL DEFAULT 1, user_team_id INTEGER NOT NULL DEFAULT 0, champion_team_id INTEGER NOT NULL DEFAULT 0, runner_up_team_id INTEGER NOT NULL DEFAULT 0, third_place_team_id INTEGER NOT NULL DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS wc_team (tournament_id INTEGER NOT NULL, team_id INTEGER NOT NULL, national_team_id INTEGER NOT NULL DEFAULT 0, country_id INTEGER NOT NULL DEFAULT 0, group_code TEXT NOT NULL DEFAULT '', manager_id INTEGER NOT NULL DEFAULT 0, seed INTEGER NOT NULL DEFAULT 0, strength INTEGER NOT NULL DEFAULT 0, eliminated INTEGER NOT NULL DEFAULT 0, final_position INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(tournament_id,team_id))");
    exec("CREATE TABLE IF NOT EXISTS wc_squad (tournament_id INTEGER NOT NULL, team_id INTEGER NOT NULL, player_id INTEGER NOT NULL, card_category TEXT NOT NULL DEFAULT '', squad_role TEXT NOT NULL DEFAULT '', is_starting INTEGER NOT NULL DEFAULT 0, shirt_number INTEGER NOT NULL DEFAULT 0, position INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(tournament_id,team_id,player_id))");
    exec("CREATE TABLE IF NOT EXISTS wc_match (tournament_id INTEGER NOT NULL, match_id INTEGER NOT NULL, stage TEXT NOT NULL DEFAULT '', group_code TEXT NOT NULL DEFAULT '', round INTEGER NOT NULL DEFAULT 0, matchday INTEGER NOT NULL DEFAULT 0, home_team_id INTEGER NOT NULL DEFAULT 0, away_team_id INTEGER NOT NULL DEFAULT 0, home_score INTEGER NOT NULL DEFAULT -1, away_score INTEGER NOT NULL DEFAULT -1, extra_time INTEGER NOT NULL DEFAULT 0, penalty_home INTEGER NOT NULL DEFAULT -1, penalty_away INTEGER NOT NULL DEFAULT -1, winner_team_id INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL DEFAULT 'PENDING', is_user_match INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(tournament_id,match_id))");
    exec("CREATE TABLE IF NOT EXISTS wc_standing (tournament_id INTEGER NOT NULL, group_code TEXT NOT NULL, team_id INTEGER NOT NULL, played INTEGER NOT NULL DEFAULT 0, wins INTEGER NOT NULL DEFAULT 0, draws INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0, goals_for INTEGER NOT NULL DEFAULT 0, goals_against INTEGER NOT NULL DEFAULT 0, goal_difference INTEGER NOT NULL DEFAULT 0, points INTEGER NOT NULL DEFAULT 0, rank INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(tournament_id,group_code,team_id))");
    exec("CREATE TABLE IF NOT EXISTS wc_player_stat (tournament_id INTEGER NOT NULL, player_id INTEGER NOT NULL, team_id INTEGER NOT NULL DEFAULT 0, appearances INTEGER NOT NULL DEFAULT 0, starts INTEGER NOT NULL DEFAULT 0, minutes INTEGER NOT NULL DEFAULT 0, goals INTEGER NOT NULL DEFAULT 0, assists INTEGER NOT NULL DEFAULT 0, motm INTEGER NOT NULL DEFAULT 0, yellow_cards INTEGER NOT NULL DEFAULT 0, red_cards INTEGER NOT NULL DEFAULT 0, clean_sheets INTEGER NOT NULL DEFAULT 0, rating_total INTEGER NOT NULL DEFAULT 0, rating_count INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(tournament_id,player_id))");
    exec("CREATE TABLE IF NOT EXISTS wc_award (tournament_id INTEGER NOT NULL, award_type TEXT NOT NULL, entity_id INTEGER NOT NULL DEFAULT 0, value INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(tournament_id,award_type))");
    exec("CREATE TABLE IF NOT EXISTS wc_history (tournament_id INTEGER PRIMARY KEY, champion INTEGER NOT NULL DEFAULT 0, runner_up INTEGER NOT NULL DEFAULT 0, third_place INTEGER NOT NULL DEFAULT 0, golden_boot_player INTEGER NOT NULL DEFAULT 0, top_assist_player INTEGER NOT NULL DEFAULT 0, mvp_player INTEGER NOT NULL DEFAULT 0, best_goalkeeper INTEGER NOT NULL DEFAULT 0, date_finished INTEGER NOT NULL DEFAULT 0)");
    exec("PRAGMA user_version=4");
  }
}
ZiruDb::~ZiruDb() { if (db) sqlite3_close(db); }
void ZiruDb::exec(const std::string& sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
    std::string message = std::string("exec failed: ") + (err ? err : "?");
    loge("ZIRU_DB", message);
    sqlite3_free(err);
    throw std::runtime_error(message);
  }
}
bool ZiruDb::queryOne(const std::string& sql, const std::vector<std::string>& args, std::string& out) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("Save read prepare failed: ") + sqlite3_errmsg(db));
  for (size_t i = 0; i < args.size(); i++) sqlite3_bind_text(st, (int)i + 1, args[i].c_str(), -1, SQLITE_TRANSIENT);
  bool ok = false;
  int result = sqlite3_step(st);
  if (result == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(st, 0);
    if (t) { out.assign((const char*)t, sqlite3_column_bytes(st, 0)); ok = true; }
  }
  sqlite3_finalize(st);
  if (result != SQLITE_ROW && result != SQLITE_DONE)
    throw std::runtime_error(std::string("Save read failed: ") + sqlite3_errmsg(db));
  return ok;
}
static void bindAll(sqlite3_stmt* st, const std::vector<zj::Value>& a) {
  for (size_t i = 0; i < a.size(); i++) {
    const auto& v = a[i];
    if (v.t == zj::Value::STR) sqlite3_bind_text(st, (int)i + 1, v.s.c_str(), (int)v.s.size(), SQLITE_TRANSIENT);
    else if (v.t == zj::Value::INT || v.t == zj::Value::LONG) sqlite3_bind_int64(st, (int)i + 1, v.n);
    else sqlite3_bind_null(st, (int)i + 1);
  }
}
static bool run(sqlite3* db, const std::string& sql, const std::vector<zj::Value>& a) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
    throw std::runtime_error(std::string("Save prepare failed: ") + sqlite3_errmsg(db));
  }
  bindAll(st, a);
  int r = sqlite3_step(st);
  sqlite3_finalize(st);
  if (r != SQLITE_DONE) throw std::runtime_error(std::string("Save write failed: ") + sqlite3_errmsg(db));
  return true;
}
void ZiruDb::deletePlayerState(int64_t s) {
  run(db, "DELETE FROM players WHERE serial=?", {zj::Value(std::to_string(s))});
  run(db, "DELETE FROM progression WHERE serial=?", {zj::Value(std::to_string(s))});
}
std::string ZiruDb::loadAllPlayersJson() {
  sqlite3_stmt* st = nullptr;
  zj::ArrP arr = zj::Array::make();
  if (sqlite3_prepare_v2(db, "SELECT player_json FROM players ORDER BY serial", -1, &st, nullptr) == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW) {
      const unsigned char* t = sqlite3_column_text(st, 0);
      arr->put(zj::Object::parse(t ? std::string((const char*)t) : std::string()));
    }
    sqlite3_finalize(st);
  }
  return arr->toString();
}
std::string ZiruDb::loadLeagueHistory() {
  sqlite3_stmt* st = nullptr;
  std::string out = "[";
  bool any = false;
  if (sqlite3_prepare_v2(db, "SELECT history_json FROM league_history ORDER BY played_at DESC LIMIT 50", -1, &st, nullptr) != SQLITE_OK) return "[]";
  while (sqlite3_step(st) == SQLITE_ROW) {
    const unsigned char* t = sqlite3_column_text(st, 0);
    if (any) out += ",";
    if (t) { out += (const char*)t; any = true; }
  }
  sqlite3_finalize(st);
  out += "]";
  logi("ZIRU_DB", "[ZIRU_V63_HISTORY_DB_LOAD] bytes=" + std::to_string(out.size()));
  return out;
}
bool ZiruDb::loadMeta(const std::string& k, std::string& out) { return queryOne("SELECT v FROM meta WHERE k=?", {k}, out); }
bool ZiruDb::loadPlayer(int64_t s, std::string& out) { return queryOne("SELECT player_json FROM players WHERE serial=?", {std::to_string(s)}, out); }
bool ZiruDb::loadProgression(int64_t s, std::string& out) { return queryOne("SELECT progression_json FROM progression WHERE serial=?", {std::to_string(s)}, out); }
bool ZiruDb::loadSquad(int32_t i, std::string& out) { return queryOne("SELECT squad_json FROM squads WHERE squad_id=?", {std::to_string(i)}, out); }
void ZiruDb::saveLeagueHistory(int64_t id, const std::string& json) {
  run(db, "INSERT OR REPLACE INTO league_history(storage_id,history_json,played_at) VALUES(?,?,?)", {zj::Value((long long)id), zj::Value(json), zj::Value((long long)zc::currentTimeMillis())});
}
void ZiruDb::saveMatchState(const std::string& state, const std::string& k, const std::string& v) {
  exec("BEGIN");
  try {
    run(db, "INSERT OR REPLACE INTO meta(k,v) VALUES(?,?)", {zj::Value(k), zj::Value(v)});
    run(db, "INSERT OR REPLACE INTO meta(k,v) VALUES(?,?)", {zj::Value("z27_division_state"), zj::Value(state)});
    exec("COMMIT");
  } catch (...) {
    exec("ROLLBACK");
    throw;
  }
}
void ZiruDb::saveMeta(const std::string& k, const std::string& v) { run(db, "INSERT OR REPLACE INTO meta(k,v) VALUES(?,?)", {zj::Value(k), zj::Value(v)}); }
void ZiruDb::savePlayer(int64_t s, int64_t pid, const std::string& json) {
  run(db, "INSERT OR REPLACE INTO players(serial,player_id,player_json,updated_at) VALUES(?,?,?,?)", {zj::Value((long long)s), zj::Value((long long)pid), zj::Value(json), zj::Value((long long)zc::currentTimeMillis())});
}
void ZiruDb::saveProgression(int64_t s, const std::string& json) {
  run(db, "INSERT OR REPLACE INTO progression(serial,progression_json,updated_at) VALUES(?,?,?)", {zj::Value((long long)s), zj::Value(json), zj::Value((long long)zc::currentTimeMillis())});
}
void ZiruDb::saveSquad(int32_t id, const std::string& json) {
  run(db, "INSERT OR REPLACE INTO squads(squad_id,squad_json,updated_at) VALUES(?,?,?)", {zj::Value((int)id), zj::Value(json), zj::Value((long long)zc::currentTimeMillis())});
}
void ZiruDb::deleteSquad(int32_t id) {
  run(db, "DELETE FROM squads WHERE squad_id=?", {zj::Value((int)id)});
}

}  // namespace zs
