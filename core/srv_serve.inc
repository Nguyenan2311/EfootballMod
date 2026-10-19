// ---- serve() routing and serveJsonAsEncryptedMsgPack (OfflineServer.java 4722-5075) ----

Response serveJsonAsEncryptedMsgPack(const std::string& name) {
  std::string text = readAsset(name);  // IOException propagates (caller decides)
  std::string trimmed = text;
  size_t b = 0;
  while (b < trimmed.size() && (unsigned char)trimmed[b] <= ' ') b++;
  Value root;
  if (b < trimmed.size() && trimmed[b] == '[') root = Value(Array::parse(text));
  else root = Value(Object::parse(text));

  ObjP jo = root.t == Value::OBJ ? root.o : nullptr;
  if (jo && name == "responses/CmdCreateUser.json") {
    // A missing client AUTH_USERID must not create a new offline roster. Resume
    // the same save, including recruits, even when iOS repeats CREATE_USER.
    ObjP login = loadZiruJsonAssetObject("responses/CmdLogin.json");
    if (login) for (const auto& key : login->keys()) {
      if (!jo->has(key)) jo->put(key, login->get(key));
    }
  }
  bool resumesSave = name == "responses/CmdLogin.json" || name == "responses/CmdCreateUser.json";
  ObjP req;
  if (jo && ziruLastRequestBody && (req = decodeZiruRequestJson(ziruLastRequestBody))) {
    jo->put("rqid", (long long)req->optLong("rqid", 0));
    std::string msgid = req->optString("msgid", "");
    if (!msgid.empty()) jo->put("msgid", msgid);
  }
  if (jo && (resumesSave || name == "responses/get_user_eula_info.json" || name == "responses/get_myclub_mainmenu_info.json")) {
    if (resumesSave || name == "responses/get_myclub_mainmenu_info.json") {
      if (resumesSave) patchLocalLivedataUrls(jo);
      ObjP ci = jo->optJSONObject("coin_info");
      if (!ci) { ci = newObj(); jo->put("coin_info", ci); }
      ci->put("coin", 9999999);
      ArrP dt = newArr();
      dt->put(newObj()->put("before_paid_coin", 9999999)->put("before_free_coin", 0)->put("after_paid_coin", 9999999)->put("after_free_coin", 0));
      ci->put("details", dt);
      jo->put("gp", 999999999);
    }
    ObjP eula = jo->optJSONObject("user_eula_info");
    if (eula) {
      eula->put("eula_privacy_action", "NONE");
      eula->put("eula_country_age_kind", "NONE");
      // Keep Ziru's own country (TUR). Forcing VNM here (e1d55ad) is when the Ziru packs disappeared.
    }
    jo->put("select_country_privacy_action", "NONE");
    jo->put("country_status", "NO_CHANGE");
  }
  if (jo && (name == "responses/get_myclub_commentary_info.json" || name == "responses/ZIRU_GET_COMMENTARY_DATA.json"))
    patchIosCommentaryData(jo);
  if (jo) {
    if (name == "responses/CmdGetMyclubProcurableGameplayerlist.json") applyZiruProcurableList(jo);
    if (name == "responses/CmdGetMyclubAgentBoxInfo.json") ziruPatchAgentBoxInfo(jo);
    if (name == "responses/CmdResetMyclubAgent.json") applyZiruResetBox(jo);
    if (name == "responses/CmdUseMyclubAgent.json") {
      applyZiruDynamicRecruit(jo);
      // The client refreshes its pack list from this response's agent_list. Ziru's file always carries one entry
      // (agent 1368) whatever pack was opened, so after one pull every other pack (BIG TIME 1348, SHOW TIME 1374 ...)
      // vanished from the client's list and its banner opened an empty page ("%s"). Send the whole pack list with
      // the current remaining counts instead.
      try {
        ObjP al = loadZiruJsonAssetObject("responses/CmdGetMyclubAgentlist.json");
        ArrP full = al ? al->optJSONArray("agent_list") : nullptr;
        if (full && full->length()) {
          jo->put("agent_list", Array::parse(full->toString()));
          applyZiruRemainingCounts(jo);
        }
      } catch (const std::exception& e) {
        zs::loge("OFFLINE_SERVER", std::string("[IOS_AGENT] agent_list refresh failed ") + e.what());
      }
      ObjP ci = jo->optJSONObject("coin_info");
      if (!ci) { ci = newObj(); jo->put("coin_info", ci); }
      ci->put("coin", 9999999);
      ArrP dt = newArr();
      dt->put(newObj()->put("before_paid_coin", 9999999)->put("before_free_coin", 0)->put("after_paid_coin", 9999999)->put("after_free_coin", 0));
      ci->put("details", dt);
      jo->put("gp", 999999999);
    }
    ArrP gl;
    if (name == "responses/CmdUseMyclubAgent.json" && (gl = jo->optJSONArray("gameplayer_list"))) {
      zs::Prefs& prefs = sp("almanac_state");
      std::string s = prefs.getString("recruited_players", "");
      std::string out;
      if (s.empty()) out = gl->toString();
      else {
        ArrP acc = Array::parse(s);
        std::set<int64_t> haveAcc;
        for (int i = 0; i < acc->length(); i++) {
          ObjP o = acc->optJSONObject(i);
          ObjP gp = o ? o->optJSONObject("gameplayer") : nullptr;
          if (!gp) gp = o;
          if (gp && gp->optJSONObject("gameplayer_id"))
            haveAcc.insert(gp->getJSONObject("gameplayer_id")->optLong("serial", 0));
        }
        for (int i = 0; i < gl->length(); i++) {
          ObjP o = gl->optJSONObject(i);
          ObjP gp = o ? o->optJSONObject("gameplayer") : nullptr;
          if (!gp) gp = o;
          int64_t srl = (gp && gp->optJSONObject("gameplayer_id")) ? gp->getJSONObject("gameplayer_id")->optLong("serial", 0) : 0;
          if (o && srl > 0 && !haveAcc.count(srl)) {
            acc->put(o);
            haveAcc.insert(srl);
          }
        }
        out = acc->toString();
      }
      prefs.edit().putString("recruited_players", out).commit();
    }
    std::string rp;
    if (resumesSave && !(rp = sp("almanac_state").getString("recruited_players", "")).empty()) {
      ArrP acc = Array::parse(rp);
      ObjP ei = jo->optJSONObject("entry_info");
      ArrP l2;
      if (ei && (l2 = ei->optJSONArray("gameplayer_list"))) {
        std::set<int64_t> have;
        for (int i = 0; i < l2->length(); i++) {
          ObjP p = l2->optJSONObject(i);
          if (p && p->optJSONObject("gameplayer_id"))
            have.insert(p->getJSONObject("gameplayer_id")->optLong("serial", 0));
        }
        for (int i = 0; i < acc->length(); i++) {
          ObjP o = acc->optJSONObject(i);
          ObjP gp = o ? o->optJSONObject("gameplayer") : nullptr;
          if (!gp) gp = o;
          if (gp && gp->optJSONObject("gameplayer_id")) {
            int64_t s = gp->getJSONObject("gameplayer_id")->optLong("serial", 0);
            if (s > 0 && !have.count(s) && !isZiruPlayerSold(s)) {
              l2->put(gp);
              have.insert(s);
            }
          }
        }
      }
    }
  }
  if (jo && name == "responses/CMD_USE_MYCLUB_ITEM.json") {
    applyZiruPositionTrainingResponse(jo);
    applyZiruBoosterTrainingResponse(jo);
    applyZiruSkillTrainingResponse(jo);
    z27InjectTrainingItems(jo);
  }
  bool isEntryInfo = (resumesSave || name == "responses/CmdGetMyclubEntryInfo.json" || name == "responses/CmdGetEntryInfo.json");
  if (jo && isEntryInfo) filterZiruSoldPlayersFromLogin(jo);
  if (jo && isEntryInfo) applyZiruSquadStateToLogin(jo);
  if (jo && isEntryInfo) normalizeZiruAllLoginPlayers(jo);
  if (jo && isEntryInfo) {
    ObjP ei = jo->optJSONObject("entry_info");
    if (ei) {
      z27InjectTrainingItems(ei);
      ei->put("current_gameplayer_warehouse_list_max", 100000);
      ei->put("warehouse_player_max", 100000);
      ei->put("current_gameplayer_list_max", 100000);
      ei->put("gameplayer_list_max", 100000);
      ei->put("unlock_gameplayer_frame", 100000);
      ei->put("unlock_gameplayer_warehouse_frame", 100000);
    }
  }
  if (jo && resumesSave) {
    jo->put("gp", 999999999LL);
    ObjP ci = jo->optJSONObject("coin_info");
    if (!ci) { ci = newObj(); jo->put("coin_info", ci); }
    ci->put("coin", 999999LL);
    ObjP user = jo->optJSONObject("user");
    if (user) user->put("gp", 999999999LL);
  }
  if (jo && (name == "responses/CmdGetMyclubAgentlist.json" || name == "responses/get_myclub_mainmenu_info.json")) applyZiruRemainingCounts(jo);
  if (jo && name == "responses/get_myclub_mainmenu_info.json") {
    jo->put("gp", 999999999LL);
    ObjP ci = jo->optJSONObject("coin_info");
    if (!ci) { ci = newObj(); jo->put("coin_info", ci); }
    ci->put("coin", 999999LL);
    // Main-menu banners of type AGENT are the entry points to Ziru's packs (EPIC 1368, BIG TIME 1348, SHOW TIME 1374).
    // The client's EAnnouncementType has only AGENT(0) EVENT CAMPAIGN PACK COMPETITION COMEBACK_ACHIEVEMENT
    // (CmdGetMainmenuInfo literals); there is no NONE, so "NONE" falls back to AGENT 0 and tapping the banner opens an
    // empty pack page ("%s"). Ziru's "ZIRU OFFICIAL" banner is NONE, and ab162b9 made every pack banner NONE.
    // A banner whose type is unknown, or an AGENT banner whose agent does not exist, now opens Ziru's first pack.
    ArrP bl = jo->optJSONArray("banner_list");
    if (bl) {
      std::vector<long long> order;
      std::set<long long> agents;
      try {
        ObjP al = loadZiruJsonAssetObject("responses/get_myclub_agentlist.json");
        ArrP l = al ? al->optJSONArray("agent_list") : nullptr;
        for (int i = 0; l && i < l->length(); i++) {
          ObjP a = l->optJSONObject(i);
          long long id = a ? a->optLong("agent_id", 0) : 0;
          if (id && agents.insert(id).second) order.push_back(id);
        }
      } catch (const std::exception&) {
      }
      static const std::set<std::string> known = {"AGENT", "EVENT", "CAMPAIGN", "PACK", "COMPETITION", "COMEBACK_ACHIEVEMENT"};
      for (int i = 0; i < bl->length(); i++) {
        ObjP b = bl->optJSONObject(i);
        if (!b) continue;
        std::string type = b->optString("announcement_type", "");
        long long id = b->optLong("entity_id", 0);
        if (known.count(type) && (type != "AGENT" || agents.count(id))) continue;
        if (!order.empty()) {
          logRequest("IOS_BANNER", "[IOS_BANNER] banner '" + b->optString("title_text", "") + "' " + type + " " + std::to_string(id) +
                                       " -> AGENT " + std::to_string(order[0]));
          b->put("announcement_type", "AGENT");
          b->put("entity_id", order[0]);
        } else {
          bl->remove(i--);  // no pack to open: drop the banner rather than show an empty page
        }
      }
    }
  }
  if (jo && name == "responses/get_user_eula_info.json") {
    ObjP uei = jo->optJSONObject("user_eula_info");
    if (uei) {
      uei->put("eula_privacy_action", "NONE");
      uei->put("eula_country_age_kind", "NONE");
    }
    jo->put("select_country_privacy_action", "NONE");
    jo->put("country_status", "NO_CHANGE");
  }
  if (jo) {
    z27Restore(jo);
    z27RepairLoginRoster(jo);
  }
  Response r = newFixed(200, "application/x-msgpack", encodeClientResponse(root));
  r.headers["Connection"] = "close";
  return r;
}

