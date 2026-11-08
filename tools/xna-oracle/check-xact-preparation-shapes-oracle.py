#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Validate HND-XACT-01 native preparation observations, identities and raw captures."""
import argparse
import hashlib
import json
from pathlib import Path
import re

REPO = Path(__file__).resolve().parents[2]
CORPUS = REPO / 'modules/audio/tests/CNA/Internal/Audio/preparation-shapes'
DLLS = {'x86': '8a7e7c5ac2e6230f0249d46751522e7ecf85e7490cf7491ab73bf2e7e59e4c0f',
        'x64': '6e656d37eadec5acf8ca99f2df37768eaec1c919e616caf3805c0884b2af7c26'}


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def require(value, message):
    if not value: raise ValueError(message)


def cell_id(row):
    return f"{row['arch']}/{row['profile']}/{row['mode']}/io{int(row['io'])}/{row['repeat']}"


def expected_cells():
    cells = set()
    for arch in DLLS:
        cells.add((arch, 'old-resident', 'keep', False, 0))
        cells.add((arch, 'resident', 'normal', False, 0))
        for mode in ('normal', 'no-props', 'missingA', 'missingB'):
            for repeat in range(8): cells.add((arch, 'weighted', mode, False, repeat))
        for profile in ('interactive', 'streaming'):
            for mode in ('normal', 'no-props', 'missingA', 'missingB', 'unplayed-stop', 'unplayed-destroy'):
                cells.add((arch, profile, mode, False, 0))
        for mode in ('AtoB', 'BtoA', 'lateB', 'lateB-playing'): cells.add((arch, 'interactive', mode, False, 0))
        for mode in ('normal', 'no-props'): cells.add((arch, 'streaming', mode, True, 0))
    return cells


def check_row(row):
    label = cell_id(row)
    require(row['dll_sha256'] == DLLS[row['arch']], label + ': wrong Microsoft engine')
    require(row['xact_load_events'] and all(e['implementation'] == 'native' for e in row['xact_load_events']), label + ': no native load evidence')
    logs = row['records']; snaps = {s['snapshot']: s for s in logs if 'snapshot' in s}
    if row['old_harness']:
        require(row['rc'] == 0, label + ': resident control failed')
        for name in ('prepared', 'prepared_wait'):
            s = snaps[name]
            require((s['cue_state'], s['sound_state'], s['A_state'], s['B_state']) == (4, 128, 132, 4), label + ': retained control preparation')
        require(snaps['phase_1700']['cue_state'] == 32 and snaps['cue_destroyed_2000']['A_state'] == 132
                and snaps['soundbank_destroyed']['A_state'] == 4, label + ': retained control lifetime')
        return
    calls = [s for s in logs if 'call' in s]; prepare = next(s for s in calls if s['call'] == 'prepare')
    notifications = [s for s in logs if 'notification' in s]
    waves = [s for s in notifications if 12 <= s['notification'] <= 16]
    markers = [s['marker'] for s in notifications if s['notification'] == 5]
    profile, mode = row['profile'], row['mode']
    initial = snaps['initial']['bank_states']
    require(initial[:2] == [0 if mode == 'missingA' else 4,
                            0 if mode in ('missingB', 'lateB', 'lateB-playing') else 4], label + ': initial bank states')
    require(snaps['initial']['sound_states'][0] == 0, label + ': initial sound ownership')
    final_trace = [s for s in logs if s.get('trace_end')]
    require(len(final_trace) == 1 and not final_trace[0]['overflow'], label + ': incomplete trace')
    require(any(s.get('process_result') == row['rc'] for s in logs), label + ': terminal process status')
    for call in calls:
        require(call['hr'] == '00000000' or (call['call'] == 'prepare' and call['hr'] == '8ac70013'), label + ': unexpected failed native call')
    if row['rc'] == 4:
        require(profile in ('weighted', 'streaming') and mode in ('missingA', 'missingB')
                and prepare['hr'] == '8ac70013', label + ': refusal stage/code')
        require(snaps['after_prepare']['cue_states'][0] == 0 and snaps['after_prepare']['sound_states'][0] == 0,
                label + ': refusal returned/retained cue')
        require(snaps['after_prepare']['bank_states'] == initial and not waves, label + ': refusal warmed an unselected bank')
        return
    require(row['rc'] == 0 and prepare['hr'] == '00000000', label + ': preparation failed')
    for name in ('after_prepare', 'prepared_300'):
        require(snaps[name]['cue_states'][0] == 4 and snaps[name]['sound_states'][0] == 128, label + ': prepared handle ownership')
    properties = [s for s in logs if s.get('cue_properties')]
    if mode == 'no-props': require(not properties, label + ': no-properties control queried properties')
    else: require(len(properties) == 1 and properties[0]['interactive'] == (profile == 'interactive')
                  and properties[0]['variations'] == (2 if profile in ('weighted', 'interactive') else 1), label + ': wrong authored shape')
    selected = None
    if profile == 'interactive':
        require(snaps['prepared_300']['bank_states'] == initial, label + ': interactive resolved before Play')
        if mode not in ('unplayed-stop', 'unplayed-destroy', 'missingA', 'missingB', 'lateB-playing'):
            selected = 1 if mode in ('AtoB', 'lateB') else 0
    else:
        prepared = snaps['prepared_300']['bank_states']
        require(prepared.count(132) == 1, label + ': noninteractive must retain only selected bank')
        selected = prepared.index(132)
        require(profile == 'weighted' or selected == 0, label + ': wrong single-wave bank')
    play = next((c for c in calls if c['call'] == 'play'), None)
    if play:
        require(play['hr'] == '00000000', label + ': Play refused')
        require(all(s['qpc'] >= play['begin'] for s in notifications if s['notification'] in (5, 13)), label + ': playback/event before Play')
    require(snaps['destroyed_cues_2000']['cue_states'][0] == 0 and snaps['destroyed_cues_2000']['sound_states'][0] == 0,
            label + ': cue destruction owner release')
    # Prepared destruction leaves noninteractive cached bank references; interactive did not resolve any.
    if mode in ('unplayed-stop', 'unplayed-destroy'):
        require(not play and not any(s['notification'] == 13 for s in notifications), label + ': never-played control played')
        require(snaps['play_100']['cue_states'][0] == (32 if mode == 'unplayed-stop' else 0), label + ': settled unplayed state')
        if profile == 'interactive': selected = None
    else:
        require(play and snaps['play_4500']['cue_states'][0] == (8 if profile == 'interactive' else 32), label + ': settled playing/EOF state')
    owners = list(initial)
    if mode in ('lateB', 'lateB-playing'): owners[1] = 4
    if selected is not None: owners[selected] = 132
    require(snaps['destroyed_cues_2000']['bank_states'] == owners, label + ': cached bank lifetime')
    released = [4 if state else 0 for state in owners]
    require(snaps['destroyed_first_soundbank']['bank_states'] == released, label + ': sound destruction releases owners')
    if play and selected is None:
        require(not waves and not markers, label + ': missing interactive bank played/dispatched')
    elif play:
        bank = next(s['pointer'] for s in logs if s.get('bank_identity') == selected and 'pointer' in s)
        played = [s for s in waves if s['notification'] == 13]
        require(len(played) == 1 and played[0]['bank'] == bank and played[0]['wave_index'] == 0, label + ': wrong actual played wave')
        require(markers == [101 + selected if profile in ('weighted', 'interactive') else 11], label + ': wrong sound marker')
        counts = [s['instances'] for s in logs if s.get('counts_at') == 'play_4500']
        if mode != 'no-props': require(counts == [1 if profile == 'interactive' else 0], label + ': settled instances')


