// ---- MyLeague as a Champions League season ----
// 32 clubs drawn into 8 groups of 4 (home and away, fixtures 1..6), group winners and runners-up to a
// two-legged round of 16, quarter-finals and semi-finals, then a one-match final (fixture 13).
// The season lives in meta "ziru_ucl" and is served through the MyLeague schedule: the user sees only
// their own group and their own ties; every other match is simulated with the MyLeague score formula.
// MyLeague matches have no extra time, so a level tie or final is settled by a simulated shoot-out.

// Seeding pots: native dt870 team ids (Real Madrid = "Madrid Chamartin B", Juventus = "Piemonte BN", ...)
static constexpr int32_t kUclPots[4][8] = {
    {109, 108, 173, 114, 103, 119, 101, 172},  // Real Madrid, Barcelona, Man City, PSG, Liverpool, Inter, Arsenal, Atletico
    {100, 102, 121, 120, 327, 179, 191, 192},  // Man Utd, Chelsea, Milan, Juventus, Napoli, Tottenham, Benfica, Porto
    {234, 125, 122, 193, 118, 116, 267, 106},  // Atalanta, Roma, Lazio, Sporting, PSV, Ajax, Villarreal, Newcastle
    {117, 113, 112, 130, 197, 131, 269, 107}};  // Feyenoord, Marseille, Monaco, Galatasaray, Fenerbahce, Celtic, Brugge, Aston Villa
// One group's double round robin: matchdays 1-3, then 4-6 with home and away swapped
static constexpr int kUclRound[3][2][2] = {{{0, 1}, {2, 3}}, {{2, 0}, {1, 3}}, {{0, 3}, {1, 2}}};

ObjP uclState() {
  JStr m = loadMeta("ziru_ucl");
  return m && !m->empty() ? Object::parse(*m) : nullptr;
}

void uclSave(const ObjP& st) { ziruDb->saveMeta("ziru_ucl", st->toString()); }

// The MyLeague formula for a match the user does not play (same as the league simulation)
void uclSimScore(int32_t f, int32_t h, int32_t a, int& hs, int& as) {
  hs = ((h + a) + f) % 4;
  as = (((h * 3) + (a * 2)) + f) % 4;
}

// Goals of match (h, a) at fixture f: the user's from the stored result, the others simulated
void uclScore(int32_t f, int32_t h, int32_t a, int& hs, int& as) {
  int user = ziruLeagueLoadInt("ziru_myleague_team_id", 100);
  JStr sh, sa;
  if ((h == user || a == user) && (sh = loadMeta(ziruMyLeagueFixtureKeyV74(f, "home"))) &&
      (sa = loadMeta(ziruMyLeagueFixtureKeyV74(f, "away")))) {
    hs = parseInt(*sh);
    as = parseInt(*sa);
    return;
  }
  uclSimScore(f, h, a, hs, as);
}

// Shoot-out winner of a level tie or final
int32_t uclShootout(int32_t f, int32_t x, int32_t y) { return ((x * 7) + (y * 3) + f) % 2 == 0 ? x : y; }

ArrP uclMatch(int32_t h, int32_t a) { return newArr()->put(h)->put(a); }

ArrP uclGroupMatchday(const ArrP& g, int md) {  // md 0..5
  ArrP out = newArr();
  for (const auto& p : kUclRound[md % 3]) {
    int32_t h = g->getInt(p[0]), a = g->getInt(p[1]);
    out->put(md < 3 ? uclMatch(h, a) : uclMatch(a, h));
  }
  return out;
}

// Group final order: points, goal difference, goals, team id
std::vector<int32_t> uclGroupOrder(const ArrP& g) {
  std::map<int32_t, std::array<int, 3>> t;  // pts, diff, goals
  for (int i = 0; i < 4; i++) t[g->getInt(i)] = {0, 0, 0};
  for (int md = 0; md < 6; md++) {
    ArrP ms = uclGroupMatchday(g, md);
    for (int i = 0; i < ms->length(); i++) {
      int32_t h = ms->getJSONArray(i)->getInt(0), a = ms->getJSONArray(i)->getInt(1);
      int hs, as;
      uclScore(md + 1, h, a, hs, as);
      t[h][0] += hs > as ? 3 : hs == as ? 1 : 0;
      t[a][0] += as > hs ? 3 : hs == as ? 1 : 0;
      t[h][1] += hs - as; t[a][1] += as - hs;
      t[h][2] += hs; t[a][2] += as;
    }
  }
  std::vector<int32_t> order;
  for (auto& [id, v] : t) order.push_back(id);
  std::sort(order.begin(), order.end(), [&](int32_t x, int32_t y) { return t[x] != t[y] ? t[x] > t[y] : x < y; });
  return order;
}

