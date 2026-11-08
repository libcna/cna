#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Check HND-XACT-02 native observations without inventing hidden timing or PCM."""
import argparse
import importlib.util
import json
from pathlib import Path
import re

REPO = Path(__file__).resolve().parents[2]
CORPUS = REPO / 'modules/audio/tests/CNA/Internal/Audio/variation-prefetch'
_spec = importlib.util.spec_from_file_location('preparation', Path(__file__).with_name('check-xact-preparation-shapes-oracle.py'))
preparation = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(preparation)
require, digest, DLLS = preparation.require, preparation.digest, preparation.DLLS


def cell_id(r):
    return f"{r['arch']}/{r['profile']}/{r['mode']}/{r['lookahead']}/io{int(r['io'])}/{r['repeat']}"


def expected_cells():
    cells = set()
    for arch in DLLS:
        cells.update({(arch, 'old-resident', 'keep', 250, False, 0), (arch, 'resident', 'normal', 250, False, 0)})
        for profile in ('samebank', 'ordered', 'ordered-streaming'):
            for ahead in (50, 250, 500):
                for repeat in range(3): cells.add((arch, profile, 'normal', ahead, False, repeat))
                if profile == 'ordered-streaming': cells.add((arch, profile, 'normal', ahead, True, 0))
            for mode in ('no-props', 'missingB', 'lateB', 'lateB-playing'):
                cells.add((arch, profile, mode, 250, False, 0))
    return cells


def check_row(r):
    label = cell_id(r)
    require(r['rc'] == 0, label + ': incomplete capture')
    require(r['dll_sha256'] == DLLS[r['arch']], label + ': wrong engine')
    require(r['xact_load_events'] and all(e['implementation'] == 'native' for e in r['xact_load_events']), label + ': native image unestablished')
    if r['profile'] in ('old-resident', 'resident'):
        preparation.check_row(r)
        return
    records = r['records']; mode = r['mode']; same = r['profile'] == 'samebank'
    snaps = {s['snapshot']: s for s in records if 'snapshot' in s}
    calls = {s['call']: s for s in records if 'call' in s}
    for name in ('prepare', 'play'): require(calls[name]['hr'] == '00000000', label + ': refused ' + name)
    absent = mode in ('missingB', 'lateB', 'lateB-playing')
    initial = [4, 0 if absent else 4, 0]
    require(snaps['initial']['bank_states'] == initial, label + ': initial banks')
    require(snaps['prepared_300']['bank_states'] == [132, initial[1], 0], label + ': successor resolved before Play')
    require(snaps['prepared_300']['cue_states'][0] == 4 and snaps['prepared_300']['sound_states'][0] == 128, label + ': preparation state')
    notifications = [s for s in records if 'notification' in s]
    prepared, played, stopped, destroyed = ([s for s in notifications if s['notification'] == n] for n in (12, 13, 14, 16))
    failed_successor = not same and mode in ('missingB', 'lateB-playing')
    count = 1 if failed_successor else 4
    require(all(len(x) == count for x in (prepared, played, stopped, destroyed)), label + ': actual wave lifecycle cardinality')
    pointers = {s['bank_identity']: s['pointer'] for s in records if 'bank_identity' in s and 'pointer' in s}
    expected = [(pointers[0], 0)] if failed_successor else [(pointers[0], 0), (pointers[0], 1) if same else (pointers[1], 0)] * 2
    for kind in (prepared, played, stopped):
        require([(s['bank'], s['wave_index']) for s in kind] == expected, label + ': wrong candidate order')
    require(prepared[0]['qpc'] < calls['play']['begin'], label + ': initial wave not prepared')
    require(all(s['qpc'] >= calls['play']['begin'] for s in played), label + ': actual playback before Play')
    require(sum(s['qpc'] < calls['play']['begin'] for s in prepared) == 1, label + ': eager whole-sequence preparation')
    for i in range(1, count):
        require(calls['play']['begin'] <= prepared[i]['qpc'] < stopped[i-1]['qpc'], label + ': successor not prepared before preceding EOF')
    # Match wave identity only within each lifecycle; allocators reuse addresses.
    for start, end in zip(played, stopped):
        require(start['wave'] == end['wave'] and start['qpc'] < end['qpc'], label + ': no observed playing-to-stopped lifecycle')
    require([s['marker'] for s in notifications if s['notification'] == 5] == [11], label + ': authored marker')
    owners = [132, 0 if mode == 'missingB' else 4, 0]
    if not same and not failed_successor: owners[1] = 132
    require(snaps['play_200']['bank_states'] == owners, label + ': early bank ownership')
    require(snaps['play_4500']['cue_states'][0] == 32, label + ': no settled EOF')
    require(snaps['destroyed_cues_2000']['bank_states'] == owners and snaps['destroyed_cues_2000']['sound_states'][0] == 0, label + ': cached owner lifetime')
    require(snaps['destroyed_first_soundbank']['bank_states'] == [4 if x else 0 for x in owners], label + ': owner release')
    if mode != 'no-props':
        require([s['instances'] for s in records if s.get('counts_at') == 'play_4500'] == [0], label + ': stopped cue count')
    ios = [s for s in records if 'io' in s]
    require(bool(ios) == r['io'], label + ': IO control changed')
    if r['io']:
        require(any(s['io'] == 1 and s['bytes'] > 0 for s in ios) and any(s['io'] == 2 and s['ok'] == 1 and s['transferred'] > 0 for s in ios), label + ': no forwarded streaming reads/completions')


