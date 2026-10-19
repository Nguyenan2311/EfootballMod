#!/usr/bin/env python3
"""PES2027 online server (mod 1.1): Friend Match rooms + NAT/TURN address exchange for the iOS client.

The iOS dylib keeps myClub on the on-device offline server and forwards only the online commands
(rooms, game session, TURN, match progress) to this server:  POST /pes22/gate/gate_<CMD>.php
Body/response use the game's own encoding (AES-256-CBC, request zlib+msgpack, response gzip+msgpack).

Every player runs Ziru's account (user_id 359042717). The dylib sends a per-device id in X-Ziru-Uid
(and X-Ziru-Name); this server works with those ids and maps the caller's own id back to 359042717 in
everything it returns, so the client always recognises itself.

Run:  python3 pes_online_server.py --port 8443 --cert cert.pem --key key.pem \
          --turn-host turn.example.com --turn-secret SECRET --log-dir logs
Requires: pip install cryptography msgpack
"""
import argparse, base64, gzip, hashlib, hmac, json, os, random, ssl, struct, sys, threading, time, urllib.parse, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import msgpack
from cryptography.hazmat.primitives import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

KEY = bytes.fromhex("43740981523cdc171e71de2ccab1a5a9b86f4b833196c55facd4bd25846c33f5")
LOCAL_UID = 359042717
VERSION = "1.1.0"
ARGS = None
LOCK = threading.RLock()
LOGF = None
MAX_REQUEST_BYTES = 2 * 1024 * 1024
MAX_DECODED_BYTES = 8 * 1024 * 1024


# ------------------------------------------------------------------ codec
def dec_request(body):
    if len(body) <= 16:
        return {}
    d = Cipher(algorithms.AES(KEY), modes.CBC(body[:16])).decryptor()
    pt = d.update(body[16:]) + d.finalize()
    u = padding.PKCS7(128).unpadder()
    pt = u.update(pt) + u.finalize()
    inflater = zlib.decompressobj()
    raw = inflater.decompress(pt, MAX_DECODED_BYTES + 1)
    if len(raw) > MAX_DECODED_BYTES or inflater.unconsumed_tail or not inflater.eof:
        raise ValueError("invalid or oversized compressed request")
    obj = msgpack.unpackb(raw, raw=False, strict_map_key=False)
    if not isinstance(obj, dict):
        raise ValueError("request must be a map")
    return obj


def enc_response(obj):
    raw = gzip.compress(msgpack.packb(obj, use_bin_type=True))
    p = padding.PKCS7(128).padder()
    data = p.update(raw) + p.finalize()
    iv = os.urandom(16)
    e = Cipher(algorithms.AES(KEY), modes.CBC(iv)).encryptor()
    return iv + e.update(data) + e.finalize()


def remap(v, a, b):
    """Replace integer a by b everywhere (dict values / list items)."""
    if isinstance(v, dict):
        return {k: remap(x, a, b) for k, x in v.items()}
    if isinstance(v, list):
        return [remap(x, a, b) for x in v]
    if isinstance(v, int) and not isinstance(v, bool) and v == a:
        return b
    return v


def log(entry):
    entry["t"] = time.strftime("%Y-%m-%d %H:%M:%S")
    line = json.dumps(entry, ensure_ascii=False, default=str)
    with LOCK:
        if LOGF:
            LOGF.write(line + "\n"); LOGF.flush()
    if ARGS and ARGS.verbose:
        print(line[:400])


# ------------------------------------------------------------------ state
class User:
    def __init__(self, uid, name, platform):
        self.uid, self.name, self.platform = uid, name, platform
        self.seen = time.time()
        self.room = None
        self.turn_inbox = []        # TURN address blobs other players sent to this user
        self.session_check = None

    def info(self):
        return {"user_id": self.uid, "user_name": self.name, "is_deleted": "NO", "platform": self.platform,
                "psn_user_id": {"account_id": "", "online_id": "", "is_valid_cache": "NO"}, "xuid": "", "steamid": "",
                "nsw_user_id": {"nsa_id": "", "issuer_id": 0},
                "avatar": {"is_already_set": "NO", "player_id": 0, "special_id": 0, "background_id": 0,
                           "special_id_background": 0, "is_enable_player": "NO", "is_enable_background": "NO",
                           "material_key_player": "", "material_key_background": ""},
                "fameIdList": [], "user_banner": {"banner_id": {"unique_id": 0}, "is_valid": "NO"}}


