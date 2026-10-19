"""Check a deployed private endpoint and authenticated UDP TURN allocation.

Uses two simulated clients, not the game's native networking implementation.
The private JSON is read locally; neither tokens nor TURN passwords are printed.
"""
import argparse
import hashlib
import hmac
import http.client
import json
import os
import secrets
import socket
import ssl
import struct
import urllib.parse

from test_online_server import encode_request, decode_response

MAGIC = 0x2112A442


def attribute(kind, value):
    return struct.pack('!HH', kind, len(value)) + value + b'\0' * (-len(value) % 4)


def parse_attributes(packet):
    result = {}
    pos = 20
    while pos + 4 <= len(packet):
        kind, size = struct.unpack_from('!HH', packet, pos)
        result[kind] = packet[pos + 4:pos + 4 + size]
        pos += 4 + size + (-size % 4)
    return result


def check_turn(entry):
    account = entry['turn_account']
    address = (entry['address'], entry['port'])
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.settimeout(8)
    nonce = realm = None
    try:
        for authenticated in [False, True]:
            transaction = os.urandom(12)
            attrs = attribute(0x0019, b'\x11\0\0\0')
            if authenticated:
                attrs += attribute(0x0006, account['id'].encode())
                attrs += attribute(0x0014, realm) + attribute(0x0015, nonce)
                key = hashlib.md5(account['id'].encode() + b':' + realm + b':' + account['pw'].encode()).digest()
                header = struct.pack('!HHI12s', 0x0003, len(attrs) + 24, MAGIC, transaction)
                attrs += attribute(0x0008, hmac.new(key, header + attrs, hashlib.sha1).digest())
            packet = struct.pack('!HHI12s', 0x0003, len(attrs), MAGIC, transaction) + attrs
            udp.sendto(packet, address)
            answer, _ = udp.recvfrom(4096)
            if answer[8:20] != transaction:
                raise RuntimeError('TURN transaction mismatch')
            kind = struct.unpack_from('!H', answer)[0]
            fields = parse_attributes(answer)
            if not authenticated:
                if kind != 0x0113 or 0x0014 not in fields or 0x0015 not in fields:
                    raise RuntimeError('TURN did not issue its authentication challenge')
                realm, nonce = fields[0x0014], fields[0x0015]
            else:
                if kind != 0x0103 or 0x0016 not in fields:
                    raise RuntimeError('Authenticated TURN allocation failed')
                relay = fields[0x0016]
                port = struct.unpack_from('!H', relay, 2)[0] ^ (MAGIC >> 16)
                ip = socket.inet_ntoa(bytes(a ^ b for a, b in zip(relay[4:8], struct.pack('!I', MAGIC))))
                # Release the temporary allocation immediately (Refresh lifetime = zero).
                attrs = attribute(0x000D, b'\0' * 4) + attribute(0x0006, account['id'].encode())
                attrs += attribute(0x0014, realm) + attribute(0x0015, nonce)
                tx = os.urandom(12)
                head = struct.pack('!HHI12s', 0x0004, len(attrs) + 24, MAGIC, tx)
                attrs += attribute(0x0008, hmac.new(key, head + attrs, hashlib.sha1).digest())
                udp.sendto(struct.pack('!HHI12s', 0x0004, len(attrs), MAGIC, tx) + attrs, address)
                udp.recvfrom(4096)
                return {'authenticated_allocation': True, 'relay_ip': ip, 'relay_port': port}
    finally:
        udp.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    with open(args.config, encoding='utf-8') as f:
        cfg = json.load(f)
    target = urllib.parse.urlsplit(cfg['server'])
    ids = [2000000000 + secrets.randbelow(500000000) for _ in range(3)]
    sequence = 0

    def call(uid, command, **fields):
        nonlocal sequence
        sequence += 1
        conn = http.client.HTTPSConnection(target.hostname, target.port or 443,
                                          context=ssl._create_unverified_context(), timeout=10)
        try:
            conn.connect()
            pin = hashlib.sha256(conn.sock.getpeercert(binary_form=True)).hexdigest()
            if pin != cfg['cert_sha256']:
                raise RuntimeError('Server certificate fingerprint mismatch')
            conn.request('POST', '/pes22/gate/gate_' + command + '.php',
                         encode_request({'msgid': command, 'rqid': sequence, **fields}),
                         {'X-Ziru-Uid': str(uid), 'X-Ziru-Token': cfg['token'], 'X-Ziru-Platform': 'IOS'})
            reply = conn.getresponse()
            if reply.status != 200:
                raise RuntimeError('Unexpected HTTP status ' + str(reply.status))
            result = decode_response(reply.read())
            if result.get('rqid') != sequence:
                raise RuntimeError('Request sequence mismatch')
            return result, reply.getheader('X-Ziru-In-Room')
        finally:
            conn.close()

    evidence = {'server': cfg['server'], 'native_iphone_match_tested': False}
    try:
        room, _ = call(ids[0], 'CMD_CREATEJOIN_ROOM', core_settings={'entry_restriction': 'PASSWORD', 'password': 'smoke'})
        rid = room['room_id']['id']
        rejected, state = call(ids[1], 'CMD_JOIN_ROOM', room_id=rid, password='incorrect')
        assert rejected['result'] == 'ERR_PASSWD_INCORRECT' and state == 'NO'
        joined, state = call(ids[1], 'CMD_JOIN_ROOM', room_id=rid, password='smoke')
        assert joined['result'] == 'NOERR' and state == 'YES'
        outsider, _ = call(ids[2], 'CMD_GET_GAME_SESSION', room_id=rid)
        assert outsider['result'] != 'NOERR'
        for uid, side in zip(ids[:2], ['HOME', 'AWAY']):
            call(uid, 'CMD_SET_ROOM_USER_ENTRY_SIDE', entry_side=side)
            call(uid, 'CMD_SET_ROOM_MATCH_READY', address_ipv4={'ip': '198.51.100.1', 'port': 9000})
        info, _ = call(ids[0], 'CMD_GET_ROOM_INFO')
        assert info['room_info']['is_go_match'] == 'YES'
        peers = {x['user_info']['user_id'] for x in info['room_info']['user_list']['room_users']}
        assert peers == {359042717, ids[1]}
        session, _ = call(ids[0], 'CMD_GET_GAME_SESSION')
        assert len(session['ntl_address_list']) == 2
        call(ids[0], 'CMD_SEND_TURN_ADDRESS_DATA', target_user_id=ids[1], address='smoke', address_data_size=5)
        received, _ = call(ids[1], 'CMD_GET_TURN_ADDRESS_DATA')
        assert received['address'] == 'smoke'
        turn, _ = call(ids[0], 'CMD_GET_TURN_SERVER_LIST')
        evidence['turn'] = check_turn(turn['turn_server_list'][0])
        evidence['encrypted_two_client_room_flow'] = True
        evidence['room_access_isolated'] = True
        evidence['certificate_pin_matches'] = True
    finally:
        for uid in ids:
            try:
                call(uid, 'CMD_LEAVE_ROOM')
            except Exception:
                pass
    with open(args.output, 'w', encoding='utf-8') as f:
        json.dump(evidence, f, indent=2)
    print(json.dumps(evidence))


if __name__ == '__main__':
    main()
