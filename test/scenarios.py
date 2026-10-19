"""Request scenarios for the differential test. URIs follow the shape the client builds
from GateInfo (gate_url .../pes22/gate/) – the server only does substring matching."""

G = "/pes22/gate/"
T0 = 1792130000000


def req(msgid, rqid, **kw):
    d = {"msgid": msgid, "rqid": rqid}
    d.update(kw)
    return d


def boot():
    return [
        ("CLOCK", T0),
        ("GET", "/ntl/api/GateInfo.php", None),
        ("POST", G + "CMD_GET_SERVER_ENV", req("CMD_GET_SERVER_ENV", 1)),
        ("POST", G + "CMD_GET_MAINTENANCE_INFO", req("CMD_GET_MAINTENANCE_INFO", 2)),
        ("POST", G + "CMD_GET_COUNTRY_LIST", req("CMD_GET_COUNTRY_LIST", 3)),
        ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 4, device="iPhone")),
        ("POST", G + "CMD_GET_MYCLUB_MAINMENU_INFO", req("CMD_GET_MYCLUB_MAINMENU_INFO", 5)),
        ("POST", G + "CMD_GET_PRODUCT_LIST", req("CMD_GET_PRODUCT_LIST", 6)),
        ("POST", G + "CMD_GET_USER_EULA_INFO", req("CMD_GET_USER_EULA_INFO", 7)),
        ("POST", G + "CMD_SEND_HEARTBEAT", req("CMD_SEND_HEARTBEAT", 8)),
        ("POST", G + "CMD_SOMETHING_UNKNOWN", req("CMD_SOMETHING_UNKNOWN", 9)),
        ("POST", "/pes22/CmdGetServerEnv.php", req("GET_SERVER_ENV", 10)),
        ("POST", "/pes22/gate_CmdGetMyclubMatchStats.php", req("X", 11)),
        ("POST", "/pes22/CmdDoesNotExist.php", req("X", 12)),
        ("GET", "/livedata/pc9242_mobile_and.pak", None),
        ("GET", "/livedata/missing.pak", None),
        ("POST", G + "CMD_SET_TRACKRECORD", req("CMD_SET_TRACKRECORD", 13)),
        ("POST", G + "CMD_SEND_ADJUST_PARAM", req("CMD_SEND_ADJUST_PARAM", 14)),
        ("POST", G + "CMD_SET_GAMERELAY_QUALITY", req("CMD_SET_GAMERELAY_QUALITY", 15)),
        ("POST", G + "CMD_GET_MYCLUB_ENTRY_INFO", req("CMD_GET_MYCLUB_ENTRY_INFO", 16)),
        ("POST", G + "CMD_GET_USEABLE_TEAM_LIST", req("CMD_GET_USEABLE_TEAM_LIST", 17)),
        ("POST", G + "CMD_GET_MYCLUB_CONTRACT_INFO", req("CMD_GET_MYCLUB_CONTRACT_INFO", 18)),
        ("POST", G + "CMD_LOGIN", None),
    ]


def recruit():
    s = [("CLOCK", T0)]
    s += [
        ("POST", G + "CMD_GET_MYCLUB_AGENTLIST", req("CMD_GET_MYCLUB_AGENTLIST", 1)),
        ("POST", G + "CMD_GET_MYCLUB_PROCURABLE_GAMEPLAYERLIST", req("CMD_GET_MYCLUB_PROCURABLE_GAMEPLAYERLIST", 2, agent_id=6001)),
        ("POST", G + "CMD_USE_MYCLUB_AGENT", req("CMD_USE_MYCLUB_AGENT", 3, agent_id=6001, count=3)),
        ("POST", G + "CMD_USE_MYCLUB_AGENT", req("CMD_USE_MYCLUB_AGENT", 4, agent_id=6001, count=1)),
        ("POST", G + "CMD_GET_MYCLUB_AGENT_BOX_INFO", req("CMD_GET_MYCLUB_AGENT_BOX_INFO", 5, agent_id=6001)),
        ("POST", G + "CMD_GET_MYCLUB_AGENTLIST", req("CMD_GET_MYCLUB_AGENTLIST", 6)),
        ("POST", G + "CMD_GET_MYCLUB_PROCURABLE_GAMEPLAYERLIST", req("CMD_GET_MYCLUB_PROCURABLE_GAMEPLAYERLIST", 7, agent_id=6001)),
        ("POST", G + "CMD_RESET_MYCLUB_AGENT", req("CMD_RESET_MYCLUB_AGENT", 8, agent_id=6001)),
        ("POST", G + "CMD_USE_MYCLUB_AGENT", req("CMD_USE_MYCLUB_AGENT", 9, agent_id=1368, count=10)),
        ("POST", G + "CMD_USE_MYCLUB_AGENT", req("CMD_USE_MYCLUB_AGENT", 10, agent_id=999999, count=1)),
        ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 11)),
        ("POST", G + "CMD_GET_MYCLUB_MAINMENU_INFO", req("CMD_GET_MYCLUB_MAINMENU_INFO", 12)),
    ]
    return s