class Member:
    def __init__(self, user):
        self.user = user
        self.ready = False
        self.side = "RANDOM"
        self.side_leader = False
        self.squad = True
        self.comment = 0
        self.addr4 = None
        self.addr6 = None
        self.event_account_type = ""
        self.strike = {"player_id": 0, "costume_id": 0}


CORE_KEYS = ("mode", "event_id", "kind", "name_tag", "entry_restriction", "password", "lang", "capacity",
             "observer_capacity", "guest_num_by_user", "is_coop_vs_com_single")
ENV_KEYS = ("condition_home", "condition_away", "cpu_level", "match_time", "injury", "ball_type", "exTime", "pk",
            "substitution_times", "substitution", "exSubstitution", "limitTime", "equalization", "regulation",
            "can_use_mobile_controller")


class Room:
    def __init__(self, rid, owner, core, env):
        self.id, self.owner, self.core, self.env = rid, owner, core, env
        self.created = int(time.time())
        self.members = []
        self.kicked = []
        self.go_match = False
        self.go_users = []
        self.game_id = 0
        self.results = {}           # uid -> SET_GAME_RESULT request
        self.scores = []            # ADD_SCORE events
        self.phase = "DATASYNC"

    def member(self, uid):
        for m in self.members:
            if m.user.uid == uid:
                return m
        return None

    def info(self):
        users = []
        for m in self.members:
            users.append({"user_info": m.user.info(), "is_match_ready": yn(m.ready), "is_guest": "NO",
                          "guest_info": {"serial_number": 0, "is_platform_guest": "NO"}, "entry_side": m.side,
                          "is_entry_side_leader": yn(m.side_leader), "is_squad_enable": yn(m.squad),
                          "comment_id": m.comment, "event_account_type": m.event_account_type,
                          "strike_arena_selected_info": m.strike, "is_enable_cross_platform": "NO"})
        return {"room_id": {"id": self.id}, "room_create_time": self.created, "owner_info": self.owner.info(),
                "room_core_settings": self.core, "room_match_settings": {"match_env": self.env},
                "num_users": len(self.members), "user_list": {"room_users": users}, "force_proceed_time": 0,
                "is_go_match": yn(self.go_match), "go_match_users": self.go_users,
                "is_all_squad_enable": yn(all(m.squad for m in self.members)), "gamephase": self.phase,
                "kicked_users": self.kicked, "platform_session_id_info": {"ps_session_id": "", "xb_session_id": ""},
                "expire_time": self.created + 6 * 3600, "coop_room_end_time": 0}

    def capacity(self):
        try:
            return 2  # v1.1 implements one versus one; co-op is not implemented
        except Exception:
            return 2

    def check_go(self):
        """All members ready (at least two): decide sides and start the match."""
        if self.go_match or len(self.members) < 2 or not all(m.ready for m in self.members):
            return
        home = [m for m in self.members if m.side == "HOME"]
        away = [m for m in self.members if m.side == "AWAY"]
        for m in self.members:
            if m.side == "RANDOM" or m.side not in ("HOME", "AWAY"):
                (home if len(home) <= len(away) else away).append(m)
        if not away and len(home) > 1:
            away.append(home.pop())
        if not home and len(away) > 1:
            home.append(away.pop())
        self.go_users = []
        for side, lst in (("HOME", home), ("AWAY", away)):
            for i, m in enumerate(lst):
                self.go_users.append({"user_id": m.user.uid, "side": side, "is_side_leader": yn(i == 0)})
        self.go_match = True
        self.game_id = random.randint(10 ** 11, 10 ** 12 - 1)
        self.results, self.scores, self.phase = {}, [], "DATASYNC"
        for m in self.members:
            m.user.turn_inbox.clear()
            m.user.session_check = None
        log({"event": "go_match", "room": self.id, "users": self.go_users, "game_id": self.game_id})


USERS, ROOMS = {}, {}


def yn(b):
    return "YES" if b else "NO"


def get_user(uid, name, platform):
    with LOCK:
        u = USERS.get(uid)
        if not u:
            u = USERS[uid] = User(uid, name or "Player%d" % (uid % 10000), platform)
        if name:
            u.name = name
        u.seen = time.time()
        return u


