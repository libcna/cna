#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Check HND-XACT-06 native loop-extension observations against loop-extensions/ORACLE.json.

Verifies every Microsoft XACT3.6 cell of the five capture groups (identities, raw logs, loader
evidence and the retained, resident and HND-XACT-02 controls each group reran), reduces each
wave lifecycle independently, compares it with the committed oracle, checks the oracle against
the stated rule, and proves byte by byte that every SDK bank differs from its HND-XACT-05
parent only in the varied field."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct

REPO = Path(__file__).resolve().parents[2]
AUDIO = REPO / 'modules/audio/tests/CNA/Internal/Audio'
CORPUS = AUDIO / 'loop-extensions'
FINITE = AUDIO / 'finite-cardinality'


def _load(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


preparation = _load('preparation', 'check-xact-preparation-shapes-oracle.py')
require, digest, DLLS = preparation.require, preparation.digest, preparation.DLLS
BANKS = ('shapes.xgs', 'shapes.xsb', 'A.xwb', 'B.xwb')
EFFECT = [f'{s}/raw{n}' for s in ('type4', 'type4nol', 'type6', 'type6nol0') for n in range(5)]
REGION = [f'{s}/raw{n}' for s in ('type1region', 'baseregion') for n in range(5)]
RANDOM = [f'{s}/raw{n}' for s in ('random', 'randomnol0', 'shuffle', 'shufflenol0') for n in (1, 3)]
BRIDGES = ['base/raw3', 'nol0/raw3', 'type1/raw3']
LONG = [f'{s}/raw{n}' for s in ('base', 'nol0') for n in (5, 100, 254)]
PITCH = ['type4/raw100', 'type4nol/raw100']
GROUPS = {'effect': EFFECT, 'region': REGION, 'random': RANDOM, 'raw': BRIDGES + LONG, 'pitch': PITCH}
REPEATS = {'pitch': 3}
CONTROLS = ('retained-control', 'resident-control', 'retained-samebank')
VARIATION_TYPE = {'random': 2, 'randomnol0': 2, 'shuffle': 4, 'shufflenol0': 4}
# The 24-byte Pitch Variation block XactBld3 writes for Min -100, Max 100 (FACT_internal.c:2361-2414,
# content version 46), without and with its own New Variation On Loop (flags 0x1000 / 0x1100).
PITCH_BLOCK = {0x1000: bytes.fromhex('9cff640097d600007a4400007a440000f0410000f0410010'),
               0x1100: bytes.fromhex('9cff640097d600007a4400007a440000f0410000f0410011')}
REGION_SAMPLES = (5512, 11026)  # smpl loop [5512, 16537] of the 22050-sample A waves


def decode(path):
    """FACT_internal.c:2498ff walk to the single complex sound's track events (types 1/3/4/6/9)."""
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
        if ev['type'] in (1, 3, 4, 6):
            ev['flags'] = d[q]; q += 1
            if ev['type'] in (1, 4):
                ev['wave'] = struct.unpack_from('<HB', d, q); q += 3
            ev['loop_at'] = q
            ev['loop_count'] = d[q]; q += 5
            ev['track_new_on_loop'], ev['variation_type'], ev['effect_variation_flags'] = False, None, None
            if ev['type'] in (4, 6):
                ev['effect_at'] = q
                ev['effect_block'] = d[q:q + 24]
                ev['effect_variation_flags'] = struct.unpack_from('<H', d, q + 22)[0]; q += 24
            if ev['type'] in (3, 6):
                ev['variation_at'] = q
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


def differing(a, b, ignore=range(8, 18)):
    """Changed byte positions between two equal-length banks; XSB CRC and FILETIME are 8..17."""
    require(len(a) == len(b), 'length differs')
    return [i for i in range(len(a)) if a[i] != b[i] and i not in ignore]


def require_effect_insertion(parent, child, name):
    """child = parent + one Pitch Variation block, with its event type moved 1->4 or 3->6.

    XactBld3 inserts the 24 effect bytes after the play-wave common fields; every other change
    must be a little-endian offset/size field of the bank that grew by exactly those 24 bytes."""
    a, pe = decode(parent)
    b, ce = decode(child)
    require(len(b) == len(a) + 24, name + ': not exactly one 24-byte block added')
    at = ce[0]['effect_at']
    require(b[at:at + 24] in PITCH_BLOCK.values(), name + ': inserted bytes are not the Pitch Variation block')
    require((pe[0]['type'], ce[0]['type']) in ((1, 4), (3, 6)), name + ': event family change')
    aligned = b[:at] + b[at + 24:]
    changed = set(differing(a, aligned)) - {pe[0]['offset']}
    sound = struct.unpack_from('<i', a, 70)[0]
    fields = [(o, '<I') for o in range(34, 74, 4)] + [(sound + 7, '<H')]
    # Offsets past the insertion point that live after it (the cue-name table entry).
    fields += [(i, '<I') for i in sorted(changed) if i >= at and i + 4 <= len(a)]
    for offset, form in fields:
        width = struct.calcsize(form)
        span = set(range(offset, offset + width))
        if not span & changed:
            continue
        old, new = struct.unpack_from(form, a, offset)[0], struct.unpack_from(form, aligned, offset)[0]
        require(new == old + 24, f'{name}: field at {offset} changed by {new - old}, not the 24-byte insertion')
        changed -= span
    require(not changed, f'{name}: unexplained changed bytes {sorted(changed)}')
    return ce[0]


def lifecycle(row, unit, held_unit=None):
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
    stopped = next(x for x in snaps if x['cue_state'] == 32)['begin']
    require(not any(s['notification'] == 13 and s['qpc'] > stopped for s in notes), 'wave started after the cue stopped')
    props = {(t['loop_count'], t['duration_ms']) for t in recs if 'track_at' in t and 'loop_count' in t}
    require(len(props) == 1, 'track properties vary')
    loop, duration = props.pop()
    regions = sorted({(w['loop_start'], w['loop_length']) for w in recs if 'loop_start' in w and w['bank_identity'] == 0})
    if held_unit and len(held) == 1:
        # A long held count is read with the held-pass length, not the one-wave control hold.
        require(abs(held[0] / held_unit - round(held[0] / held_unit)) < 0.1, 'held hold is not a whole number of passes')
    return {'activations': len(kinds[13]), 'play_order': [s['wave_index'] for s in kinds[13]],
            'passes_per_activation': sorted({round(h / (held_unit if held_unit and len(held) == 1 else unit)) for h in held}),
            'hold_half_passes': sorted({round(2 * h / unit) / 2 for h in held}),
            'mean_pass_ms': held[0] / (loop + 1) if len(held) == 1 else None,
            'track_properties': {'loop_count': loop, 'duration_ms': duration},
            'markers': [s['marker'] for s in notes if s['notification'] == 5], 'wave_regions': regions}


def rule(event, region, pitched=False):
    """The stated rule, computed from the authored bytes and the A waves' loop region alone.

    A pitch-varied 101-pass hold cannot be read as a whole pass count (+-6 %); the raw group's
    unpitched raw100 cells measure that count."""
    n = event['loop_count']
    separate = event['type'] in (3, 6) and event['track_new_on_loop']
    value = {'activations': n + 1 if separate else 1,
             'track_properties': {'loop_count': n, 'duration_ms': 1000 + n * (500 if region else 1000)}}
    if event['variation_type'] in (None, 0):
        value['play_order'] = [i % 2 for i in range(n + 1)] if separate else [0]
    if region:
        value['hold_half_passes'] = [1.0] if separate else [1 + n / 2]
    elif not pitched:
        value['passes_per_activation'] = [1] if separate else [n + 1]
    return value


def parent_checks(name, data, event):
    """Prove the one authored change of a new or archived variant against its parent bank."""
    shape, raw = name.split('/raw')
    raw = int(raw)
    finite = lambda v: FINITE / v / 'shapes.xsb'
    loop_byte = lambda parent, child: require(differing(parent, child) == ([event['loop_at']] if parent[event['loop_at']] != child[event['loop_at']] else []),
                                              name + ': changes more than the loop byte')
    if shape in VARIATION_TYPE:
        base, _ = decode(finite(('nol0' if shape.endswith('nol0') else 'base') + f'/raw{raw}'))
        require(differing(base, data) == [event['variation_at'] + 2], name + ': changes more than the variation type')
        require(event['variation_type'] == VARIATION_TYPE[shape], name + ': decoded variation type')
    elif shape in ('base', 'nol0') and raw > 4:
        base, _ = decode(finite(f'{shape}/raw3'))
        loop_byte(base, data)
    elif shape in ('type1region', 'baseregion'):
        base, _ = decode(finite(('type1' if shape == 'type1region' else 'base') + f'/raw{raw}'))
        require(differing(base, data) == [], name + ': the region shape changes its sound bank')
    elif shape in ('type4', 'type6') and raw <= 4:
        require_effect_insertion(finite(('type1' if shape == 'type4' else 'base') + f'/raw{raw}'), CORPUS / name / 'shapes.xsb', name)
        require(event['effect_variation_flags'] == 0x1000, name + ': pitch variation without its New Variation On Loop')
    elif shape == 'type4nol':
        sibling, _ = decode(CORPUS / f'type4/raw{raw}' / 'shapes.xsb')
        require(differing(sibling, data) == [event['effect_at'] + 23], name + ': changes more than the effect NEW_ON_LOOP')
        require(event['effect_variation_flags'] == 0x1100, name + ': pitch NEW_ON_LOOP')
    elif shape == 'type6nol0':
        require_effect_insertion(finite(f'nol0/raw{raw}'), CORPUS / name / 'shapes.xsb', name)
        require(event['effect_variation_flags'] == 0x1100 and not event['track_new_on_loop'], name + ': effect/track NEW_ON_LOOP')
    elif shape == 'type4' and raw > 4:
        sibling, _ = decode(CORPUS / 'type4/raw3/shapes.xsb')
        loop_byte(sibling, data)
    else:
        raise ValueError(name + ': no parent rule')
    require(event['loop_count'] == raw, name + ': decoded loop byte')


def check_corpus(provenance, oracle):
    for rel, value in provenance['files_sha256'].items():
        if not rel.endswith('shapes.xsb'):  # sound banks are checked structurally first, below
            require(digest(CORPUS / rel) == value, f'corpus file changed: {rel}')
    finite_provenance = json.loads((FINITE / 'PROVENANCE.json').read_text())
    for name in ('shapes.xgs', 'B.xwb'):
        require(digest(FINITE / 'shared' / name) == finite_provenance['authored_banks']['base/raw3'][name], 'shared bank identity')
    # region/A.xwb = shared/A.xwb with only the build time and the two A entries' loop regions.
    shared, region = (FINITE / 'shared/A.xwb').read_bytes(), (CORPUS / 'region/A.xwb').read_bytes()
    entries = struct.unpack_from('<I', shared, 20)[0]
    loop_fields = set(range(entries + 16, entries + 24)) | set(range(entries + 40, entries + 48))
    require(set(differing(shared, region, range(140, 148))) <= loop_fields, 'region/A.xwb changes more than the loop regions')
    for i in range(2):
        require(struct.unpack_from('<II', region, entries + 24 * i + 16) == REGION_SAMPLES, 'region A entry loop region')
    for name, want in oracle['variants'].items():
        data, events = decode(CORPUS / name / 'shapes.xsb')
        ev = events[0]
        require([e['marker'] for e in events if e['type'] == 9] == [11], name + ': marker event')
        for key in ('event_type', 'flags', 'loop_count', 'track_new_on_loop', 'variation_type', 'effect_variation_flags'):
            got = ev['type'] if key == 'event_type' else ev[key]
            require(got == want[key], f'{name}: decoded {key}')
        parent_checks(name, data, ev)
        require(digest(CORPUS / name / 'shapes.xsb') == provenance['authored_banks'][name]['shapes.xsb'], name + ': committed XSB')
        region = name.split('/')[0] in ('type1region', 'baseregion')
        stated = rule(ev, region, name in PITCH)
        for key, value in stated.items():
            require(want[key] == value, f'{name}: oracle {key} {want[key]} does not obey the rule {value}')


def check_row_identity(r, matrix_dir, provenance, label):
    require(r['rc'] == 0, label + ': incomplete capture')
    require(r['dll_sha256'] == DLLS[r['arch']], label + ': wrong engine')
    require(r['xact_load_events'] and all(e['implementation'] == 'native' for e in r['xact_load_events']), label + ': native image unestablished')
    for stream in ('stdout', 'stderr'):
        path = matrix_dir / 'raw' / Path(r[stream + '_path']).name
        require(digest(path) == r[stream + '_sha256'], label + ': raw log changed')
        if stream == 'stdout':
            require([json.loads(s) for s in path.read_text().splitlines() if s.startswith('{')] == r['records'], label + ': parsed records differ')
        else:
            suffix = f"runtime-3.6-{r['arch']}/xactengine3_6.dll"
            require(any('loaddll:' in s and re.search(r'\bloaded\s+', s.lower()) and suffix in s.lower().replace('\\', '/').replace('//', '/')
                        and s.rstrip().endswith(': native') for s in path.read_text(errors='replace').splitlines()), label + ': actual native load missing')
    if r['harness'] != 'retained':
        require(r['source_sha256'] == digest(REPO / provenance['probe']) == provenance['probe_sha256'], label + ': probe source changed')
        require(r['exe_sha256'] == provenance['probe_exe_sha256'][r['arch']], label + ': probe binary differs')


def check_retained(r, provenance, label):
    for k, v in provenance['retained_control_identities'][r['arch']].items():
        require(r[k] == v, label + ': retained control identity')
    snaps = {s['snapshot']: s for s in r['records'] if 'snapshot' in s}
    for stage in ('prepared', 'prepared_wait'):
        s = snaps[stage]
        require((s['sound_state'], s['cue_state'], s['A_state'], s['B_state']) == (128, 4, 132, 4), label + ': retained preparation')
    require(snaps['phase_1700']['cue_state'] == 32 and snaps['cue_destroyed_2000']['A_state'] == 132
            and snaps['soundbank_destroyed']['A_state'] == 4, label + ': retained lifetime')


def check(archive):
    provenance = json.loads((CORPUS / 'PROVENANCE.json').read_text())
    oracle = json.loads((CORPUS / 'ORACLE.json').read_text())
    finite_oracle = json.loads((FINITE / 'ORACLE.json').read_text())['variants']
    check_corpus(provenance, oracle)
    cells, pitch = 0, {}
    for group, variants in GROUPS.items():
        matrix = archive / f'matrix-{group}.json'
        require(digest(matrix) == provenance['matrices_sha256'][group], f'{group}: matrix hash changed')
        rows = json.loads(matrix.read_text())['rows']
        keys = [(r['arch'], r['variant'], r['repeat']) for r in rows]
        expected = {(a, v, 0) for a in DLLS for v in CONTROLS} | \
                   {(a, v, k) for a in DLLS for v in variants for k in range(REPEATS.get(group, 1))}
        require(len(keys) == len(set(keys)) and set(keys) == expected, f'{group}: cells differ from the protocol')
        unit = {}
        for r in rows:
            label = '/'.join(map(str, (group, r['arch'], r['variant'], r['repeat'])))
            check_row_identity(r, archive, provenance, label)
            if r['harness'] == 'retained':
                check_retained(r, provenance, label)
            elif r['variant'] == 'resident-control':
                for name in BANKS:
                    require(r['banks_sha256'][name] == digest(AUDIO / 'preparation-shapes/resident' / name), label + ': control bytes')
                one = lifecycle(r, 1)
                require(one['activations'] == 1 and one['play_order'] == [0], label + ': one-wave control')
                recs = r['records']; freq = next(s['frequency'] for s in recs if 'frequency' in s)
                start = next(s for s in recs if s.get('notification') == 13); stop = next(s for s in recs if s.get('notification') == 14)
                unit[r['arch']] = (stop['qpc'] - start['qpc']) * 1000 / freq
        # The control hold (one wave, start/stop latency, ~20 ms quantum) reads small counts; a
        # 101/255-pass hold needs the held-pass length, taken from the 6-pass nol0/raw5 hold once
        # the control has read that one unambiguously.
        held_unit = {}
        for r in rows:
            if r['variant'] == 'nol0/raw5':
                h = lifecycle(r, unit[r['arch']])
                require(h['activations'] == 1 and h['passes_per_activation'] == [6], 'nol0/raw5 is not one 6-pass hold')
                recs = r['records']; freq = next(s['frequency'] for s in recs if 'frequency' in s)
                start = next(s for s in recs if s.get('notification') == 13); stop = next(s for s in recs if s.get('notification') == 14)
                hold = (stop['qpc'] - start['qpc']) * 1000 / freq
                require(abs(hold / unit[r['arch']] - 6) < 0.3, 'nol0/raw5 hold is ambiguous')
                held_unit[r['arch']] = hold / 6
        for r in rows:
            if r['harness'] == 'retained' or r['variant'] == 'resident-control':
                continue
            label = '/'.join(map(str, (group, r['arch'], r['variant'], r['repeat'])))
            long_held = r['variant'] in ('nol0/raw100', 'nol0/raw254')
            got = lifecycle(r, unit[r['arch']], held_unit[r['arch']] if long_held else None)
            if r['variant'] == 'retained-samebank':
                for name in BANKS:
                    require(r['banks_sha256'][name] == digest(AUDIO / 'variation-prefetch/samebank' / name), label + ': HND-XACT-02 bytes')
                require(got['activations'] == 4 and got['play_order'] == [0, 1, 0, 1], label + ': HND-XACT-02 four waves not reproduced')
                continue
            require(r['banks_sha256'] == provenance['authored_banks'][r['variant']], label + ': authored bank identities')
            if r['variant'] in BRIDGES:
                want = finite_oracle[r['variant']]
                for key in ('activations', 'play_order', 'passes_per_activation', 'track_properties', 'markers'):
                    require(got[key] == want[key], f'{label}: bridge {key} {got[key]} != HND-XACT-05 {want[key]}')
                continue
            want = oracle['variants'][r['variant']]
            region = r['variant'].split('/')[0] in ('type1region', 'baseregion')
            require(got['wave_regions'] == ([REGION_SAMPLES] if region else [(0, 0)]), label + ': wave bank loop regions')
            keys = ['activations', 'track_properties', 'markers'] + (['hold_half_passes'] if region else
                   [] if group == 'pitch' else ['passes_per_activation'])
            if 'play_order' in want:
                keys.append('play_order')
            else:
                order = want['play_order_by_cell'][f"{r['arch']}/{r['repeat']}"]
                require(got['play_order'] == order, f'{label}: recorded draw order')
            for key in keys:
                require(got[key] == want[key], f'{label}: {key} {got[key]} != oracle {want[key]}')
            if group == 'pitch':
                pitch.setdefault(r['variant'], []).append(got['mean_pass_ms'])
        cells += len(rows)
    # A pitch drawn once per held wave scatters its 101-pass mean over the +-100 cent range
    # (about 11 %); one drawn per pass averages out. The oracle records each variant's spread
    # over its six cells (x86/x64, three repeats) and the verdict the stated limit gives.
    limit = oracle['pitch_per_pass']['spread_limit_ms']
    for variant in PITCH:
        means, want = pitch[variant], oracle['pitch_per_pass'][variant]
        require(len(means) == 2 * REPEATS['pitch'], variant + ': pitch cells')
        spread = max(means) - min(means)
        require(round(spread, 1) == want['mean_pass_spread_ms'], f'{variant}: spread {spread:.1f} ms != oracle {want["mean_pass_spread_ms"]}')
        require((spread < limit) == want['varies_per_pass'], f'{variant}: per-pass verdict')
    require(not oracle['pitch_per_pass']['type4/raw100']['varies_per_pass']
            and oracle['pitch_per_pass']['type4nol/raw100']['varies_per_pass'], 'pitch verdicts')
    require(cells == provenance['cells'], f'{cells} cells, provenance says {provenance["cells"]}')
    print(f'HND-XACT-06: {cells} native cells verified; every family plays raw n as n+1 passes, only track '
          'NEW_ON_LOOP separates them; a held wave repeats its loop region')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, help='cnaplan/evidence/rev002/hnd-xact-06-loop-extensions')
    args = parser.parse_args()
    try: check(args.archive)
    except (ValueError, OSError, KeyError, StopIteration) as error: parser.exit(1, str(error) + '\n')