def check(matrix):
    rows = json.loads(matrix.read_text())['rows']
    provenance = json.loads((CORPUS / 'PROVENANCE.json').read_text())
    require(digest(matrix) == provenance['matrix_sha256'], 'Matrix hash changed')
    keys = [(r['arch'], r['profile'], r['mode'], r['lookahead'], r['io'], r['repeat']) for r in rows]
    require(len(keys) == len(set(keys)) and set(keys) == expected_cells(), 'Require all88 unique native prefetch cells')
    resident = REPO / 'modules/audio/tests/CNA/Internal/Audio/preparation-shapes/resident'
    for r in rows:
        check_row(r)
        if r['old_harness']:
            for k, v in provenance['retained_control_identities'][r['arch']].items(): require(r[k] == v, 'Retained control identity differs')
        else:
            require(r['source_sha256'] == digest(REPO / provenance['probe']) == provenance['probe_sha256'], 'Probe source changed')
            require(r['exe_sha256'] == provenance['probe_exe_sha256'][r['arch']], 'Probe binary differs')
            require(set(r['banks_sha256']) == {'shapes.xgs', 'shapes.xsb', 'A.xwb', 'B.xwb'}, 'Missing fixture identities')
            for name, value in r['banks_sha256'].items():
                path = resident / name if r['profile'] == 'resident' else CORPUS / r['profile'] / name
                require(digest(path) == value, 'Wrong fixture bytes')
        for stream in ('stdout', 'stderr'):
            path = matrix.parent / Path(r[stream + '_path']).name
            require(digest(path) == r[stream + '_sha256'], 'Raw log changed')
            if stream == 'stdout': require([json.loads(s) for s in path.read_text().splitlines() if s.startswith('{')] == r['records'], 'Parsed records differ')
            else:
                suffix = f"runtime-3.6-{r['arch']}/xactengine3_6.dll"
                require(any('loaddll:' in s and re.search(r'\bloaded\s+', s.lower()) and suffix in s.lower().replace('\\', '/').replace('//', '/') and s.rstrip().endswith(': native') for s in path.read_text().splitlines()), 'Actual native load missing')
    for rel, value in provenance['files_sha256'].items(): require(digest(CORPUS / rel) == value, 'Corpus changed')
    print('HND-XACT-02:88 native cells verified; early successor preparation/no late retry; SDK sequences4; hidden timing/PCM unqualified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('matrix', type=Path)
    try: check(parser.parse_args().matrix)
    except (ValueError, OSError, KeyError, StopIteration) as error: parser.exit(1, str(error) + '\n')