def room_id_from(req):
    r = req.get("room_id")
    if isinstance(r, dict):
        r = r.get("id")
    if r is None:
        r = req.get("id")
    try:
        return int(r)
    except Exception:
        return None


def leave(u):
    r = u.room
    if not r:
        return
    m = r.member(u.uid)
    if m:
        r.members.remove(m)
    u.room = None
    if not r.members or r.owner is u:
        for m in list(r.members):
            m.user.room = None
        ROOMS.pop(r.id, None)
        log({"event": "room_closed", "room": r.id})
    elif r.go_match and len(r.members) < 2:
        r.go_match = False


def new_room_id():
    while True:
        rid = random.randint(100000, 999999)
        if rid not in ROOMS:
            return rid


# ------------------------------------------------------------------ commands
def ok(**kw):
    d = {"result": "NOERR"}
    d.update(kw)
    return d


def err(code):
    return {"result": code}


def h_createjoin_room(u, req):
    leave(u)
    core = dict(req.get("core_settings") or {k: req[k] for k in CORE_KEYS if k in req})
    ms = req.get("match_settings") or {}
    env = dict(ms.get("match_env") if isinstance(ms, dict) and "match_env" in ms else ms or {k: req[k] for k in ENV_KEYS if k in req})
    r = Room(new_room_id(), u, core, env)
    m = Member(u); m.side_leader = True
    m.addr4, m.addr6 = req.get("address_ipv4"), req.get("address_ipv6")
    m.event_account_type = req.get("event_account_type", "") or ""
    if isinstance(req.get("strike_arena_selected_info"), dict):
        m.strike = req["strike_arena_selected_info"]
    r.members.append(m); ROOMS[r.id] = r; u.room = r
    log({"event": "room_created", "room": r.id, "owner": u.uid, "core": core})
    return ok(room_id={"id": r.id})


def h_join_room(u, req):
    rid = room_id_from(req)
    r = ROOMS.get(rid)
    if not r:
        return err("ERR_ROOM_NOT_FOUND")
    if u.uid in r.kicked:
        return err("ERR_ALREADY_KICKED")
    if r.member(u.uid):
        return ok()
    if r.core.get("entry_restriction") == "PASSWORD" and (req.get("password") or "") != (r.core.get("password") or ""):
        return err("ERR_PASSWD_INCORRECT")
    if len(r.members) >= r.capacity():
        return err("ERR_FAIL_FULL")
    leave(u)
    m = Member(u)
    m.addr4, m.addr6 = req.get("address_ipv4"), req.get("address_ipv6")
    m.event_account_type = req.get("event_account_type", "") or ""
    r.members.append(m); u.room = r
    log({"event": "room_joined", "room": r.id, "user": u.uid})
    return ok()


def h_get_room_info(u, req):
    rid = room_id_from(req)
    r = ROOMS.get(rid) if rid else u.room
    if not r:
        return err("ERR_ROOM_NOT_FOUND")
    if not r.member(u.uid):
        return err("ERR_NOT_IN_ROOM")
    return ok(room_info=r.info())


def h_get_room_list(u, req):
    rooms = [r for r in ROOMS.values() if not r.go_match and len(r.members) < r.capacity()
             and r.core.get("entry_restriction") != "INVITE"]
    mode = req.get("mode")
    if mode:
        rooms = [r for r in rooms if r.core.get("mode") in (None, mode)]
    out = []
    for r in rooms[:30]:
        i = r.info(); i.pop("owner_info", None)
        i["room_core_settings"] = {k: v for k, v in r.core.items() if k != "password"}
        out.append(i)
    return ok(room_info=out)


def member_or_err(u, req):
    r = u.room
    rid = room_id_from(req)
    if rid and rid in ROOMS:
        r = ROOMS[rid]
    m = r.member(u.uid) if r else None
    return r, m


def h_leave_room(u, req):
    leave(u)
    return ok()


def h_set_ready(u, req, ready=True):
    r, m = member_or_err(u, req)
    if not m:
        return err("ERR_NOT_IN_ROOM")
    m.ready = ready
    if req.get("address_ipv4"):
        m.addr4 = req["address_ipv4"]
    if req.get("address_ipv6"):
        m.addr6 = req["address_ipv6"]
    if ready:
        r.check_go()
    elif not r.go_match:
        pass
    return ok()


