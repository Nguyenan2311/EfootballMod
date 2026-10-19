"""Encrypted HTTP integration tests with independent simulated iPhone identities."""
import base64
import gzip
import hashlib
import hmac
import http.client
import os
import threading
import types
import unittest
import zlib

import msgpack
from cryptography.hazmat.primitives import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

import pes_online_server as server


def encode_request(obj):
    raw = zlib.compress(msgpack.packb(obj, use_bin_type=True))
    padder = padding.PKCS7(128).padder()
    raw = padder.update(raw) + padder.finalize()
    iv = os.urandom(16)
    enc = Cipher(algorithms.AES(server.KEY), modes.CBC(iv)).encryptor()
    return iv + enc.update(raw) + enc.finalize()


def decode_response(body):
    dec = Cipher(algorithms.AES(server.KEY), modes.CBC(body[:16])).decryptor()
    raw = dec.update(body[16:]) + dec.finalize()
    unpadder = padding.PKCS7(128).unpadder()
    raw = unpadder.update(raw) + unpadder.finalize()
    return msgpack.unpackb(gzip.decompress(raw), raw=False)


class OnlineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        server.ARGS = types.SimpleNamespace(access_token="test-deployment", admin_token="admin",
                                            turn_host="turn.example.test", turn_secret="test-turn-secret",
                                            turn_zone="ap-southeast-1", turn_port=3478, turn_tls_port=5349,
                                            verbose=False)
        cls.httpd = server.ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
        cls.worker = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.worker.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.worker.join()

    def setUp(self):
        with server.LOCK:
            server.USERS.clear()
            server.ROOMS.clear()

    def call(self, uid, cmd, token="test-deployment", payload=None, **fields):
        request = {"msgid": cmd, "rqid": 42, **fields} if payload is None else payload
        body = encode_request(request)
        headers = {"X-Ziru-Uid": str(uid), "X-Ziru-Token": token, "X-Ziru-Name": "Player",
                   "Content-Type": "application/x-msgpack"}
        conn = http.client.HTTPConnection(*self.httpd.server_address, timeout=5)
        try:
            conn.request("POST", "/pes22/gate/gate_" + cmd + ".php", body, headers)
            response = conn.getresponse()
            raw = response.read()
            return response.status, dict(response.getheaders()), decode_response(raw) if response.status == 200 else None
        finally:
            conn.close()

    def create(self, uid=1000000001, **kw):
        status, headers, result = self.call(uid, "CMD_CREATEJOIN_ROOM", **kw)
        self.assertEqual(status, 200)
        self.assertEqual(headers["X-Ziru-In-Room"], "YES")
        return result["room_id"]["id"]

    def test_two_clients_ready_session_turn_result_and_return(self):
        a, b = 1000000001, 1000000002
        rid = self.create(a)
        self.assertTrue(100000 <= rid <= 999999)
        self.assertEqual(self.call(b, "CMD_JOIN_ROOM", room_id={"id": rid})[2]["result"], "NOERR")
        for uid, own in [(a, b), (b, a)]:
            info = self.call(uid, "CMD_GET_ROOM_INFO")[2]["room_info"]
            ids = {u["user_info"]["user_id"] for u in info["user_list"]["room_users"]}
            self.assertEqual(ids, {server.LOCAL_UID, own})
            self.assertEqual(info["room_core_settings"].get("password"), None)
            self.call(uid, "CMD_SET_ROOM_USER_ENTRY_SIDE", entry_side="AWAY")
            self.call(uid, "CMD_SET_ROOM_MATCH_READY", address_ipv4={"ip": "198.51.100.1", "port": 5000})
        info = self.call(a, "CMD_GET_ROOM_INFO")[2]["room_info"]
        self.assertEqual(info["is_go_match"], "YES")
        self.assertEqual({u["side"] for u in info["go_match_users"]}, {"HOME", "AWAY"})
        session = self.call(a, "CMD_GET_GAME_SESSION")[2]
        self.assertEqual(len(session["ntl_address_list"]), 2)
        self.assertEqual(session["game_sycom"], "P2P")
        self.assertEqual(session["rqid"], 42)
        self.call(a, "CMD_SEND_TURN_ADDRESS_DATA", target_user_id=b, address="peer-blob", address_data_size=9)
        self.assertEqual(self.call(b, "CMD_GET_TURN_ADDRESS_DATA")[2]["address"], "peer-blob")
        self.assertEqual(self.call(b, "CMD_GET_TURN_ADDRESS_DATA")[2]["address_data_size"], 0)
        account = self.call(a, "CMD_GET_TURN_SERVER_LIST")[2]["turn_server_list"][0]["turn_account"]
        expected = base64.b64encode(hmac.new(b"test-turn-secret", account["id"].encode(), hashlib.sha1).digest()).decode()
        self.assertEqual(account["pw"], expected)
        self.call(a, "CMD_SET_GAME_RESULT", match_result={"home_score": 2, "away_score": 1})
        results = [self.call(uid, "CMD_GET_GAME_RESULT")[2]["game_end_result"]["match_result"]["game_res"] for uid in [a, b]]
        self.assertEqual(set(results), {"WIN", "LOSE"})
        self.call(a, "CMD_RETURN_ROOM")
        self.assertFalse(any(m.ready for m in server.ROOMS[rid].members))

    def test_failed_join_does_not_claim_room_membership(self):
        rid = self.create(core_settings={"entry_restriction": "PASSWORD", "password": "secret"})
        status, headers, result = self.call(1000000002, "CMD_JOIN_ROOM", room_id=rid, password="wrong")
        self.assertEqual(status, 200)
        self.assertEqual(headers["X-Ziru-In-Room"], "NO")
        self.assertEqual(result["result"], "ERR_PASSWD_INCORRECT")
        listed = self.call(1000000002, "CMD_GET_ROOM_LIST")[2]["room_info"]
        self.assertNotIn("password", listed[0]["room_core_settings"])

    def test_room_access_and_turn_exchange_are_isolated(self):
        a, b, outsider = 1000000001, 1000000002, 1000000003
        rid = self.create(a)
        self.call(b, "CMD_JOIN_ROOM", room_id=rid)
        self.assertNotEqual(self.call(outsider, "CMD_GET_ROOM_INFO", room_id=rid)[2]["result"], "NOERR")
        self.assertNotEqual(self.call(outsider, "CMD_GET_GAME_SESSION", room_id=rid)[2]["result"], "NOERR")
        self.assertNotEqual(self.call(b, "CMD_SET_ROOM_SETTINGS", match_time=99)[2]["result"], "NOERR")
        self.assertNotEqual(self.call(outsider, "CMD_SEND_TURN_ADDRESS_DATA", target_user_id=b)[2]["result"], "NOERR")
        self.assertNotEqual(self.call(a, "CMD_KICK_ROOM_USER", kick_user_id=a)[2]["result"], "NOERR")
        self.call(a, "CMD_LEAVE_ROOM")
        self.assertEqual(self.call(b, "CMD_GET_ROOM_INFO")[1]["X-Ziru-In-Room"], "NO")

    def test_token_malformed_body_and_unknown_command(self):
        self.assertEqual(self.call(1000000001, "CMD_GET_TURN_SERVER_LIST", token="")[0], 403)
        self.assertEqual(self.call(1000000001, "CMD_GET_ROOM_LIST", payload=[])[0], 400)
        self.assertEqual(self.call(server.LOCAL_UID, "CMD_GET_ROOM_LIST")[0], 403)
        self.assertNotEqual(self.call(1000000001, "CMD_NOT_IMPLEMENTED")[2]["result"], "NOERR")


if __name__ == "__main__":
    unittest.main()