static const char* routeAsset(const std::string& uri) {
  struct R { const char* k; const char* v; };
  static const R first[] = {
      {"GateInfo", "GateInfo.json"}, {"GET_SERVER_ENV", "CmdGetServerEnv.json"}, {"CmdGetServerEnv.php", "CmdGetServerEnv.json"},
      {"CmdGetCountryList.php", "GetCountryList.json"}, {"CmdGetProductList.php", "get_product_list.json"},
      {"CmdGetMyclubMainmenuInfo.php", "get_myclub_mainmenu_info.json"}, {"CmdGetMyclubCoachContractNorma.php", "get_myclub_coaches_norma.json"},
      {"CmdGetMyclubCommentaryInfo.php", "get_myclub_commentary_info.json"}, {"CmdSetMyclubEntryInfo.php", "set_myclub_entry_info.json"},
      {"CmdLogin.php", "CmdLogin.json"}, {"CmdCreateUser.php", "CmdCreateUser.json"}, {"CmdGetMyclubEntryInfo.php", "CmdGetMyclubEntryInfo.json"},
      {"CmdCheckString.php", "CmdCheckString.json"}, {"CmdSetMyclubUserInfo.php", "CmdSetMyclubUserInfo.json"},
      {"CmdGetMyclubAchievementlist.php", "CmdGetMyclubAchievementlist.json"},
      {"CmdSetMyclubTutorialAchievementInfo.php", "CmdSetMyclubTutorialAchievementInfo.json"}, {"CmdExtendMyclubCoach.php", "CmdExtendMyclubCoach.json"},
      {"CmdExtendMyclubGameplayer.php", "CmdExtendMyclubGameplayer.json"}, {"CmdGetMyclubPresentlist.php", "CmdGetMyclubPresentlist.json"},
      {"COUNTRY", "GetCountryList.json"}, {"PRODUCT", "get_product_list.json"}, {"MAINTENANCE", "get_maintenance_info.json"},
      {"LOGIN", "CmdLogin.json"}, {"CREATE_USER", "CmdCreateUser.json"}, {"ENTRY_INFO", "CmdGetEntryInfo.json"},
      {"AGENT_BOX", "CmdGetMyclubAgentBoxInfo.json"}, {"AGENTLIST", "CmdGetMyclubAgentlist.json"},
      {"COACH_NORMA", "CmdGetMyclubCoachContractNorma.json"}, {"COMMENTARY", "get_myclub_commentary_info.json"},
      {"MARKET_AGENT", "get_myclub_market_agentlist.json"}, {"PRESENTLIST", "get_myclub_presentlist.json"},
      {"PROCURABLE", "CmdGetMyclubProcurableGameplayerlist.json"}, {"RESET_MYCLUB_AGENT", "CmdResetMyclubAgent.json"},
      {"USE_MYCLUB_AGENT", "CmdUseMyclubAgent.json"}, {"USE_MYCLUB_ITEM", "CMD_USE_MYCLUB_ITEM.json"},
      {"SET_MYCLUB_SQUAD_INFO", "CMD_SET_MYCLUB_SQUAD_INFO.json"},
      {"VSCOM", "get_myclub_vscom_opponent.json"}, {"RECOVER_ENERGY", "CmdRecoverEnergy.json"}, {"DOWNLOAD_STATS", "CmdSendDownloadStatsData.json"},
      {"HEARTBEAT", "CmdSendHeartbeat.json"}, {"MATCH_TEAM", "CmdSendMatchTeamInfo.json"}, {"PLAYLOG", "CmdSendPlaylog.json"},
      {"DEVICETOKEN", "CmdSetDevicetoken.json"}, {"MATCH_RESULT", "CmdSetMatchResult.json"}, {"LANGUAGE", "CmdSetMyclubLanguage.json"},
      {"LOCK_GAMEPLAYER", "CmdSetMyclubLockGameplayer.json"}, {"MAIN_SQUAD", "CmdSetMyclubMainSquad.json"},
      {"SEASON_UPDATE", "CmdSetMyclubSeasonUpdateInfo.json"}, {"SQUAD_INFO", "CmdSetMyclubSquadInfo.json"},
      {"SQUAD_NAME", "CmdSetMyclubSquadName.json"}, {"TUTORIAL", "CmdSetMyclubTutorialAchievementInfo.json"},
      {"MYCLUB_USER", "set_myclub_user_info.json"}, {"USER_UPDATE", "set_user_update_info.json"}, {"START_MATCH", "cmd_start_match.json"},
      {"USE_AGENT", "CmdUseMyclubAgent.json"}, {"AUCTION_WATCH", "cmd_add_myclub_auction_watch_list.json"},
      {"AUCTION_UPDATE", "cmd_get_myclub_auction_update_info.json"}, {"MARKET_COACH", "cmd_get_myclub_market_coachlist.json"},
      {"NEWSLIST", "cmd_get_myclub_newslist.json"}, {"EXTEND_GAMEPLAYER", "extend_myclub_gameplayer.json"},
      {"QUALITYCHECK", "gamerelay_qualitycheck_list.json"}, {"ADVERTISEMENT", "get_advertisement.json"}, {"MAILBOX", "get_mailbox_info.json"},
      {"CREATIVE", "get_mainmenu_match_creative_info.json"}, {"FRIEND_PENDING", "get_myclub_friend_pending_list.json"},
      {"MAINMENU", "get_myclub_mainmenu_info.json"}, {"USER_DETAIL", "get_myclub_user_detail.json"}, {"ONLINE_MATCH", "get_online_match_info.json"},
      {"PATHTOGLORY", "get_pathtoglory_info.json"}, {"SEASON_INFO", "get_season_info.json"},
      {"RENTALPLAYER", "get_season_point_rentalplayer_list.json"}, {"STADIUM", "get_stadium_data.json"}, {"TEAM_LIST", "get_useable_team_list.json"},
      {"EULA", "get_user_eula_info.json"}, {"AUCTIONLIST", "getauctionlist.json"}, {"SET_MYCLUBUSER_ACHIEVEMENTLIST", "set_myclubuser_achieveentlist.json"},
      {"GET_MYCLUB_CONTRACT_INFO", "get_myclub_contract_info.json"}, {"START_GAME", "start_game.json"},
  };
  for (const R& r : first)
    if (uri.find(r.k) != std::string::npos) return r.v;
  return nullptr;
}