def h_set_room_settings(u, req):
    r, m = member_or_err(u, req)
    if not m or r.owner is not u:
        return err("ERR_NOT_IN_ROOM")
    core = req.get("room_core_settings") or {k: req[k] for k in CORE_KEYS if k in req}
    env = req.get("match_env") or {k: req[k] for k in ENV_KEYS if k in req}
    if core:
        r.core.update(core)
    if env:
        r.env.update(env)
    return ok()


def h_entry_side(u, req):
    r, m = member_or_err(u, req)
    if not m:
        return err("ERR_NOT_IN_ROOM")
    m.side = req.get("entry_side") or m.side
    if "is_entry_side_leader" in req:
        m.side_leader = req.get("is_entry_side_leader") == "YES"
    return ok()


def h_squad_enable(u, req):
    r, m = member_or_err(u, req)
    if not m:
        return err("ERR_NOT_IN_ROOM")
    m.squad = req.get("is_squad_enable", "YES") == "YES"
    return ok()


def h_comment(u, req):
    r, m = member_or_err(u, req)
    if m:
        try:
            m.comment = int(req.get("comment_id") or 0)
        except Exception:
            pass
    return ok()


def h_kick(u, req):
    r, m = member_or_err(u, req)
    if not r or r.owner is not u:
        return err("ERR_NOT_IN_ROOM")
    kid = req.get("kick_user_id")
    if kid == u.uid:
        return err("ERR_NOT_IN_ROOM")
    t = r.member(kid)
    if t:
        r.members.remove(t); t.user.room = None; r.kicked.append(kid)
    return ok()


def h_return_room(u, req):
    r, m = member_or_err(u, req)
    if not m:
        return err("ERR_NOT_IN_ROOM")
    m.ready = False
    if r.go_match:
        r.go_match = False; r.go_users = []
        for member in r.members:
            member.ready = False
    return ok()


def h_get_game_session(u, req):
    r = u.room
    rid = room_id_from(req)
    if rid in ROOMS:
        r = ROOMS[rid]
    if not r or not r.member(u.uid):
        return err("ERR_ROOM_NOT_FOUND")
    lst = []
    for m in r.members:
        e = {"user_id": m.user.uid}
        if m.addr4:
            e["address_ipv4"] = m.addr4
        if m.addr6:
            e["address_ipv6"] = m.addr6
        lst.append(e)
    now = int(time.time())
    # No Konami game relay: matches run peer to peer (GameSycom P2P), with TURN as the fallback path.
    return ok(gamerelay={"name": "", "region": "", "ip_addr": "", "port": 0}, game_sycom="P2P", ntl_address_list=lst,
              ping=0, connecting_start_time=now, connecting_retry_time=0)


def turn_credentials(uid):
    exp = int(time.time()) + 6 * 3600
    user = "%d:%d" % (exp, uid)
    pw = base64.b64encode(hmac.new(ARGS.turn_secret.encode(), user.encode(), hashlib.sha1).digest()).decode()
    return user, pw


def h_get_turn_server_list(u, req):
    if not ARGS.turn_host:
        return ok(turn_server_list=[])
    user, pw = turn_credentials(u.uid)
    return ok(turn_server_list=[{"zone": ARGS.turn_zone, "address": ARGS.turn_host, "port": ARGS.turn_port,
                                 "tls_port": ARGS.turn_tls_port, "turn_account": {"id": user, "pw": pw, "origin": ""},
                                 "server_certificate": "", "server_certificate_data_size": 0}])


def h_send_turn_address(u, req):
    target = req.get("target_user_id")
    t = USERS.get(target)
    if not t or not u.room or t.room is not u.room or t is u:
        return err("ERR_NOTFOUND")
    t.turn_inbox.append({"from": u.uid, "address": req.get("address", ""), "address_data_size": req.get("address_data_size", 0)})
    return ok()


def h_get_turn_address(u, req):
    if not u.turn_inbox:
        return ok(address="", address_data_size=0)
    a = u.turn_inbox.pop(0)
    return ok(address=a["address"], address_data_size=a["address_data_size"])


def h_game_progress(u, req):
    r = u.room
    if r:
        if "phase" in req:
            r.phase = req.get("phase") or r.phase
        if req.get("msgid") == "CMD_ADD_SCORE":
            r.scores.append(req)
    return ok()


def h_set_game_result(u, req):
    r = u.room
    if r:
        r.results[u.uid] = req
    return ok()