P1 = {"id": 17592454614511, "serial": 359042717000000000}


def training():
    s = [("CLOCK", T0), ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 1))]
    s += [
        ("POST", G + "CMD_GET_MYCLUB_PLAYER_DATA", req("CMD_GET_MYCLUB_PLAYER_DATA", 2, gameplayer_id=P1)),
        ("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", 3, gameplayer_id_list=[P1], training_index=1,
                                                 item_list=[{"item": {"item_type": "POSITION_TRAINING", "grade": 1, "effect": 0}, "present_num": 1}])),
        ("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", 4, gameplayer_id_list=[P1], training_index=2, skill_training_category_type="SHOOT",
                                                 item_list=[{"item": {"item_type": "SKILL_TRAINING_CATEGORY", "grade": 1, "effect": 1}, "present_num": 1}])),
        ("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", 5, gameplayer_id_list=[P1], training_index=0,
                                                 item_list=[{"item": {"item_type": "SKILL_TRAINING", "grade": 1, "effect": 0}, "present_num": 1}])),
        ("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", 6, gameplayer_id_list=[P1], training_index=1, ability_id=5,
                                                 item_list=[{"item": {"item_type": "ABILITY_TRAINING_SELECT", "grade": 0, "effect": 0}, "present_num": 1}])),
        ("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", 7, gameplayer_id_list=[P1], training_index=2,
                                                 item_list=[{"item": {"item_type": "ABILITY_TRAINING_RANDOM", "grade": 0, "effect": 0}, "present_num": 1}])),
        ("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", 8, gameplayer_id_list=[P1], training_index=0, skill_training_category_type=3,
                                                 item_list=[{"item": {"item_type": "SKILL_TRAINING_CATEGORY", "grade": 2, "effect": 0}, "present_num": 1}])),
        ("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", 9, gameplayer_id_list=[P1],
                                                 item_list=[{"item": {"item_type": "LEVEL_TRAINING", "grade": 4, "effect": 10000}, "present_num": 3}])),
        ("POST", G + "CMD_USE_MYCLUB_CAREERPLAN", req("CMD_USE_MYCLUB_CAREERPLAN", 10, gameplayer_use_list=[{
            "gameplayer_id": P1, "overall": 90, "parameters": list(range(150)),
            "use_list": [{"parameter_type": "SHOOT", "point": 3}, {"parameter_type": "PASS", "point": 2}]}])),
        ("POST", G + "CMD_USE_MYCLUB_CAREERPLAN", req("CMD_USE_MYCLUB_CAREERPLAN", 11, gameplayer_use_list=[{
            "gameplayer_id": P1, "overall": 90, "use_list": [{"parameter_type": "SHOOT", "point": 0}]}])),
        ("POST", G + "CMD_USE_MYCLUB_CAREERPLAN", req("CMD_USE_MYCLUB_CAREERPLAN", 12, gameplayer_use_list=[{
            "gameplayer_id": P1, "overall": 90, "use_list": [{"parameter_type": "SHOOT", "point": 500}]}])),
        ("POST", G + "CMD_GET_MYCLUB_PLAYER_DATA", req("CMD_GET_MYCLUB_PLAYER_DATA", 13, gameplayer_id=P1)),
        ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 14)),
        ("POST", G + "CMD_SELL_MYCLUB_GAMEPLAYER", req("CMD_SELL_MYCLUB_GAMEPLAYER", 15, gameplayer_id_list=[{"id": 1, "serial": 359042717000000400}])),
        ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 16)),
    ]
    return s


