#!/usr/bin/env python3
"""Reads ZIRU_MATCH_SETTINGS.json and writes weather/time_zone into every
CMD_GET_SEASON_INFO response (get_season_info.json, ZIRU_ML_SEASON_INFO_F*.json).
Run again any time you change the settings."""
import json, glob, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
cfg = json.load(open('ZIRU_MATCH_SETTINGS.json'))
if not cfg.get('enabled', True):
    raise SystemExit('Hook disabled in ZIRU_MATCH_SETTINGS.json')
good = cfg.get('_known_good_values', {})
for k in ('weather', 'time_zone'):
    if cfg[k] not in good.get(k, []):
        print(f'WARNING: {cfg[k]} not seen in dumps; untested')
for f in ['get_season_info.json'] + sorted(glob.glob('ZIRU_ML_SEASON_INFO_F*.json')):
    d = json.load(open(f))
    if d.get('msgid') != 'CMD_GET_SEASON_INFO':
        continue
    d['weather'], d['time_zone'] = cfg['weather'], cfg['time_zone']
    json.dump(d, open(f, 'w'), separators=(',', ':'))
    print('patched', f)