def side_of(r, uid):
    for g in r.go_users:
        if g["user_id"] == uid:
            return g["side"]
    return "HOME"


def h_get_game_result(u, req):
    r = u.room
    if not r:
        return err("ERR_GAME_RESULT_NOT_FOUND")
    home = away = 0
    for s in r.scores:
        if s.get("goal_side") == "AWAY":
            away += 1
        else:
            home += 1
    for res in r.results.values():  # prefer the clients' own final score when reported
        mr = res.get("match_result")
        if isinstance(mr, dict) and "home_score" in mr:
            home, away = mr.get("home_score", home), mr.get("away_score", away)
    me = side_of(r, u.uid)
    mine, theirs = (home, away) if me == "HOME" else (away, home)
    res = "WIN" if mine > theirs else "LOSE" if mine < theirs else "DRAW"
    players = [{"user_id": g["user_id"], "rating": 0, "oldrating": 0, "rating_boost_type": "", "rating_boost_rate": 0,
                "team_id": 0, "side": g["side"], "cont_win_num": 0} for g in r.go_users]
    return ok(get_match_result="YES", game_end_result={
        "match_result": {"gameend_reason": "NORMAL_END", "abnormalend_reason": "NOERR", "player_data": players,
                         "game_res": res, "score_data": {"home_score": home, "away_score": away}}})


def h_session_check(u, req):
    u.session_check = req
    return ok()


def h_get_session_check(u, req):
    return ok(is_check_finished="YES", error_code="NOERR")


HANDLERS = {
    "CMD_CREATEJOIN_ROOM": h_createjoin_room, "CMD_JOIN_ROOM": h_join_room, "CMD_GET_ROOM_INFO": h_get_room_info,
    "CMD_GET_ROOM_LIST": h_get_room_list, "CMD_GET_COMMUNITY_ROOM_LIST": h_get_room_list,
    "CMD_LEAVE_ROOM": h_leave_room, "CMD_SET_ROOM_MATCH_READY": lambda u, q: h_set_ready(u, q, True),
    "CMD_UNSET_ROOM_MATCH_READY": lambda u, q: h_set_ready(u, q, False), "CMD_SET_ROOM_SETTINGS": h_set_room_settings,
    "CMD_SET_ROOM_USER_ENTRY_SIDE": h_entry_side, "CMD_SET_ROOM_USER_SQUAD_ENABLE": h_squad_enable,
    "CMD_SET_ROOM_USER_COMMENT": h_comment, "CMD_KICK_ROOM_USER": h_kick, "CMD_RETURN_ROOM": h_return_room,
    "CMD_GET_GAME_SESSION": h_get_game_session, "CMD_GET_TURN_SERVER_LIST": h_get_turn_server_list,
    "CMD_SEND_TURN_ADDRESS_DATA": h_send_turn_address, "CMD_GET_TURN_ADDRESS_DATA": h_get_turn_address,
    "CMD_WATCH_TURN_ADDRESS_DATA": h_get_turn_address,
    "CMD_CHANGE_GAMEPHASE": h_game_progress, "CMD_ADD_SCORE": h_game_progress, "CMD_ADD_FOUL": h_game_progress,
    "CMD_SET_GAME_RESULT": h_set_game_result, "CMD_GET_GAME_RESULT": h_get_game_result,
    "CMD_CHECK_GAME_RESULT": h_get_game_result, "CMD_SET_GAME_SESSION_CHECK_RES": h_session_check,
    "CMD_GET_GAME_SESSION_CHECK_RES": h_get_session_check,
    "CMD_GET_REQUESTED_JOIN_ROOM_INFO": lambda u, q: ok(requested_join_room_info={}),
    "CMD_GET_SESSION_ID": lambda u, q: ok(ps_session_id="", xb_session_id=""),
}


def cleanup():
    while True:
        time.sleep(60)
        with LOCK:
            now = time.time()
            for u in list(USERS.values()):
                if now - u.seen > 900:
                    leave(u)
                    USERS.pop(u.uid, None)