def squad():
    s = [("CLOCK", T0), ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 1))]
    s += [
        ("POST", G + "CMD_SET_MYCLUB_SQUAD_INFO", req("CMD_SET_MYCLUB_SQUAD_INFO", 2, squad_id=9, squad_data={"name_extra": "X"})),
        ("POST", G + "CMD_SET_MYCLUB_SQUAD_INFO", req("CMD_SET_MYCLUB_SQUAD_INFO", 3, squad_id=9, squad_data={"a": 1}, base_team={"base_team_id": 6, "team_name": "Mine"})),
        ("POST", G + "CMD_SET_MYCLUB_SQUAD_INFO", req("CMD_SET_MYCLUB_SQUAD_INFO", 4)),
        ("POST", G + "CMD_UPDATE_MYCLUB_BASE_TEAM", req("CMD_UPDATE_MYCLUB_BASE_TEAM", 5, squad_id=9, base_team_id=7)),
        ("POST", G + "CMD_SET_MYCLUB_BRING_UNIFORM", req("CMD_SET_MYCLUB_BRING_UNIFORM", 6, squad_id=9, set_uniform_list=[1, 2])),
        ("POST", G + "CMD_SET_MYCLUB_ENTRY_INFO", req("CMD_SET_MYCLUB_ENTRY_INFO", 7, team_name="ABC", team_name_short="ABC")),
        ("POST", G + "CMD_SET_MYCLUB_MAIN_SQUAD", req("CMD_SET_MYCLUB_MAIN_SQUAD", 8, main_squad_id=9)),
        ("POST", G + "CMD_GET_MYCLUB_ENTRY_INFO", req("CMD_GET_MYCLUB_ENTRY_INFO", 9)),
        ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 10)),
    ]
    return s


def league():
    s = [("CLOCK", T0), ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 1))]
    s += [
        ("POST", G + "CMD_IS_FIRST_LEAGUE", req("CMD_IS_FIRST_LEAGUE", 2)),
        ("POST", G + "CMD_GET_LEAGUE_COM_INFO", req("CMD_GET_LEAGUE_COM_INFO", 3)),
        ("POST", G + "CMD_CHECK_MATCH_ENABLE", req("CMD_CHECK_MATCH_ENABLE", 4)),
        ("POST", G + "CMD_START_GAME", req("CMD_START_GAME", 5)),
        ("POST", G + "CMD_GET_VSCOM_OPPONENT", req("CMD_GET_VSCOM_OPPONENT", 6, cpu_level="HARD")),
        ("POST", G + "CMD_GET_VSCOM_OPPONENT", req("CMD_GET_VSCOM_OPPONENT", 7)),
        ("POST", G + "CMD_START_GAME", req("CMD_START_GAME", 8, game_id=T0)),
        ("POST", G + "CMD_START_GAME", req("CMD_START_GAME", 9, game_id=T0)),
        ("POST", G + "CMD_GET_VSCOM_GAME_RESULT", req("CMD_GET_VSCOM_GAME_RESULT", 10, game_id=T0,
                                                       score=[{"score_1st": 2, "score_2nd": 1}, {"score_1st": 0, "score_2nd": 1}],
                                                       goal_player_list=[{"id": 17592454614511}])),
        ("POST", G + "CMD_GET_VSCOM_GAME_RESULT", req("CMD_GET_VSCOM_GAME_RESULT", 11, game_id=T0, score=[{"score_1st": 2}, {"score_1st": 0}])),
        ("POST", G + "CMD_GET_GAMERESULT_LIST", req("CMD_GET_GAMERESULT_LIST", 12)),
        ("POST", G + "CMD_GET_LEAGUE_COM_INFO", req("CMD_GET_LEAGUE_COM_INFO", 13)),
        ("CLOCK", T0 + 100000),
    ]
    rq = 14
    for m in range(11):
        g = T0 + 100000 + m * 1000
        s += [("CLOCK", g),
              ("POST", G + "CMD_GET_VSCOM_OPPONENT", req("CMD_GET_VSCOM_OPPONENT", rq)),
              ("POST", G + "CMD_START_GAME", req("CMD_START_GAME", rq + 1, game_id=g)),
              ("POST", G + "CMD_GET_VSCOM_GAME_RESULT", req("CMD_GET_VSCOM_GAME_RESULT", rq + 2, game_id=g,
                                                             score=[{"score_1st": (m * 7) % 4}, {"score_1st": (m * 3) % 3}],
                                                             intentional_give_up="YES" if m == 5 else "NO",
                                                             match_result={"game_id": g, "abnormalend_reason": "NORMAL_GIVEUP" if m == 5 else "NORMAL"}))]
        rq += 3
    s += [("POST", G + "CMD_GET_GAMERESULT_LIST", req("CMD_GET_GAMERESULT_LIST", rq)),
          ("POST", G + "CMD_GET_LEAGUE_COM_INFO", req("CMD_GET_LEAGUE_COM_INFO", rq + 1))]
    return s


