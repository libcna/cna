#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Check HND-XACT-05 native finite-loop cardinality observations against ORACLE.json.

Verifies every Microsoft XACT3.6 cell (identities, raw logs, loader evidence, controls),
reduces its wave lifecycle independently, compares it with the committed oracle, and checks
that the oracle obeys the stated rule and that each SDK sound bank differs from the measured
HND-XACT-02 shape only in the varied field."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct

REPO = Path(__file__).resolve().parents[2]
AUDIO = REPO / 'modules/audio/tests/CNA/Internal/Audio'
CORPUS = AUDIO / 'finite-cardinality'
_spec = importlib.util.spec_from_file_location('preparation', Path(__file__).with_name('check-xact-preparation-shapes-oracle.py'))
preparation = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(preparation)
require, digest, DLLS = preparation.require, preparation.digest, preparation.DLLS
SHAPES, RAWS = ('base', 'flags0', 'nol0', 'type1'), range(5)
BANKS = ('shapes.xgs', 'shapes.xsb', 'A.xwb', 'B.xwb')
# Build timestamps: XGS FILETIME at 10..17 (CRC at 8..9 is unchanged), XWB BuildTime at 140..147.
TIMESTAMP = {'shapes.xgs': range(10, 18), 'A.xwb': range(140, 148), 'B.xwb': range(140, 148)}


def decode(path):
    """FACT_internal.c:2498ff walk to the single complex sound's track events."""
    d = Path(path).read_bytes()
    require(d[:4] == b'SDBK', f'{path}: not an XSB')
    sound = struct.unpack_from('<i', d, 70)[0]
    require(d[sound] == 0x01 and d[sound + 9] == 1, f'{path}: expected one plain complex track')
    p = struct.unpack_from('<I', d, sound + 11)[0] + 1
    events = []
    for _ in range(d[p - 1]):
        info = struct.unpack_from('<I', d, p)[0]
        require(d[p + 6] == 0xFF, f'{path}: event separator')
        ev = {'type': info & 0x1F, 'timestamp': (info >> 5) & 0xFFFF, 'offset': p}
        q = p + 7
        if ev['type'] in (1, 3):
            ev['flags'] = d[q]; q += 1
            if ev['type'] == 1:
                ev['wave'] = struct.unpack_from('<HB', d, q); q += 3
            ev['loop_count'] = d[q]; q += 5
            ev['track_new_on_loop'] = False
            if ev['type'] == 3:
                info2 = struct.unpack_from('<I', d, q)[0]; q += 8
                ev['track_new_on_loop'] = bool(info2 & 0x00400000)
                ev['variation_type'] = (info2 >> 16) & 7
                ev['entries'] = [struct.unpack_from('<HBBB', d, q + 5 * i) for i in range(info2 & 0xFFFF)]
                q += 5 * (info2 & 0xFFFF)
        elif ev['type'] == 9:
            ev['marker'] = struct.unpack_from('<I', d, q)[0]; q += 4
        else:
            raise ValueError(f'{path}: unexpected event type {ev["type"]}')
        events.append(ev); p = q
    return d, events


def differing(a, b, ignore=()):
    require(len(a) == len(b), 'length differs')
    return [i for i in range(len(a)) if a[i] != b[i] and i not in ignore]


def rule(event):
    """The stated HND-XACT-05 rule, computed from the authored bytes alone."""
    n, separate = event['loop_count'], event['type'] == 3 and event['track_new_on_loop']
    return {'activations': n + 1 if separate else 1,
            'play_order': [i % 2 for i in range(n + 1)] if separate else [0],
            'passes_per_activation': [1] if separate else [n + 1],
            'track_properties': {'loop_count': n, 'duration_ms': 1000 * (n + 1)}}


def lifecycle(row, unit):
    recs = row['records']
    play = next(s for s in recs if s.get('call') == 'play')
    require(play['hr'] == '00000000', 'Play refused')
    freq = next(s['frequency'] for s in recs if 'frequency' in s)
    notes = [s for s in recs if 'notification' in s]
    kinds = {k: [s for s in notes if s['notification'] == k] for k in (12, 13, 14, 16)}
    require(len({len(v) for v in kinds.values()}) == 1, 'PREPARED/PLAY/STOP/DESTROY counts differ')
    require(sum(s['qpc'] < play['begin'] for s in kinds[12]) == 1, 'not exactly one wave prepared before Play')
    require(all(s['qpc'] >= play['begin'] for s in kinds[13]), 'playback before Play')
    held = []
    for start in kinds[13]:
        end = next((s for s in kinds[14] if s['wave'] == start['wave'] and s['qpc'] > start['qpc']), None)
        require(end is not None, 'wave never stopped')
        held.append((end['qpc'] - start['qpc']) * 1000 / freq)
    snaps = [s for s in recs if s.get('snapshot', '').startswith('play_')]
    require(snaps and snaps[-1]['cue_state'] == 32, 'cue did not stop by itself within the window')
    require(not any(s['notification'] == 13 and s['qpc'] > next(x for x in snaps if x['cue_state'] == 32)['begin'] for s in notes),
            'wave started after the cue stopped')
    props = {(t['loop_count'], t['duration_ms']) for t in recs if 'track_at' in t and 'loop_count' in t}
    require(len(props) == 1, 'track properties vary')
    loop, duration = props.pop()
    return {'activations': len(kinds[13]), 'play_order': [s['wave_index'] for s in kinds[13]],
            'passes_per_activation': sorted({round(h / unit) for h in held}),
            'track_properties': {'loop_count': loop, 'duration_ms': duration},
            'markers': [s['marker'] for s in notes if s['notification'] == 5]}


