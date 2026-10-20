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
      (sa = loadMeta(ziruMyLeagueFixtureKeyV74(f, "away"))) && !sh->empty() && !sa->empty()) {  // blank: new season
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
  st->put("groups", groups)->put("user_group", userGroup)->put("ended", "")->put("rounds", newArr());
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
void uclResetSeason() {
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
}

void uclStartSeason(int32_t user) {
  uclResetSeason();
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

// MyLeague mode, chosen on the Champions League screen: "UCL" (default) or "LEAGUE" (the national league)
bool uclMode() {
  JStr m = loadMeta("ziru_ml_mode");
  return !m || *m != "LEAGUE";
}

// Whether fixture f counts in the MyLeague table: every league fixture, only group matches in UCL
bool uclCountsInTable(int32_t f) { return !uclMode() || (f >= 1 && f <= 6); }

bool uclEnded() {
  ObjP st = uclState();
  return uclMode() && st && !st->optString("ended", "").empty();
}

// A simulated match also credits goals and assists to the clubs' MyLeague rosters
void uclSimMatch(int32_t f, int32_t h, int32_t a) {
  int hs, as;
  uclSimScore(f, h, a, hs, as);
  ziruMlGenerateCpuEventsV85(h, hs, f);
  ziruMlGenerateCpuEventsV85(a, as, f);
}

// Winners of a knockout round whose legs are at fixtures f1, f2 (f2 = 0: one match, the final)
std::vector<int32_t> uclRoundWinners(const ArrP& ties, int32_t f1, int32_t f2) {
  std::vector<int32_t> out;
  for (int i = 0; i < ties->length(); i++) {
    int32_t x = ties->getJSONArray(i)->getInt(0), y = ties->getJSONArray(i)->getInt(1);
    int gx, gy;
    if (f2 == 0) {
      uclScore(f1, x, y, gx, gy);
    } else {  // leg 1 at y, leg 2 at x
      int a1, b1, a2, b2;
      uclScore(f1, y, x, a1, b1);
      uclScore(f2, x, y, a2, b2);
      gx = b1 + a2; gy = a1 + b2;
    }
    out.push_back(gx > gy ? x : gy > gx ? y : uclShootout(f2 ? f2 : f1, x, y));
  }
  return out;
}

ArrP uclPairs(const std::vector<int32_t>& clubs) {
  ArrP ties = newArr();
  for (size_t i = 0; i + 1 < clubs.size(); i += 2) ties->put(uclMatch(clubs[i], clubs[i + 1]));
  return ties;
}

std::string uclNextStage(const std::string& s) {
  return s == "GROUP" ? "R16" : s == "R16" ? "QF" : s == "QF" ? "SF" : "FINAL";
}

// The user is out: play the rest of the competition so the bracket and the scorers are complete
void uclFastForward(const ObjP& st, std::vector<int32_t> clubs, std::string stage, int32_t f) {
  ArrP rounds = st->getJSONArray("rounds");
  while (true) {
    ArrP ties = uclPairs(clubs);
    bool final = stage == "FINAL";
    int32_t f2 = final ? 0 : f + 1;
    for (int i = 0; i < ties->length(); i++) {
      int32_t x = ties->getJSONArray(i)->getInt(0), y = ties->getJSONArray(i)->getInt(1);
      if (final) { uclSimMatch(f, x, y); continue; }
      uclSimMatch(f, y, x);
      uclSimMatch(f2, x, y);
    }
    rounds->put(newObj()->put("stage", stage)->put("ties", ties)->put("f1", f)->put("f2", f2));
    clubs = uclRoundWinners(ties, f, f2);
    if (final) { st->put("champion", clubs[0]); return; }
    stage = uclNextStage(stage);
    f += 2;
  }
}

// After the user's fixture f is stored: play the rest of that matchday or leg, then draw the next round
void uclAdvance(int32_t f) {
  if (!uclMode()) return;
  ObjP st = uclEnsure();
  if (!st->optString("ended", "").empty()) return;
  ArrP fx = st->getJSONArray("fixtures");
  if (f != fx->length()) return;  // only the newest fixture moves the competition on
  int32_t user = ziruLeagueLoadInt("ziru_myleague_team_id", 100);
  ObjP cur = fx->getJSONObject(f - 1);
  std::string stage = cur->getString("stage");
  if (!st->optJSONArray("rounds")) st->put("rounds", newArr());
  ArrP rounds = st->getJSONArray("rounds");
  if (rounds->length() == 0 && stage != "GROUP") {  // season drawn by the first UCL build: only "ties" was kept
    int32_t f1 = f;
    while (f1 > 1 && fx->getJSONObject(f1 - 2)->getString("stage") == stage) f1--;
    rounds->put(newObj()->put("stage", stage)->put("ties", st->getJSONArray("ties"))->put("f1", f1)
                    ->put("f2", stage == "FINAL" ? 0 : f1 + 1));
    if (stage == "FINAL") cur->put("leg", 2);
  }
  std::vector<int32_t> next;  // clubs through to the next round, in bracket order
  if (stage == "GROUP") {
    ArrP groups = st->getJSONArray("groups");
    for (int g = 0; g < 8; g++) {  // the user's group is simulated by the MyLeague fixture itself
      if (g == st->getInt("user_group")) continue;
      ArrP ms = uclGroupMatchday(groups->getJSONArray(g), f - 1);
      for (int i = 0; i < ms->length(); i++) uclSimMatch(f, ms->getJSONArray(i)->getInt(0), ms->getJSONArray(i)->getInt(1));
    }
    if (f < 6) { uclSave(st); return; }
    std::vector<std::vector<int32_t>> order;
    for (int g = 0; g < 8; g++) order.push_back(uclGroupOrder(groups->getJSONArray(g)));
    for (int g = 0; g < 8; g += 2)  // 1A-2B, 1B-2A, 1C-2D, ...
      for (int s = 0; s < 2; s++) { next.push_back(order[g + s][0]); next.push_back(order[g + 1 - s][1]); }
  } else {
    ObjP round = rounds->getJSONObject(rounds->length() - 1);
    ArrP ties = round->getJSONArray("ties");
    int leg = cur->getInt("leg");
    for (int i = 0; i < ties->length(); i++) {
      int32_t x = ties->getJSONArray(i)->getInt(0), y = ties->getJSONArray(i)->getInt(1);
      if (x == user || y == user) continue;
      if (leg == 1) uclSimMatch(f, y, x); else uclSimMatch(f, x, y);
    }
    if (leg == 1) { uclSave(st); return; }
    next = uclRoundWinners(ties, round->getInt("f1"), round->getInt("f2"));
    if (stage == "FINAL") {
      st->put("ended", next[0] == user ? "CHAMPION" : "RUNNER_UP")->put("champion", next[0]);
      uclSave(st);
      return;
    }
  }
  std::string newStage = uclNextStage(stage);
  if (std::find(next.begin(), next.end(), user) == next.end()) {
    st->put("ended", "OUT_" + stage);
    uclFastForward(st, next, newStage, f + 1);
    uclSave(st);
    zs::logi("OFFLINE_SERVER", "[ZIRU_UCL] eliminated after " + stage);
    return;
  }
  ArrP ties = uclPairs(next);
  bool final = newStage == "FINAL";
  rounds->put(newObj()->put("stage", newStage)->put("ties", ties)->put("f1", f + 1)->put("f2", final ? 0 : f + 2));
  for (int i = 0; i < ties->length(); i++) {
    int32_t x = ties->getJSONArray(i)->getInt(0), y = ties->getJSONArray(i)->getInt(1);
    if (x != user && y != user) continue;
    if (final) {
      fx->put(newObj()->put("stage", "FINAL")->put("leg", 2)->put("matches", newArr()->put(uclMatch(x, y))));
    } else {
      fx->put(newObj()->put("stage", newStage)->put("leg", 1)->put("matches", newArr()->put(uclMatch(y, x))));
      fx->put(newObj()->put("stage", newStage)->put("leg", 2)->put("matches", newArr()->put(uclMatch(x, y))));
    }
  }
  uclSave(st);
  zs::logi("OFFLINE_SERVER", "[ZIRU_UCL] " + newStage + " " + ties->toString());
}

// Switch MyLeague between the national league and the Champions League; either way a new season starts
void uclSetMode(const std::string& mode) {
  bool league = mode == "LEAGUE";
  ziruDb->saveMeta("ziru_ml_mode", league ? "LEAGUE" : "UCL");
  if (league) {
    uclResetSeason();
    ziruMlResetTableV83();
    ziruDb->saveMeta("ziru_ucl", "");
  } else {
    uclStartSeason(ziruLeagueLoadInt("ziru_myleague_team_id", 100));
  }
  zs::logi("OFFLINE_SERVER", "[ZIRU_UCL] MyLeague mode " + std::string(league ? "LEAGUE" : "UCL"));
}

// ---- Champions League screen (opened from the dylib's UCL button) ----

static std::string uclTeamName(int32_t id, const ObjP& real, const ObjP& native) {
  ArrP r = real ? real->optJSONArray(str(id)) : nullptr;
  if (r) return r->getString(0);
  return native->optString(str(id), "Team " + str(id));
}

// Club names of the game's own data (MyLeague registry), for clubs without a real name
ObjP uclNativeNames() {
  ObjP native = newObj(), reg = loadZiruJsonAssetObject("responses/ZIRU_ML_COMPETITION_REGISTRY.json");
  ObjP comps = reg ? reg->optJSONObject("competitions") : nullptr;
  for (const auto& c : comps ? comps->keys() : std::vector<std::string>()) {
    ArrP ts = comps->getJSONObject(c)->optJSONArray("teams");
    for (int i = 0; ts && i < ts->length(); i++) native->put(str(ts->getJSONObject(i)->getInt("team_id")), ts->getJSONObject(i)->getString("name"));
  }
  return native;
}

// The user's fixtures played; every other match of those fixtures was simulated with them
int uclPlayed() {
  int played = 0;
  for (JStr r; played < 13 && (r = loadMeta(ziruMyLeagueFixtureKeyV74(played + 1, "result"))) && !r->empty();) played++;
  return played;
}

struct UclRow { int32_t id; int p, w, d, l, gf, ga, pts; };

// A group's table after the matchdays played (all of them once the season has ended)
std::vector<UclRow> uclGroupTable(const ArrP& teams, int played, bool ended) {
  std::map<int32_t, UclRow> t;
  for (int i = 0; i < 4; i++) t[teams->getInt(i)] = {teams->getInt(i), 0, 0, 0, 0, 0, 0, 0};
  for (int md = 0; md < 6 && (ended || md < played); md++) {
    ArrP ms = uclGroupMatchday(teams, md);
    for (int i = 0; i < ms->length(); i++) {
      int32_t h = ms->getJSONArray(i)->getInt(0), a = ms->getJSONArray(i)->getInt(1);
      int hs, as;
      uclScore(md + 1, h, a, hs, as);
      for (auto [id, gf, ga] : {std::tuple<int32_t, int, int>{h, hs, as}, std::tuple<int32_t, int, int>{a, as, hs}}) {
        UclRow& r = t[id];
        r.p++; r.gf += gf; r.ga += ga;
        if (gf > ga) { r.w++; r.pts += 3; } else if (gf == ga) { r.d++; r.pts++; } else r.l++;
      }
    }
  }
  std::vector<UclRow> rows;
  for (auto& [id, r] : t) rows.push_back(r);
  auto key = [](const UclRow& r) { return std::make_tuple(r.pts, r.gf - r.ga, r.gf, -r.id); };
  std::sort(rows.begin(), rows.end(), [&](const UclRow& x, const UclRow& y) { return key(x) > key(y); });
  return rows;
}

ObjP uclPageData() {
  ObjP real = loadZiruJsonAssetObject("responses/ZIRU_REAL_TEAM_NAMES.json"), native = uclNativeNames();
  int32_t user = ziruLeagueLoadInt("ziru_myleague_team_id", 100);
  ObjP out = newObj()->put("mode", uclMode() ? "UCL" : "LEAGUE")->put("user_name", uclTeamName(user, real, native));
  ObjP st = uclMode() ? uclState() : nullptr;
  if (st) {
    bool ended = !st->optString("ended", "").empty();
    int played = uclPlayed();
    auto isPlayed = [&](int32_t f) { return ended || f <= played; };
    out->put("ended", st->optString("ended", ""))->put("played", played);
    if (st->has("champion")) out->put("champion", uclTeamName(st->getInt("champion"), real, native));
    ArrP groups = newArr(), sg = st->getJSONArray("groups");
    for (int g = 0; g < 8; g++) {
      ArrP teams = sg->getJSONArray(g), matches = newArr();
      for (int md = 0; md < 6; md++) {
        ArrP ms = uclGroupMatchday(teams, md);
        for (int i = 0; i < ms->length(); i++) {
          int32_t h = ms->getJSONArray(i)->getInt(0), a = ms->getJSONArray(i)->getInt(1);
          ObjP m = newObj()->put("md", md + 1)->put("home", uclTeamName(h, real, native))->put("away", uclTeamName(a, real, native));
          if (isPlayed(md + 1)) {
            int hs, as;
            uclScore(md + 1, h, a, hs, as);
            m->put("hs", hs)->put("as", as);
          }
          matches->put(m);
        }
      }
      ArrP table = newArr();
      for (const UclRow& r : uclGroupTable(teams, played, ended))
        table->put(newObj()->put("team", uclTeamName(r.id, real, native))->put("user", r.id == user ? 1 : 0)->put("p", r.p)->put("w", r.w)
                       ->put("d", r.d)->put("l", r.l)->put("gf", r.gf)->put("ga", r.ga)->put("pts", r.pts));
      groups->put(newObj()->put("name", std::string(1, (char)('A' + g)))->put("table", table)->put("matches", matches));
    }
    out->put("groups", groups);
    ArrP rounds = newArr(), sr = st->optJSONArray("rounds");
    for (int i = 0; sr && i < sr->length(); i++) {
      ObjP r = sr->getJSONObject(i);
      int32_t f1 = r->getInt("f1"), f2 = r->getInt("f2");
      ArrP ties = r->getJSONArray("ties"), list = newArr();
      bool done = isPlayed(f2 ? f2 : f1);
      std::vector<int32_t> winners = done ? uclRoundWinners(ties, f1, f2) : std::vector<int32_t>();
      for (int j = 0; j < ties->length(); j++) {
        int32_t x = ties->getJSONArray(j)->getInt(0), y = ties->getJSONArray(j)->getInt(1);
        ObjP tie = newObj()->put("a", uclTeamName(x, real, native))->put("b", uclTeamName(y, real, native))
                       ->put("user", x == user || y == user ? 1 : 0);
        int s1, s2;  // goals of a, goals of b
        if (f2 == 0) {
          if (isPlayed(f1)) { uclScore(f1, x, y, s1, s2); tie->put("leg1", newArr()->put(s1)->put(s2)); }
        } else {
          if (isPlayed(f1)) { uclScore(f1, y, x, s2, s1); tie->put("leg1", newArr()->put(s1)->put(s2)); }
          if (isPlayed(f2)) { uclScore(f2, x, y, s1, s2); tie->put("leg2", newArr()->put(s1)->put(s2)); }
        }
        if (done) tie->put("winner", winners[j] == x ? "a" : "b");
        list->put(tie);
      }
      rounds->put(newObj()->put("stage", r->getString("stage"))->put("ties", list));
    }
    out->put("rounds", rounds);
  }
  ObjP names = loadZiruJsonAssetObject("responses/ZIRU_PLAYER_NAMES.json");
  for (std::string kind : {"goal", "assist"}) {
    JStr m = loadMeta("ziru_myleague_" + kind + "_stats_json");
    ObjP stats = Object::parse(m && !m->empty() ? *m : std::string("{}"));
    std::vector<ObjP> rows;
    for (const auto& k : stats->keys()) rows.push_back(stats->getJSONObject(k));
    std::stable_sort(rows.begin(), rows.end(), [](const ObjP& x, const ObjP& y) { return x->optInt("value") > y->optInt("value"); });
    ArrP list = newArr();
    for (size_t i = 0; i < rows.size() && i < 30; i++) {
      int64_t pid = rows[i]->optLong("player_id", 0);
      std::string name = names ? names->optString(str((int32_t)(pid & 0xFFFFFF)), "") : "";
      list->put(newObj()->put("name", name.empty() ? "#" + str(pid) : name)
                    ->put("team", uclTeamName(rows[i]->optInt("team_id"), real, native))->put("value", rows[i]->optInt("value")));
    }
    out->put(kind + "s", list);
  }
  return out;
}

// The screen itself: ZIRU_UCL_PAGE.html with the current season data in place of its /*DATA*/ marker
std::string uclPageHtml() {
  std::string page = readAsset("responses/ZIRU_UCL_PAGE.html");
  const std::string marker = "/*DATA*/null";
  size_t at = page.find(marker);
  if (at != std::string::npos) page.replace(at, marker.size(), replaceAll(uclPageData()->toString(), "</", "<\\/"));
  return page;
}


// ---- The season in the game's own tournament screens (User Compe: group tables, bracket) ----
// The 32 clubs appear as the competition's 32 entrants; the user's club carries the user's own id, the
// others a made-up id per club.  Schemas come from the client's commandautomake readers; matches are still
// played through MyLeague, these commands only show the competition.

static constexpr int64_t kUclCompeId = 27001;
static constexpr const char* kUclRoundNames[] = {"BEST_16", "QUARTER_FINALS", "SEMI_FINAL", "FINAL"};

struct UclCompeCtx {
  ObjP st, real, native;
  int32_t team, me;  // the user's club, the user's own id
  int played;
  bool ended;
  int32_t uid(int32_t id) const { return id == team ? me : 1800000000 + id; }
  std::string name(int32_t id) const { return uclTeamName(id, real, native); }
  std::string shortName(int32_t id) const {
    ArrP r = real ? real->optJSONArray(str(id)) : nullptr;
    return r && r->length() > 1 ? r->getString(1) : name(id).substr(0, 3);
  }
  bool isPlayed(int32_t f) const { return ended || f <= played; }
};

ObjP uclCompeUser(int32_t uid, const std::string& name) {
  return newObj()->put("user_id", uid)->put("user_name", name)->put("is_deleted", "NO")->put("platform", "IOS")
      ->put("avatar", newObj()->put("is_already_set", "NO"))->put("user_banner", newObj()->put("is_valid", "NO"));
}

ObjP uclCompeSettings(const UclCompeCtx& c) {
  ObjP ev = loadZiruJsonAssetObject("responses/ZIRU_EVENT_LIST.json");
  ObjP e = ev && ev->optJSONArray("event_list") ? ev->getJSONArray("event_list")->optJSONObject(0) : nullptr;
  ObjP env = e && e->optJSONObject("match_env") ? e->getJSONObject("match_env") : newObj();
  env->put("match_time", 10)->put("pk", "YES");
  return newObj()->put("owner_user_info", uclCompeUser(c.me, c.name(c.team)))->put("user_compe_name", "UEFA Champions League")
      ->put("is_owner_only_observe", "NO")
      ->put("regulations", newObj()->put("compe_format", "LEAGUE_AND_KNOCKOUT")->put("group_num", 8)->put("group_team_num", 4)
                               ->put("knockout_team_num", 16)->put("is_exist_third_prize_match", "NO")
                               ->put("candidate_max", 32)->put("candidate_min", 32))
      ->put("bg_file_name", "")->put("prematch_time_limit_num", 0)->put("is_set_match_date_and_time", "NO")
      ->put("match_date_and_time", newObj()->put("group_time", newArr())->put("round_time", newArr())->put("third_prize_match_time", 0))
      ->put("is_authentic", "NO")->put("is_auto_progress", "YES")
      ->put("emblem_info", newObj()->put("compe_id", 0)->put("original_emblem_id", 0)->put("special_emblem_name", ""))
      ->put("has_password", "NO")->put("password", "")
      ->put("entry_limit", newObj()->put("limit_type", "NO_LIMIT")->put("limit_category", "LIMIT_CLUB")->put("is_league_multi_team", "NO")
                               ->put("entity_id", 0)->put("limit_id", 0)->put("team_power_limit_upper", 0)
                               ->put("team_power_limit_lower", 0)->put("player_num", 0))
      ->put("match_env", env)
      ->put("stadium_setting", e && e->optJSONObject("stadium_setting") ? e->getJSONObject("stadium_setting") : newObj())
      ->put("is_used_owners_stadium", "NO")->put("is_sound_stream", "NO");
}

ObjP uclCompeProgress(const UclCompeCtx& c) {
  ArrP rounds = c.st->optJSONArray("rounds");
  int knockout = rounds ? rounds->length() : 0;
  std::string status = c.ended ? "FINISHED" : knockout ? "IN_PROGRESS_KNOCKOUT" : "IN_PROGRESS_LEAGUE";
  std::string round = knockout ? kUclRoundNames[std::min(knockout, 4) - 1] : "ROUND_NONE";
  int phase = knockout ? 1 : std::min(c.played + 1, 6);
  return newObj()->put("status", status)->put("phase", phase)->put("round", round)->put("time_limit_for_next_match", 0);
}

// One side of a match: club, goals (and shoot-out goals)
ObjP uclCompeSide(const UclCompeCtx& c, int32_t id, int goals, int pk, bool played) {
  return newObj()->put("user_id", c.uid(id))->put("team_id", id)->put("team_name", c.name(id))->put("team_name_short", c.shortName(id))
      ->put("score", newArr()->put(newObj()->put("score_1st", played ? goals : 0)->put("score_2nd", 0)->put("score_ex1st", 0)
                                       ->put("score_ex2nd", 0)->put("score_pk", std::max(pk, 0))))
      ->put("is_ex", "NO")->put("is_pk", pk >= 0 && played ? "YES" : "NO")->put("no_match", "NO");
}

ObjP uclCompeMatch(const UclCompeCtx& c, int32_t h, int hs, int32_t a, int as, int phase, bool played, int32_t pkWinner = 0) {
  return newObj()->put("user_info_list", newArr()->put(uclCompeSide(c, h, hs, pkWinner ? (pkWinner == h ? 1 : 0) : -1, played))
                                             ->put(uclCompeSide(c, a, as, pkWinner ? (pkWinner == a ? 1 : 0) : -1, played)))
      ->put("is_match_finished", played ? "YES" : "NO")->put("phase", phase);
}

ObjP uclCompeSchedule(const UclCompeCtx& c) {
  ArrP groupInfo = newArr(), roundInfo = newArr(), groups = c.st->getJSONArray("groups");
  for (int g = 0; g < 8; g++) {
    ArrP teams = groups->getJSONArray(g), users = newArr(), matches = newArr();
    for (int i = 0; i < 4; i++) users->put(c.uid(teams->getInt(i)));
    for (int md = 0; md < 6; md++) {
      ArrP ms = uclGroupMatchday(teams, md);
      for (int i = 0; i < ms->length(); i++) {
        int32_t h = ms->getJSONArray(i)->getInt(0), a = ms->getJSONArray(i)->getInt(1);
        int hs = 0, as = 0;
        if (c.isPlayed(md + 1)) uclScore(md + 1, h, a, hs, as);
        matches->put(uclCompeMatch(c, h, hs, a, as, md + 1, c.isPlayed(md + 1)));
      }
    }
    groupInfo->put(newObj()->put("round", "ROUND_NONE")->put("group", g)->put("group_user_list", users)->put("match_result_list", matches));
  }
  ArrP rounds = c.st->optJSONArray("rounds");
  for (int i = 0; rounds && i < rounds->length() && i < 4; i++) {  // one entry per tie: the aggregate of both legs
    ObjP r = rounds->getJSONObject(i);
    int32_t f1 = r->getInt("f1"), f2 = r->getInt("f2");
    ArrP ties = r->getJSONArray("ties"), users = newArr(), matches = newArr();
    bool done = c.isPlayed(f2 ? f2 : f1);
    std::vector<int32_t> winners = done ? uclRoundWinners(ties, f1, f2) : std::vector<int32_t>();
    for (int j = 0; j < ties->length(); j++) {
      int32_t x = ties->getJSONArray(j)->getInt(0), y = ties->getJSONArray(j)->getInt(1);
      users->put(c.uid(x))->put(c.uid(y));
      int gx = 0, gy = 0, s1, s2;
      if (f2 == 0) {
        if (c.isPlayed(f1)) uclScore(f1, x, y, gx, gy);
      } else {
        if (c.isPlayed(f1)) { uclScore(f1, y, x, s2, s1); gx += s1; gy += s2; }
        if (c.isPlayed(f2)) { uclScore(f2, x, y, s1, s2); gx += s1; gy += s2; }
      }
      int32_t pkWinner = done && gx == gy ? winners[j] : 0;
      matches->put(uclCompeMatch(c, x, gx, y, gy, 1, done, pkWinner));
    }
    roundInfo->put(newObj()->put("round", kUclRoundNames[i])->put("group", 0)->put("group_user_list", users)->put("match_result_list", matches));
  }
  return newObj()->put("group_info", groupInfo)->put("round_info", roundInfo);
}

ArrP uclCompeRanking(const UclCompeCtx& c) {
  ArrP out = newArr(), groups = c.st->getJSONArray("groups");
  for (int g = 0; g < 8; g++) {
    ArrP list = newArr();
    std::vector<UclRow> rows = uclGroupTable(groups->getJSONArray(g), c.played, c.ended);
    for (size_t i = 0; i < rows.size(); i++) {
      const UclRow& r = rows[i];
      list->put(newObj()->put("rank", (int)i + 1)->put("is_rank_up", "NO")->put("is_rank_down", "NO")->put("user_id", c.uid(r.id))
                    ->put("is_kicked_out_user", "NO")->put("team_id", r.id)->put("team_name", c.name(r.id))
                    ->put("is_my_info", r.id == c.team ? "YES" : "NO")->put("match_num", r.p)->put("wins", r.w)->put("losses", r.l)
                    ->put("draws", r.d)->put("goals", r.gf)->put("goals_against", r.ga)->put("diff", (long long)(r.gf - r.ga))
                    ->put("winning_points", r.pts));
    }
    out->put(newObj()->put("index", g)->put("is_my_group", g == c.st->getInt("user_group") ? "YES" : "NO")->put("ranking_data_list", list));
  }
  return out;
}

ObjP uclCompeInfo(const UclCompeCtx& c) {
  ArrP users = newArr(), teams = newArr(), groups = c.st->getJSONArray("groups");
  for (int g = 0; g < 8; g++)
    for (int i = 0; i < 4; i++) {
      int32_t id = groups->getJSONArray(g)->getInt(i);
      users->put(uclCompeUser(c.uid(id), c.name(id)));
      teams->put(id);
    }
  int64_t now = zc::currentTimeMillis() / 1000;
  ObjP core = newObj()->put("user_compe_id", (long long)kUclCompeId)->put("progress_status_info", uclCompeProgress(c))
                  ->put("entry_status", "ENTERED")->put("user_info_list", users)->put("user_selected_team_list", teams)
                  ->put("user_final_rank_list", newArr())->put("schedule_info", uclCompeSchedule(c))->put("match_info_list", newArr())
                  ->put("result_uniform_id", 0)->put("result_uni_base_team_id", 0);
  return newObj()->put("core_info", core)->put("settings", uclCompeSettings(c))->put("start_time", (long long)(now - 86400))
      ->put("end_time", (long long)(now + 30 * 86400))->put("group_ranking_data", uclCompeRanking(c))
      ->put("selected_team_id", c.team)->put("result_uniform_id", 0);
}

Response uclCompeHandle(const std::string& msgid) {
  ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
  ObjP jo = newObj()->put("result", "NOERR")->put("rqid", (long long)(req ? req->optLong("rqid", 1) : 1))->put("msgid", msgid);
  UclCompeCtx c;
  c.st = uclMode() ? uclEnsure() : nullptr;
  c.real = loadZiruJsonAssetObject("responses/ZIRU_REAL_TEAM_NAMES.json");
  c.native = uclNativeNames();
  c.team = ziruLeagueLoadInt("ziru_myleague_team_id", 100);
  c.me = req ? (int32_t)req->optLong("user_id", 1) : 1;
  c.played = uclPlayed();
  c.ended = c.st && !c.st->optString("ended", "").empty();
  if (msgid == "CMD_GET_USER_COMPE_SERVER_PARAM") {
    jo->put("param_info", newObj()->put("compe_duration", 30)->put("result_duration", 7)->put("prematch_time_limits", newArr()->put(0))
                              ->put("limit_team_power_choices", newArr()->put(0))->put("team_power_default", 0)
                              ->put("emblem_info_list", newArr()));
  } else if (msgid == "CMD_CREATE_USER_COMPE") {
    jo->put("user_compe_id", (long long)kUclCompeId);
  } else if (msgid == "CMD_GET_USER_COMPE_TOTAL_RECORD") {
    jo->put("record_list", newArr());
  } else if (msgid == "CMD_GET_USER_COMPE_ROOM_ID") {
    jo->put("room_id", newObj()->put("id", 0));
  } else if (c.st && msgid == "CMD_GET_USER_COMPE_CURRENT_ENTRY_INFO") {
    jo->put("user_compe_id", (long long)kUclCompeId)->put("progress_status_info", uclCompeProgress(c))
        ->put("compe_settings", uclCompeSettings(c))->put("d_user_compe_id", 0)->put("last_entry_compe_id", (long long)kUclCompeId);
  } else if (c.st && (msgid == "CMD_GET_USER_COMPE_INFO" || msgid == "CMD_CLOSE_USER_COMPE")) {
    jo->put("user_compe_info", uclCompeInfo(c));
  } else if (c.st && msgid == "CMD_GET_USER_COMPE_LIST") {
    jo->put("user_compe_info_list", newArr()->put(uclCompeInfo(c)));
  } else if (c.st && msgid == "CMD_GET_USER_COMPE_PROGRESS") {
    jo->put("group_ranking_data", uclCompeRanking(c))->put("phase_result_list", newArr())->put("match_result", uclCompeSchedule(c));
  } else if (c.st && msgid == "CMD_GET_USER_COMPE_USER_LIST") {
    ArrP list = newArr(), groups = c.st->getJSONArray("groups");
    for (int g = 0; g < 8; g++)
      for (int i = 0; i < 4; i++) {
        int32_t id = groups->getJSONArray(g)->getInt(i);
        list->put(newObj()->put("user_info", uclCompeUser(c.uid(id), c.name(id)))->put("is_online", "YES")->put("user_status", "CAN_START_MATCH"));
      }
    jo->put("user_compe_user_list", list);
  } else if (msgid == "CMD_GET_USER_COMPE_CURRENT_ENTRY_INFO") {  // league mode: no competition entered
    jo->put("user_compe_id", 0)->put("d_user_compe_id", 0)->put("last_entry_compe_id", 0);
  }
  logRequest("ZIRU_UCL_COMPE", "[ZIRU_UCL_COMPE_RES] " + jo->toString().substr(0, 2500));
  return serveObjectAsEncryptedMsgPack(jo);
}