# ------------------------------------------------------------------ http
class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, *a):
        pass

    def reply(self, code, body, ctype="application/x-msgpack", headers=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/health"):
            return self.reply(200, json.dumps({"ok": True, "version": VERSION, "users": len(USERS), "rooms": len(ROOMS)}).encode(), "application/json")
        path = urllib.parse.urlsplit(self.path)
        supplied = self.headers.get("Authorization", "").removeprefix("Bearer ")
        if path.path == "/admin/state" and ARGS.admin_token and hmac.compare_digest(supplied, ARGS.admin_token):
            with LOCK:
                st = {"rooms": [r.info() for r in ROOMS.values()],
                      "users": [{"uid": u.uid, "name": u.name, "room": u.room.id if u.room else None} for u in USERS.values()]}
            return self.reply(200, json.dumps(st, ensure_ascii=False, default=str).encode(), "application/json")
        self.reply(404, b"", "text/plain")

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            return self.reply(400, b"", "text/plain")
        if n < 0 or n > MAX_REQUEST_BYTES:
            return self.reply(413, b"", "text/plain")
        body = self.rfile.read(n) if n else b""
        token = getattr(ARGS, "access_token", "")
        if token and not hmac.compare_digest(self.headers.get("X-Ziru-Token", ""), token):
            return self.reply(403, b"", "text/plain")
        path = urllib.parse.urlparse(self.path).path
        cmd = path.rsplit("/", 1)[-1].replace("gate_", "").replace(".php", "")
        try:
            uid = int(self.headers.get("X-Ziru-Uid") or 0)
        except ValueError:
            uid = 0
        if not 0 < uid <= 0xffffffff or uid == LOCAL_UID:
            return self.reply(403, b"", "text/plain")
        name = urllib.parse.unquote(self.headers.get("X-Ziru-Name") or "")
        try:
            req = dec_request(body)
        except Exception as e:
            log({"cmd": cmd, "uid": uid, "error": "decode: %s" % e})
            return self.reply(400, b"", "text/plain")
        req = remap(req, LOCAL_UID, uid)
        if not isinstance(req.get("msgid", cmd), str):
            return self.reply(400, b"", "text/plain")
        with LOCK:
            u = get_user(uid, name[:80], self.headers.get("X-Ziru-Platform") or "IOS")
            msgid = req.get("msgid") or cmd
            h = HANDLERS.get(msgid) or HANDLERS.get(cmd)
            try:
                res = h(u, req) if h else err("ERR_NOTFOUND")
            except Exception as e:
                log({"cmd": cmd, "uid": uid, "error": repr(e)})
                res = {"result": "ERR_NOTFOUND"}
            in_room = u.room is not None and u.room.member(uid) is not None
            res.setdefault("result", "NOERR")
            res["msgid"] = msgid
            if "rqid" in req:
                res["rqid"] = req["rqid"]
            log({"cmd": msgid, "uid": uid, "name": name, "handled": bool(h), "req": req, "res": res})
            wire_response = remap(res, uid, LOCAL_UID)
        self.reply(200, enc_response(wire_response), headers={"X-Ziru-In-Room": yn(in_room)})


def main():
    global ARGS, LOGF
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8443)
    ap.add_argument("--cert", default="")
    ap.add_argument("--key", default="")
    ap.add_argument("--turn-host", default="")
    ap.add_argument("--turn-port", type=int, default=3478)
    ap.add_argument("--turn-tls-port", type=int, default=5349)
    ap.add_argument("--turn-secret", default=os.environ.get("PES2027_TURN_SECRET", ""))
    ap.add_argument("--turn-zone", default="ap-southeast-1")
    ap.add_argument("--admin-token", default=os.environ.get("PES2027_ADMIN_TOKEN", ""))
    ap.add_argument("--access-token", default=os.environ.get("PES2027_ACCESS_TOKEN", ""))
    ap.add_argument("--log-dir", default="logs")
    ap.add_argument("--verbose", action="store_true")
    ARGS = ap.parse_args()
    os.makedirs(ARGS.log_dir, exist_ok=True)
    LOGF = open(os.path.join(ARGS.log_dir, time.strftime("online-%Y%m%d.jsonl")), "a", encoding="utf-8")
    srv = ThreadingHTTPServer((ARGS.host, ARGS.port), Handler)
    if ARGS.cert:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(ARGS.cert, ARGS.key)
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    threading.Thread(target=cleanup, daemon=True).start()
    print("PES2027 online server %s on %s:%d (%s)" % (VERSION, ARGS.host, ARGS.port, "https" if ARGS.cert else "http"))
    srv.serve_forever()


if __name__ == "__main__":
    main()