def myleague():
    s = [("CLOCK", T0), ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 1))]
    s += [
        ("POST", G + "CMD_GET_MAINMENU_MATCH_CREATIVE_INFO", req("CMD_GET_MAINMENU_MATCH_CREATIVE_INFO", 2)),
        ("POST", G + "CMD_SET_SEASON_ENTER", req("CMD_SET_SEASON_ENTER", 3, select_team_id=106)),
        ("POST", G + "CMD_GET_MAINMENU_MATCH_CREATIVE_INFO", req("CMD_GET_MAINMENU_MATCH_CREATIVE_INFO", 4)),
        ("POST", G + "CMD_GET_SEASON_INFO", req("CMD_GET_SEASON_INFO", 5)),
        ("POST", G + "CMD_GET_SEASON_SCHEDULE", req("CMD_GET_SEASON_SCHEDULE", 6)),
        ("POST", G + "CMD_GET_SEASON_POINT_ITEM_LIST", req("CMD_GET_SEASON_POINT_ITEM_LIST", 7)),
        ("POST", G + "CMD_EXCHANGE_SEASON_POINT_ITEM", req("CMD_EXCHANGE_SEASON_POINT_ITEM", 8, item_id=1)),
        ("POST", G + "CMD_GET_SEASON_POINT_RENTALPLAYER_LIST", req("CMD_GET_SEASON_POINT_RENTALPLAYER_LIST", 9)),
        ("POST", G + "CMD_EXCHANGE_SEASON_POINT_RENTALPLAYER", req("CMD_EXCHANGE_SEASON_POINT_RENTALPLAYER", 10, gameplayer_id={"id": 1})),
        ("POST", G + "CMD_SET_MYCLUB_SPECIAL_DREAM_SQUAD_INFO", req("CMD_SET_MYCLUB_SPECIAL_DREAM_SQUAD_INFO", 11, special_squad_data={
            "squad_data": {"player_list": [{"gameplayer_id": P1}], "coach": [{"coach_id": {"id": 1}}], "squad_gameplan": {"x": 1}}})),
        ("POST", G + "CMD_SET_SEASON_MISSION_DISPED", req("CMD_SET_SEASON_MISSION_DISPED", 12)),
        ("POST", G + "CMD_CHECK_MATCH_ENABLE", req("CMD_CHECK_MATCH_ENABLE", 13)),
    ]
    rq = 14
    for m in range(5):
        s += [("POST", G + "CMD_GET_VSCOM_OPPONENT", req("CMD_GET_VSCOM_OPPONENT", rq, cpu_level=["LEGEND", "EASY", "", "NORMAL", "HARD"][m])),
              ("POST", G + "CMD_START_GAME", req("CMD_START_GAME", rq + 1, game_id=5000 + m)),
              ("POST", G + "CMD_GET_VSCOM_GAME_RESULT", req("CMD_GET_VSCOM_GAME_RESULT", rq + 2,
                                                             score=[{"score_1st": m % 3}, {"score_1st": (m + 1) % 2}],
                                                             intentional_give_up="YES" if m == 3 else "NO",
                                                             match_result={"abnormalend_reason": "NORMAL_GIVEUP" if m == 3 else "NORMAL"},
                                                             goal_player_list=[{"id": 17592454614511}], assist_player_list=[{"id": 88045755827028}],
                                                             opp_goal_player_list=[{"id": 123}])),
              ("POST", G + "CMD_GET_VSCOM_GAME_RESULT", req("CMD_GET_VSCOM_GAME_RESULT", rq + 2, score=[{"score_1st": 9}, {"score_1st": 9}])),
              ("POST", G + "CMD_GET_SEASON_INFO", req("CMD_GET_SEASON_INFO", rq + 3)),
              ("POST", G + "CMD_SET_PLAYDATA_MATCH", req("CMD_SET_PLAYDATA_MATCH", rq + 4)),
              ("POST", G + "CMD_RECV_CAMPAIGN_PASS_REWARD", req("CMD_RECV_CAMPAIGN_PASS_REWARD", rq + 5))]
        rq += 6
    s += [("POST", G + "CMD_GET_SEASON_SCHEDULE", req("CMD_GET_SEASON_SCHEDULE", rq)),
          ("POST", G + "CMD_IS_FIRST_LEAGUE", req("CMD_IS_FIRST_LEAGUE", rq + 1)),
          ("POST", G + "CMD_GET_VSCOM_OPPONENT", req("CMD_GET_VSCOM_OPPONENT", rq + 2))]
    return s