def expected_cells():
    cells = set()
    for arch in DLLS:
        cells |= {(arch, v, 0) for v in ('retained-control', 'resident-control', 'retained-samebank')}
        cells |= {(arch, f'{s}/raw{n}', 0) for s in SHAPES for n in RAWS}
        cells |= {(arch, f'base/raw{n}', 1) for n in RAWS}
    return cells


def check_corpus(provenance, oracle, evidence):
    for rel, value in provenance['files_sha256'].items():
        require(digest(CORPUS / rel) == value, f'corpus file changed: {rel}')
    base, base_events = decode(CORPUS / 'base/raw3/shapes.xsb')
    retained, _ = decode(AUDIO / 'variation-prefetch/samebank/shapes.xsb')
    require(differing(base, retained, range(8, 18)) == [], 'base/raw3 is not the HND-XACT-02 samebank shape')
    loop_at, flags_at = base_events[0]['offset'] + 8, base_events[0]['offset'] + 7
    nol_at = loop_at + 7
    for shape in SHAPES:
        for n in RAWS:
            name = f'{shape}/raw{n}'
            data, events = decode(CORPUS / name / 'shapes.xsb')
            ev, want = events[0], oracle['variants'][name]
            require([e['marker'] for e in events if e['type'] == 9] == [11], name + ': marker event')
            for key in ('loop_count', 'flags', 'track_new_on_loop'):
                require(ev[key] == want[key], f'{name}: decoded {key}')
            require(ev['type'] == want['event_type'], name + ': event type')
            got = {k: want[k] for k in ('activations', 'play_order', 'passes_per_activation', 'track_properties')}
            require(rule(ev) == got, name + ': oracle does not obey the stated rule')
            if shape == 'type1':
                require(ev['type'] == 1 and ev['wave'] == (0, 0) and ev['flags'] == 0x0c and len(data) == len(base) - 15,
                        name + ': PlayWave A0 is not the base event without its candidate table')
                continue
            expect = ([loop_at] if n != 3 else []) + ([flags_at] if shape == 'flags0' else []) + ([nol_at] if shape == 'nol0' else [])
            require(differing(base, data, range(8, 18)) == sorted(expect), f'{name}: changes more than the varied field')
    for name, flags in {'ucs1-nspol1': 0x0c, 'ucs0-nspol0': 0x00, 'ucs1-nspol0': 0x04, 'ucs0-nspol1': 0x08}.items():
        data, events = decode(CORPUS / 'explore' / name / 'shapes.xsb')
        require(events[0]['flags'] == flags, f'explore/{name}: speaker flag bit')
        require(differing(base, data, range(8, 18)) == ([] if flags == 0x0c else [flags_at]), f'explore/{name}: other bytes')
    shared = provenance['authored_banks']['base/raw3']
    for name in ('shapes.xgs', 'A.xwb', 'B.xwb'):
        require(digest(CORPUS / 'shared' / name) == shared[name], f'shared/{name} is not the base/raw3 bank')
    if evidence and (evidence / 'banks').is_dir():
        for variant, banks in provenance['authored_banks'].items():
            for name, value in banks.items():
                path = evidence / 'banks' / variant / name
                require(digest(path) == value, f'evidence bank {variant}/{name}')
                if name != 'shapes.xsb' and not variant.startswith(('type4', 'type6', 'type1region', 'baseregion')):
                    require(differing((CORPUS / 'shared' / name).read_bytes(), path.read_bytes(), TIMESTAMP[name]) == [],
                            f'{variant}/{name} differs from shared beyond its build timestamp')
            if variant in oracle['variants'] or variant.startswith('explore/'):
                require(digest(CORPUS / variant / 'shapes.xsb') == banks['shapes.xsb'], f'{variant}: committed XSB')
                require((CORPUS / variant / 'shapes.xap').read_bytes() == (evidence / 'banks' / variant / 'shapes.xap').read_bytes(), f'{variant}: XAP')