def check(matrix):
    rows = json.loads(matrix.read_text())['rows']; provenance = json.loads((CORPUS / 'PROVENANCE.json').read_text())
    keys = [(r['arch'], r['profile'], r['mode'], r['io'], r['repeat']) for r in rows]
    require(len(keys) == len(set(keys)) and set(keys) == expected_cells(), 'Require all104 unique preparation cells')
    for row in rows:
        check_row(row)
        if row['old_harness']:
            for key, value in provenance['retained_control_identities'][row['arch']].items():
                require(row[key] == value, 'Wrong retained control identity')
        else:
            require(row['source_sha256'] == digest(REPO / provenance['probe']) == provenance['probe_sha256'], 'Wrong probe source')
            require(row['exe_sha256'] == provenance['probe_exe_sha256'][row['arch']], 'Wrong probe binary')
            require(set(row['banks_sha256']) == {'shapes.xgs', 'shapes.xsb', 'A.xwb', 'B.xwb'}, 'Incomplete fixture identity')
            for name, value in row['banks_sha256'].items(): require(digest(CORPUS / row['profile'] / name) == value, 'Wrong fixture bytes')
        for stream in ('stdout', 'stderr'):
            # Capture paths are retained as originally logged. Archived receipts keep raw basenames.
            path = matrix.parent / Path(row[stream + '_path']).name
            require(digest(path) == row[stream + '_sha256'], 'Changed raw ' + stream)
            if stream == 'stdout': require([json.loads(s) for s in path.read_text().splitlines() if s.startswith('{')] == row['records'], 'Parsed record mismatch')
            else:
                suffix = f"runtime-3.6-{row['arch']}/xactengine3_6.dll"
                require(any('loaddll:' in s and re.search(r'\bloaded\s+', s.lower()) and suffix in s.lower().replace('\\\\', '/').replace('\\', '/') and s.rstrip().endswith(': native') for s in path.read_text().splitlines()), 'No actual native load in raw trace')
    for rel, value in provenance['files_sha256'].items(): require(digest(CORPUS / rel) == value, 'Corpus source/output changed')
    print('HND-XACT-01 native preparation:104 checked cells,86 complete/18 NoWaveBank refusals; no timing/PCM claim')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('matrix', type=Path)
    try: check(parser.parse_args().matrix)
    except (ValueError, OSError, KeyError, StopIteration) as error: parser.exit(1, str(error) + '\n')