Response serveInner(Request& r) {
  const std::string& uri = r.uri;
  std::string norm = toUpperUS(replaceAll(uri, "_", ""));
  auto isCmd = [&](const char* c) {
    return contains(norm, toUpperUS(replaceAll(c, "_", "")));
  };
  if (startsWith(uri, "/livedata/")) return serveLivedataAsset(uri.substr(1));
  if (isCmd("CMD_SELL_MYCLUB_GAMEPLAYER")) {
    auto x = handleZiruSellPlayerEarly(r);
    if (!x) return newFixedText(500, "SERVER INTERNAL ERROR: Serve() returned a null response.");
    return *x;
  }
  logRequest(uri, r.method);
  if (isCmd("CMD_GET_STADIUM_DATA")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_GET_STADIUM_DATA");
    jo->put("data_list", installedStadiumData());
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_CHECK_STRING")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_CHECK_STRING");
    jo->put("check_result", "OK");
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_GET_MYCLUB_SELECT_SKILL")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_GET_MYCLUB_SELECT_SKILL");
    jo->put("is_success", "YES");
    ArrP list = newArr();
    for (int i = 0; i < 74; i++) list->put(i);
    jo->put("select_skill_list", list);
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_CREATE_MYCLUB_SQUAD")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    std::string name = req ? req->optString("name", req->optString("squad_name", "Tactic")) : "Tactic";
    int32_t mainId = z27Main();
    ObjP baseSquad = z27Squad(mainId);
    std::set<int> used;
    ArrP existing = z27Entry()->getJSONArray("squad_list");
    for (int i = 0; i < existing->length(); ++i) used.insert(existing->getJSONObject(i)->getInt("squad_id"));
    int32_t nextId = 0;
    for (int id = 1; id <= 20; id++) {
      // z27Squad falls back to the main squad for unknown IDs; it cannot
      // determine whether an ID is free.
      if (!used.count(id) && !loadSquad(id)) {
        nextId = id;
        break;
      }
    }
    if (!nextId) {
      return serveObjectAsEncryptedMsgPack(z27Ack("CMD_CREATE_MYCLUB_SQUAD", req)->put("result", "ERROR"));
    }
    ObjP newSquad = z27Copy(baseSquad);
    newSquad->put("squad_id", nextId);
    newSquad->put("squad_name", name);
    saveSquad(nextId, newSquad->toString());
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_CREATE_MYCLUB_SQUAD");
    jo->put("squad_id", nextId);
    jo->put("squad_data", newSquad);
    // CmdCreateMyclubSquadResult::UnPackJson reads this MyclubSquad wrapper,
    // not a top-level squad_data field (iOS 11.1.1 VA 0x103b204bc).
    ObjP squadEntry = newObj()->put("squad_id", nextId)->put("name", name)
        ->put("squad_data", newSquad)->put("base_team", newArr()->put(z27Base(mainId)));
    jo->put("new_squad_data", squadEntry);
    existing->put(squadEntry);
    z27PutMeta("z27_base_" + str(nextId), z27Base(mainId)->toString());
    logRequest("ZIRU_SQUAD_CREATE", "squad=" + str(nextId));
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_DELETE_MYCLUB_SQUAD")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    int32_t sq = req ? req->optInt("squad_id", 0) : 0;
    if (sq > 0) {
      ziruDb->deleteSquad(sq);
      ArrP sqs = z27Entry()->optJSONArray("squad_list");
      if (sqs) {
        for (int i = 0; i < sqs->length(); i++) {
          ObjP s = sqs->optJSONObject(i);
          if (s && s->optInt("squad_id", 0) == sq) {
            sqs->remove(i);
            break;
          }
        }
      }
      logRequest("ZIRU_SQUAD_DELETE", "deleted squad=" + str(sq));
    }
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_DELETE_MYCLUB_SQUAD");
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_SET_MYCLUB_SQUAD_NAME")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    int32_t sq = req ? req->optInt("squad_id", 0) : 0;
    std::string newName = req ? req->optString("squad_name", req->optString("name", "Tactic")) : "Tactic";
    if (sq > 0) {
      ObjP sData = z27Squad(sq);
      if (sData) {
        sData->put("squad_name", newName);
        saveSquad(sq, sData->toString());
      }
      ArrP sqs = z27Entry()->optJSONArray("squad_list");
      if (sqs) {
        for (int i = 0; i < sqs->length(); i++) {
          ObjP s = sqs->optJSONObject(i);
          if (s && s->optInt("squad_id", 0) == sq) {
            s->put("name", newName);
            break;
          }
        }
      }
      logRequest("ZIRU_SQUAD_RENAME", "renamed squad=" + str(sq) + " to " + newName);
    }
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_SET_MYCLUB_SQUAD_NAME");
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_GET_EVENT_LIST_AND_LOG")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    ObjP jo = loadZiruJsonAssetObject("responses/ZIRU_EVENT_LIST.json");
    if (!jo) {
      jo = newObj();
      jo->put("event_list", newArr());
      jo->put("event_log_list", newArr());
      jo->put("event_num", 0);
    }
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_GET_EVENT_LIST_AND_LOG");
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_CREATEJOIN_ROOM") || isCmd("CMD_JOIN_ROOM") ||
      isCmd("CMD_GET_ROOM_LIST") || isCmd("CMD_GET_ROOM_INFO") ||
      isCmd("CMD_GET_COMMUNITY_ROOM_LIST") || isCmd("CMD_GET_TURN_SERVER_LIST")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    std::string msgid = req ? req->optString("msgid", "") : "";
    if (msgid.empty()) {
      std::string sub = uri.substr(uri.rfind('/') == std::string::npos ? 0 : uri.rfind('/') + 1);
      if (startsWith(sub, "gate_")) sub = sub.substr(5);
      if (endsWith(sub, ".php")) sub = sub.substr(0, sub.length() - 4);
      msgid = sub;
    }
    ObjP jo = newObj();
    if (isCmd("CMD_GET_ROOM_INFO")) {
      jo->put("result", "ERR_NOT_IN_ROOM");
    } else {
      jo->put("result", "ERROR_MAINTENANCE");
    }
    jo->put("rqid", rq);
    jo->put("msgid", msgid);
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_GET_AGE_GATE_REQUIREMENTS")) return handleZiruStartupAck("CMD_GET_AGE_GATE_REQUIREMENTS");
  if (isCmd("CMD_SET_USER_EULA_INFO")) return handleZiruStartupAck("CMD_SET_USER_EULA_INFO");
  if (isCmd("CMD_USE_MYCLUB_CAREERPLAN")) return handleZiruCareerplan(r);
  logAlmanacAgentRequestBody(r);
  traceZiruUserCompeV87(uri);
  auto zr = z27Route(uri);
  if (zr) return *zr;
  if (isCmd("CMD_RECV_MYCLUB_ACHIEVEMENT")) {
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    long long rq = req ? req->optLong("rqid", 1) : 1;
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    jo->put("rqid", rq);
    jo->put("msgid", "CMD_RECV_MYCLUB_ACHIEVEMENT");

    ArrP achList = newArr();
    ArrP resList = newArr();

    std::vector<int> claimedIds;
    if (req) {
      if (req->has("achievement_id")) {
        claimedIds.push_back(req->optInt("achievement_id", 0));
      }
      ArrP idArr = req->optJSONArray("achievement_id_list");
      if (idArr) {
        for (int i = 0; i < idArr->length(); i++) {
          int cid = idArr->optInt(i, 0);
          if (cid > 0 && !vcontains(claimedIds, cid)) claimedIds.push_back(cid);
        }
      }
      ArrP infoArr = req->optJSONArray("achievement_item_info_list");
      if (infoArr) {
        for (int i = 0; i < infoArr->length(); i++) {
          ObjP entry = infoArr->optJSONObject(i);
          if (entry) {
            int cid = entry->optInt("achievement_id", 0);
            if (cid > 0 && !vcontains(claimedIds, cid)) claimedIds.push_back(cid);
          }
        }
      }
    }
    if (claimedIds.empty()) {
      for (int i = 101; i <= 108; i++) claimedIds.push_back(i);
    }

    ObjP achConfig = loadZiruJsonAssetObject("responses/set_myclubuser_achieveentlist.json");
    ArrP cfgList = achConfig ? achConfig->optJSONArray("achievement_list") : nullptr;

    ArrP fullAchList = newArr();

    for (int cid : claimedIds) {
      if (cid <= 0) continue;

      ObjP matchedPres;
      if (cfgList) {
        for (int k = 0; k < cfgList->length(); k++) {
          ObjP entry = cfgList->optJSONObject(k);
          if (entry && entry->optInt("achievement_id") == cid) {
            matchedPres = entry->optJSONObject("present");
            break;
          }
        }
      }

      ObjP aEntry = newObj();
      aEntry->put("achievement_id", cid);
      aEntry->put("is_clear", "true");
      aEntry->put("is_received", "true");
      aEntry->put("level", 1);
      aEntry->put("complete_level", 1);
      aEntry->put("achievement_num", 10);
      aEntry->put("achievement_need_num", 10);
      aEntry->put("present_type", "achievement_reward");
      aEntry->put("present_count", matchedPres ? matchedPres->optInt("count", 50) : 50);
      if (matchedPres) {
        aEntry->put("present", z27Copy(matchedPres));
      }
      fullAchList->put(aEntry);

      ObjP rRes = newObj();
      rRes->put("achievement_id", cid);
      rRes->put("is_success", "YES");
      rRes->put("rev_result", "NONE");
      resList->put(rRes);

      ObjP itemInfo = newObj();
      itemInfo->put("achievement_id", cid);

      if (matchedPres) {
        itemInfo->put("present", z27Copy(matchedPres));
        ObjP ii = matchedPres->optJSONObject("item_info");
        if (ii) {
          ObjP it = newObj();
          it->put("item_type", ii->optString("item_type", "SKILL_TRAINING_SELECT"));
          it->put("grade", ii->optInt("item_grade", 0));
          it->put("effect", ii->optInt("item_effect", 0));
          itemInfo->put("item", it);
          itemInfo->put("present_num", matchedPres->optInt("count", 50));
          itemInfo->put("inventory_num", 999999);
        }
      } else {
        ObjP pres = newObj();
        pres->put("present_id", 2000 + cid);
        pres->put("present_type", "item");
        pres->put("count", 50);
        pres->put("name", "Reward");
        ObjP it = newObj()->put("item_type", "SKILL_TRAINING_SELECT")->put("grade", 0)->put("effect", 0);
        pres->put("item_info", it);
        itemInfo->put("present", pres);
        itemInfo->put("item", it);
        itemInfo->put("present_num", 50);
        itemInfo->put("inventory_num", 999999);
      }
      achList->put(itemInfo);
    }

    jo->put("achievement_list", fullAchList);
    jo->put("achievement_item_info_list", achList);
    jo->put("receive_result_list", resList);
    jo->put("fame_receive_result_list", newArr());
    jo->put("is_expired_item_included", "NO");
    jo->put("fame_notification_list", newArr());
    jo->put("achievement_notification", newArr());

    ArrP items = newArr();
    ObjP eiWrap = newObj();
    eiWrap->put("item_list", items);
    z27InjectTrainingItems(eiWrap);
    jo->put("item_list", items);
    jo->put("gp", 999999999);
    ArrP dt = newArr();
    dt->put(newObj()->put("before_paid_coin", 999999)->put("before_free_coin", 0)->put("after_paid_coin", 999999)->put("after_free_coin", 0));
    jo->put("coin_info", newObj()->put("coin", 999999)->put("details", dt));

    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_GET_MYCLUB_ACHIEVEMENTLIST")) return handleZiruLeagueStatic("responses/CmdGetMyclubAchievementlist.json");
  if (isCmd("CMD_PROGRESS_MYCLUB_ACHIEVEMENT")) return handleZiruProtocolAckV81("CMD_PROGRESS_MYCLUB_ACHIEVEMENT");
  if (isCmd("CMD_SET_MYCLUB_ACHIEVEMENT")) return handleZiruProtocolAckV81("CMD_SET_MYCLUB_ACHIEVEMENT");
  if (isCmd("CMD_GET_MAINMENU_MATCH_CREATIVE_INFO")) {
    ziruMyLeagueSetActive(0);
    return handleZiruMyLeagueMainmenuCreativeV78();
  }
  if (isCmd("CMD_SET_SEASON_ENTER")) return handleZiruMyLeagueEnterV71();
  if (isCmd("CMD_GET_SEASON_INFO")) return handleZiruMyLeagueInfoV71();
  if (isCmd("CMD_GET_SEASON_SCHEDULE")) return handleZiruMyLeagueScheduleV74();
  if (isCmd("CMD_GET_SEASON_POINT_ITEM_LIST")) return handleZiruLeagueStatic("responses/ZIRU_ML_POINT_ITEM_LIST.json");
  if (isCmd("CMD_EXCHANGE_SEASON_POINT_ITEM")) return handleZiruMyLeagueExchangeItemV71();
  if (isCmd("CMD_GET_SEASON_POINT_RENTALPLAYER_LIST")) return handleZiruLeagueStatic("responses/ZIRU_ML_RENTAL_LIST.json");
  if (isCmd("CMD_EXCHANGE_SEASON_POINT_RENTALPLAYER")) return handleZiruMyLeagueExchangeRentalV71();
  if (isCmd("CMD_SET_MYCLUB_SPECIAL_DREAM_SQUAD_INFO")) return handleZiruMyLeagueSpecialSquadV71();
  if (isCmd("CMD_SET_SEASON_MISSION_DISPED")) return handleZiruLeagueStatic("responses/ZIRU_ML_SET_MISSION_DISPED.json");
  if (isCmd("CMD_SET_PLAYDATA_MATCH") && ziruMyLeagueIsActive()) return handleZiruLeagueStatic("responses/ZIRU_ML_SET_PLAYDATA_MATCH.json");
  if (isCmd("CMD_RECV_CAMPAIGN_PASS_REWARD") && ziruMyLeagueIsActive()) return handleZiruLeagueStatic("responses/ZIRU_ML_CAMPAIGN_PASS_REWARD.json");
  if (isCmd("CMD_GET_MYCLUB_PLAYER_DATA")) return handleZiruGetMyclubPlayerData(ziruLastRequestBody);
  if (isCmd("CMD_SET_MYCLUB_SQUAD_INFO")) return handleZiruSquadAck();
  if (isCmd("CMD_SELL_MYCLUB_GAMEPLAYER")) {
    auto x = handleZiruSellPlayer();
    if (!x) return newFixedText(500, "SERVER INTERNAL ERROR: Serve() returned a null response.");
    return *x;
  }
  if (isCmd("CMD_SET_TRACKRECORD")) return handleZiruTrackrecordMinimal();
  if (isCmd("CMD_SEND_ADJUST_PARAM")) return handleZiruAdjustParamMinimal();
  if (isCmd("CMD_SET_GAMERELAY_QUALITY")) return handleZiruGameRelayQualityMinimal();
  if (isCmd("CMD_IS_FIRST_LEAGUE")) {
    ziruMyLeagueSetActive(0);
    return handleZiruLeagueStatic("responses/ZIRU_IS_FIRST_LEAGUE.json");
  }
  if (isCmd("CMD_GET_LEAGUE_COM_INFO") || isCmd("CMD_GET_LEAGUE_INFO")) {
    // Entering the AI/PVP league after MyLeague must switch the following opponent
    // and START_GAME requests back to the division handlers.
    ziruMyLeagueSetActive(0);
    std::string msg = isCmd("CMD_GET_LEAGUE_INFO") ? "CMD_GET_LEAGUE_INFO" : "CMD_GET_LEAGUE_COM_INFO";
    return handleZiruLeagueInfo(msg);
  }
  if (isCmd("CMD_GET_MANNER_WARN")) {
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    jo->put("rqid", (long long)(req ? req->optLong("rqid", 1) : 1));
    jo->put("msgid", "CMD_GET_MANNER_WARN");
    jo->put("manner_grade", "A");
    jo->put("manner_point", 100);
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_SET_LEAGUE_PERFORMANCE_DISPED")) {
    ObjP jo = newObj();
    jo->put("result", "NOERR");
    ObjP req = decodeZiruRequestJson(ziruLastRequestBody);
    jo->put("rqid", (long long)(req ? req->optLong("rqid", 1) : 1));
    jo->put("msgid", "CMD_SET_LEAGUE_PERFORMANCE_DISPED");
    return serveObjectAsEncryptedMsgPack(jo);
  }
  if (isCmd("CMD_GET_GAMERESULT_LIST")) return handleZiruLeagueHistoryDynamic();
  if (isCmd("CMD_CHECK_MATCH_ENABLE"))
    return ziruMyLeagueIsActive() ? handleZiruLeagueStatic("responses/ZIRU_ML_CHECK_MATCH_ENABLE.json") : handleZiruLeagueStatic("responses/ZIRU_CHECK_MATCH_ENABLE.json");
  if (isCmd("CMD_GET_VSCOM_OPPONENT")) return ziruMyLeagueIsActive() ? handleZiruMyLeagueOpponentV72() : handleZiruLeagueOpponentDynamicV75();
  if (isCmd("CMD_START_GAME")) return ziruMyLeagueIsActive() ? handleZiruMyLeagueStartGameV71() : handleZiruLeagueStartGameDynamic();
  if (isCmd("CMD_GET_VSCOM_GAME_RESULT")) return ziruMyLeagueIsActive() ? handleZiruMyLeagueResultV71() : handleZiruLeagueResult();
  try {
    const char* a = routeAsset(uri);
    if (a) {
      std::string name = a;
      if (contains(uri, "COMMENTARY") && name == "get_myclub_commentary_info.json" && !contains(uri, "CmdGetMyclubCommentaryInfo.php")) isZiruLocalCommentaryInstalled();
      logRequest(name, "MATCH");
      return serveJsonAsEncryptedMsgPack("responses/" + name);
    }
    std::string sub = uri.substr(uri.rfind('/') == std::string::npos ? 0 : uri.rfind('/') + 1);
    if (startsWith(sub, "gate_")) sub = sub.substr(5);
    if (startsWith(sub, "GATE_")) sub = sub.substr(5);
    if (endsWith(sub, ".php")) {
      std::string rep = replaceAll(sub, ".php", ".json");
      try {
        Response x = serveJsonAsEncryptedMsgPack("responses/" + rep);
        logRequest(rep, "FALLBACK");
        return x;
      } catch (const IOEx&) {
        zs::logw("OFFLINE_SERVER", " [SERVER_MISS] No asset for fallback: " + rep);
      }
    }
    zs::logw("OFFLINE_SERVER", " [SERVER_NOT_FOUND] No match for: " + uri);
    logRequest(uri, "GENERIC_NO_RESPONSE");
    size_t bpos = uri.find("/banner/");
    if (bpos == std::string::npos) bpos = uri.find("/prd/banner/");
    if (bpos != std::string::npos || uri.find(".png") != std::string::npos || uri.find(".jpg") != std::string::npos || uri.find(".webp") != std::string::npos) {
      std::string assetPath;
      if (bpos != std::string::npos) {
        std::string sub = uri.substr(bpos + 1);
        if (sub.rfind("prd/banner/", 0) == 0) sub = sub.substr(4);
        assetPath = "responses/" + sub;
      } else {
        assetPath = (uri.front() == '/') ? uri.substr(1) : uri;
      }
      try {
        std::string imgData = readAsset(assetPath);
        std::string mime = "image/png";
        if (imgData.size() >= 3 && (uint8_t)imgData[0] == 0xFF && (uint8_t)imgData[1] == 0xD8 && (uint8_t)imgData[2] == 0xFF) {
          mime = "image/jpeg";
        } else if (imgData.size() >= 12 && imgData.compare(0, 4, "RIFF") == 0 && imgData.compare(8, 4, "WEBP") == 0) {
          mime = "image/webp";
        }
        logRequest("IOS_BANNER", "[IOS_BANNER_SERVED] " + assetPath + " (" + std::to_string(imgData.size()) + " B)");
        return newFixed(200, mime, {imgData.begin(), imgData.end()});
      } catch (const IOEx&) {
        size_t slash = assetPath.rfind('/');
        if (slash != std::string::npos) {
          std::string dir = assetPath.substr(0, slash + 1);
          for (const char* fbName : {"offline_pack_banner.png", "offline_pack_banner.jpg"}) {
            try {
              std::string imgData = readAsset(dir + fbName);
              std::string mime = (std::string(fbName).find(".jpg") != std::string::npos) ? "image/jpeg" : "image/png";
              logRequest("IOS_BANNER", "[IOS_BANNER_FALLBACK] " + dir + fbName + " (" + std::to_string(imgData.size()) + " B)");
              return newFixed(200, mime, {imgData.begin(), imgData.end()});
            } catch (...) {}
          }
        }
        try {
          std::string png = readAsset("responses/ziru_blank.png");
          return newFixed(200, "image/png", {png.begin(), png.end()});
        } catch (...) {
          return newFixed(404, "text/plain", {});
        }
      }
    }
    return handleZiruProtocolAckV81(std::nullopt);
  } catch (const std::exception& e) {
    zs::loge("OFFLINE_SERVER", " [SERVER_ERROR] Error handling " + uri + ": " + e.what());
    return newFixedText(500, e.what());
  }
}