void uclNewSeason(int32_t user) {
  std::vector<std::vector<int32_t>> pots(4);
  bool listed = false;
  for (int p = 0; p < 4; p++)
    for (int32_t id : kUclPots[p]) { pots[p].push_back(id); listed |= id == user; }
  if (!listed) pots[3].back() = user;  // the user's club takes the last pot-4 place
  std::map<int32_t, int32_t> league;
  for (auto& pot : pots) for (int32_t id : pot) league[id] = ziruMyLeagueInferCompetitionV80(id);
  std::mt19937 rng((uint32_t)zc::currentTimeMillis());
  // Draw one club per pot into each group, avoiding two clubs of one league in a group where possible
  std::vector<std::vector<int32_t>> best;
  int bestClash = 1 << 30;
  for (int attempt = 0; attempt < 400 && bestClash > 0; attempt++) {
    std::vector<std::vector<int32_t>> groups(8);
    for (auto& pot : pots) {
      std::shuffle(pot.begin(), pot.end(), rng);
      for (int g = 0; g < 8; g++) groups[g].push_back(pot[g]);
    }
    int clash = 0;
    for (auto& g : groups)
      for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++)
          clash += league[g[i]] == league[g[j]];
    if (clash < bestClash) { bestClash = clash; best = groups; }
  }
  ObjP st = newObj();
  ArrP groups = newArr();
  int userGroup = 0;
  for (int g = 0; g < 8; g++) {
    ArrP ga = newArr();
    for (int32_t id : best[g]) { ga->put(id); if (id == user) userGroup = g; }
    groups->put(ga);
  }
  st->put("groups", groups)->put("user_group", userGroup)->put("ended", "")->put("ties", newArr());
  ArrP fixtures = newArr();
  for (int md = 0; md < 6; md++)
    fixtures->put(newObj()->put("stage", "GROUP")->put("matches", uclGroupMatchday(groups->getJSONArray(userGroup), md)));
  st->put("fixtures", fixtures);
  uclSave(st);
  for (int g = 0; g < 8; g++)
    for (int i = 0; i < 4; i++)
      for (const char* k : {"p", "w", "d", "l", "gf", "ga", "pts"}) ziruMlSaveTeamStatV83(groups->getJSONArray(g)->getInt(i), k, 0);
  zs::logi("OFFLINE_SERVER", "[ZIRU_UCL_DRAW] user=" + str(user) + " group=" + str(userGroup) + " " + groups->toString());
}

// Fresh season for the user's club: what CMD_SET_SEASON_ENTER does on a team change, plus the UCL draw
void uclStartSeason(int32_t user) {
  ziruLeagueSaveInt("ziru_myleague_fixture", 1);
  for (const char* k : {"points", "wins", "draws", "losses", "gf", "ga"}) ziruLeagueSaveInt(std::string("ziru_myleague_table_") + k, 0);
  ziruDb->saveMeta("ziru_myleague_history_json", "[]");
  ziruDb->saveMeta("ziru_myleague_last_result_rqid", "");
  ziruDb->saveMeta("ziru_myleague_last_result", "");
  ziruLeagueSaveInt("ziru_myleague_last_added_point", 0);
  for (int f = 1; f <= 13; f++)
    for (const char* k : {"result", "home", "away"}) ziruDb->saveMeta(ziruMyLeagueFixtureKeyV74(f, k), "");
  ziruDb->saveMeta("ziru_ucl_end_seen", "");
  ziruMlResetPlayerStatsV85();
  uclNewSeason(user);
}

// The season of the selected club; a MyLeague save from before the UCL format starts one
ObjP uclEnsure() {
  ObjP st = uclState();
  if (!st) {
    uclStartSeason(ziruLeagueLoadInt("ziru_myleague_team_id", 100));
    st = uclState();
  }
  return st;
}

ObjP uclSchedule() {
  ObjP st = uclEnsure();
  ArrP fx = st->getJSONArray("fixtures"), list = newArr();
  for (int i = 0; i < fx->length(); i++) {
    ArrP ms = fx->getJSONObject(i)->getJSONArray("matches"), ml = newArr();
    for (int j = 0; j < ms->length(); j++)
      ml->put(newObj()->put("home_team_id", ms->getJSONArray(j)->getInt(0))->put("home_goals", 0)
                  ->put("away_team_id", ms->getJSONArray(j)->getInt(1))->put("away_goals", 0));
    list->put(newObj()->put("fixture", i + 1)->put("is_boss_match", "NO")->put("match_list", ml));
  }
  return newObj()->put("result", "NOERR")->put("rqid", 1)->put("msgid", "CMD_GET_SEASON_SCHEDULE")->put("schedule_list", list);
}