ALL = {"boot": boot, "recruit": recruit, "training": training, "squad": squad, "league": league, "myleague": myleague}


R1 = {"id": 88045755960770, "serial": 777000000000001}
R2 = {"id": 88045755863174, "serial": 777000000000002}


def career():
    s = [("CLOCK", T0), ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", 1))]
    s += [("POST", G + "CMD_GET_MYCLUB_PLAYER_DATA", req("CMD_GET_MYCLUB_PLAYER_DATA", 2, gameplayer_id=R1)),
          ("POST", G + "CMD_USE_MYCLUB_CAREERPLAN", req("CMD_USE_MYCLUB_CAREERPLAN", 3, gameplayer_use_list=[{
              "gameplayer_id": R1, "overall": 90, "parameters": list(range(150)),
              "use_list": [{"parameter_type": "SHOOT", "point": 3}, {"parameter_type": "PASS", "point": 2}]}])),
          ("POST", G + "CMD_GET_MYCLUB_PLAYER_DATA", req("CMD_GET_MYCLUB_PLAYER_DATA", 4, gameplayer_id=R1)),
          ("POST", G + "CMD_USE_MYCLUB_CAREERPLAN", req("CMD_USE_MYCLUB_CAREERPLAN", 5, gameplayer_use_list=[{
              "gameplayer_id": R1, "overall": 90, "use_list": []}])),
          ("POST", G + "CMD_USE_MYCLUB_CAREERPLAN", req("CMD_USE_MYCLUB_CAREERPLAN", 6, gameplayer_use_list=[{
              "gameplayer_id": R2, "overall": 90, "use_list": [{"parameter_type": "SHOOT", "point": 1000}]}]))]
    rq = 7
    for k in range(8):
        s.append(("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", rq, gameplayer_id_list=[R1], training_index=k % 6,
                 skill_training_category_type=["PASS", "DRIBBLE", "DEFENCE", "OTHER", " shoot ", ""][k % 6],
                 item_list=[{"item": {"item_type": "SKILL_TRAINING_CATEGORY" if k % 2 else "SKILL_TRAINING", "grade": k, "effect": k}, "present_num": 1}])))
        rq += 1
    for k in range(3):
        s.append(("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", rq, gameplayer_id_list=[R1], training_index=k,
                 item_list=[{"item": {"item_type": "POSITION_TRAINING", "grade": 1, "effect": 0}, "present_num": 1}])))
        s.append(("POST", G + "CMD_USE_MYCLUB_ITEM", req("CMD_USE_MYCLUB_ITEM", rq + 1, gameplayer_id_list=[R1], training_index=k,
                 item_list=[{"item": {"item_type": "ABILITY_TRAINING_RANDOM", "grade": 0, "effect": 0}, "present_num": 1}])))
        rq += 2
    s += [("POST", G + "CMD_LOGIN", req("CMD_LOGIN", rq)),
          ("POST", G + "CMD_SELL_MYCLUB_GAMEPLAYER", req("CMD_SELL_MYCLUB_GAMEPLAYER", rq + 1, gameplayer_id_list=[R1])),
          ("POST", G + "CMD_LOGIN", req("CMD_LOGIN", rq + 2))]
    return s


ALL["career"] = career