def check(matrix, evidence):
    provenance = json.loads((CORPUS / 'PROVENANCE.json').read_text())
    oracle = json.loads((CORPUS / 'ORACLE.json').read_text())
    require(digest(matrix) == provenance['matrix_sha256'], 'Matrix hash changed')
    rows = json.loads(matrix.read_text())['rows']
    keys = [(r['arch'], r['variant'], r['repeat']) for r in rows]
    require(len(keys) == len(set(keys)) and set(keys) == expected_cells(), 'Require all56 unique native cardinality cells')
    check_corpus(provenance, oracle, evidence)
    unit = {}
    for r in rows:
        label = '/'.join(map(str, (r['arch'], r['variant'], r['repeat'])))
        require(r['rc'] == 0, label + ': incomplete capture')
        require(r['dll_sha256'] == DLLS[r['arch']], label + ': wrong engine')
        require(r['xact_load_events'] and all(e['implementation'] == 'native' for e in r['xact_load_events']), label + ': native image unestablished')
        for stream in ('stdout', 'stderr'):
            path = matrix.parent / 'raw' / Path(r[stream + '_path']).name
            require(digest(path) == r[stream + '_sha256'], label + ': raw log changed')
            if stream == 'stdout':
                require([json.loads(s) for s in path.read_text().splitlines() if s.startswith('{')] == r['records'], label + ': parsed records differ')
            else:
                suffix = f"runtime-3.6-{r['arch']}/xactengine3_6.dll"
                require(any('loaddll:' in s and re.search(r'\bloaded\s+', s.lower()) and suffix in s.lower().replace('\\', '/').replace('//', '/')
                            and s.rstrip().endswith(': native') for s in path.read_text(errors='replace').splitlines()), label + ': actual native load missing')
        if r['harness'] == 'retained':
            for k, v in provenance['retained_control_identities'][r['arch']].items():
                require(r[k] == v, label + ': retained control identity')
            snaps = {s['snapshot']: s for s in r['records'] if 'snapshot' in s}
            for stage in ('prepared', 'prepared_wait'):
                s = snaps[stage]
                require((s['sound_state'], s['cue_state'], s['A_state'], s['B_state']) == (128, 4, 132, 4), label + ': retained preparation')
            require(snaps['phase_1700']['cue_state'] == 32 and snaps['cue_destroyed_2000']['A_state'] == 132
                    and snaps['soundbank_destroyed']['A_state'] == 4, label + ': retained lifetime')
            continue
        require(r['source_sha256'] == digest(REPO / provenance['probe']) == provenance['probe_sha256'], label + ': probe source changed')
        require(r['exe_sha256'] == provenance['probe_exe_sha256'][r['arch']], label + ': probe binary differs')
        if r['variant'] == 'resident-control':
            for name in BANKS:
                require(r['banks_sha256'][name] == digest(AUDIO / 'preparation-shapes/resident' / name), label + ': control bytes')
            held = lifecycle(r, 1)
            require(held['activations'] == 1 and held['play_order'] == [0], label + ': one-wave control')
            recs = r['records']; play = next(s for s in recs if s.get('call') == 'play'); freq = next(s['frequency'] for s in recs if 'frequency' in s)
            start = next(s for s in recs if s.get('notification') == 13); stop = next(s for s in recs if s.get('notification') == 14)
            unit[r['arch']] = (stop['qpc'] - start['qpc']) * 1000 / freq
    for r in rows:
        if r['harness'] == 'retained' or r['variant'] == 'resident-control':
            continue
        label = '/'.join(map(str, (r['arch'], r['variant'], r['repeat'])))
        got = lifecycle(r, unit[r['arch']])
        if r['variant'] == 'retained-samebank':
            for name in BANKS:
                require(r['banks_sha256'][name] == digest(AUDIO / 'variation-prefetch/samebank' / name), label + ': HND-XACT-02 bytes')
            require(got['activations'] == 4 and got['play_order'] == [0, 1, 0, 1], label + ': HND-XACT-02 four waves not reproduced')
            continue
        require(r['banks_sha256'] == provenance['authored_banks'][r['variant']], label + ': authored bank identities')
        require(r['banks_sha256']['shapes.xsb'] == digest(CORPUS / r['variant'] / 'shapes.xsb'), label + ': committed XSB')
        want = oracle['variants'][r['variant']]
        for key in ('activations', 'play_order', 'passes_per_activation', 'track_properties', 'markers'):
            require(got[key] == want[key], f'{label}: {key} {got[key]} != oracle {want[key]}')
    print('HND-XACT-05:56 native cells verified; raw n plays n+1 passes; NEW_ON_LOOP separates them; flags byte inert')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('matrix', type=Path)
    parser.add_argument('--evidence', type=Path, help='archive with banks/ (default: the matrix directory)')
    args = parser.parse_args()
    try: check(args.matrix, args.evidence or args.matrix.parent)
    except (ValueError, OSError, KeyError, StopIteration) as error: parser.exit(1, str(error) + '\n')