ArrP uclTableTeams() {
  ObjP st = uclEnsure();
  return Array::parse(st->getJSONArray("groups")->getJSONArray(st->getInt("user_group"))->toString());
}

bool uclGroupFixture(int32_t f) { return f >= 1 && f <= 6; }

bool uclEnded() {
  ObjP st = uclState();
  return st && !st->optString("ended", "").empty();
}

// After the user's fixture f is stored: draw the next round once a round is complete
void uclAdvance(int32_t f) {
  ObjP st = uclEnsure();
  if (!st->optString("ended", "").empty()) return;
  ArrP fx = st->getJSONArray("fixtures");
  if (f != fx->length()) return;  // only the newest fixture moves the competition on
  int32_t user = ziruLeagueLoadInt("ziru_myleague_team_id", 100);
  std::string stage = fx->getJSONObject(f - 1)->getString("stage");
  std::vector<int32_t> next;  // clubs through to the next round, in bracket order
  if (stage == "GROUP") {
    if (f < 6) return;
    ArrP groups = st->getJSONArray("groups");
    std::vector<std::vector<int32_t>> order;
    for (int g = 0; g < 8; g++) order.push_back(uclGroupOrder(groups->getJSONArray(g)));
    for (int g = 0; g < 8; g += 2)  // 1A-2B, 1B-2A, 1C-2D, ...
      for (int s = 0; s < 2; s++) { next.push_back(order[g + s][0]); next.push_back(order[g + 1 - s][1]); }
  } else if (stage == "FINAL") {
    ArrP m = fx->getJSONObject(f - 1)->getJSONArray("matches")->getJSONArray(0);
    int hs, as;
    uclScore(f, m->getInt(0), m->getInt(1), hs, as);
    int32_t winner = hs > as ? m->getInt(0) : as > hs ? m->getInt(1) : uclShootout(f, m->getInt(0), m->getInt(1));
    st->put("ended", winner == user ? "CHAMPION" : "RUNNER_UP")->put("champion", winner);
    uclSave(st);
    return;
  } else {
    if (fx->getJSONObject(f - 1)->getInt("leg") != 2) return;
    ArrP ties = st->getJSONArray("ties");
    for (int i = 0; i < ties->length(); i++) {
      int32_t x = ties->getJSONArray(i)->getInt(0), y = ties->getJSONArray(i)->getInt(1);  // leg 1 at y
      int a1, b1, a2, b2;
      uclScore(f - 1, y, x, a1, b1);
      uclScore(f, x, y, a2, b2);
      int gx = b1 + a2, gy = a1 + b2;
      next.push_back(gx > gy ? x : gy > gx ? y : uclShootout(f, x, y));
    }
  }
  std::string newStage = stage == "GROUP" ? "R16" : stage == "R16" ? "QF" : stage == "QF" ? "SF" : "FINAL";
  if (std::find(next.begin(), next.end(), user) == next.end()) {
    st->put("ended", "OUT_" + (stage == "GROUP" ? std::string("GROUP") : stage));
    uclSave(st);
    zs::logi("OFFLINE_SERVER", "[ZIRU_UCL] eliminated after " + stage);
    return;
  }
  ArrP ties = newArr();
  for (size_t i = 0; i + 1 < next.size(); i += 2) ties->put(uclMatch(next[i], next[i + 1]));
  st->put("ties", ties);
  for (int i = 0; i < ties->length(); i++) {
    int32_t x = ties->getJSONArray(i)->getInt(0), y = ties->getJSONArray(i)->getInt(1);
    if (x != user && y != user) continue;
    if (newStage == "FINAL") {
      fx->put(newObj()->put("stage", "FINAL")->put("leg", 0)->put("matches", newArr()->put(uclMatch(x, y))));
    } else {
      fx->put(newObj()->put("stage", newStage)->put("leg", 1)->put("matches", newArr()->put(uclMatch(y, x))));
      fx->put(newObj()->put("stage", newStage)->put("leg", 2)->put("matches", newArr()->put(uclMatch(x, y))));
    }
  }
  uclSave(st);
  zs::logi("OFFLINE_SERVER", "[ZIRU_UCL] " + newStage + " " + ties->toString());
}
