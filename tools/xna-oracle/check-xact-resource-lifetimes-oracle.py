#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Check74 native XACT3.6 lifecycle cells; no hidden-clock/quiescence inference."""
import argparse,importlib.util,json,re
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];ROOT=REPO/'modules/audio/tests/CNA/Internal/Audio'
spec=importlib.util.spec_from_file_location('prep',Path(__file__).with_name('check-xact-preparation-shapes-oracle.py'));p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
require,digest=p.require,p.digest
PROFILES=('resident','weighted','interactive','streaming','ordered')
MODES=('normal','no-props','unplayed-stop','unplayed-destroy','destroy-playing','unregister','clone')

def check_row(r):
    label=f"{r['capture_index']}/{r['arch']}/{r['profile']}/{r['mode']}"
    require(r['rc']==0 and r['dll_sha256']==p.DLLS[r['arch']],label+': incomplete/wrong runtime')
    require(r['xact_load_events'] and all(e['implementation']=='native'for e in r['xact_load_events']),label+': native image missing')
    if r['old_harness']:p.check_row(r);return
    rec=r['records'];ss={s['snapshot']:s for s in rec if 'snapshot'in s};calls={s['call']:s for s in rec if 'call'in s};ns=[s for s in rec if 'notification'in s]
    pr,mo=r['profile'],r['mode'];interactive=pr=='interactive';ordered=pr=='ordered';clone=mo=='clone';unplayed=mo.startswith('unplayed-')
    require(calls['prepare']['hr']=='00000000',label+': Prepare refused')
    if not unplayed:require(calls['play']['hr']=='00000000',label+': Play refused')
    prepared=[s for s in ns if s['notification']==12];played=[s for s in ns if s['notification']==13];stops=[s for s in ns if s['notification']==14];destroyed=[s for s in ns if s['notification']==16]
    if unplayed:
        n=0 if interactive else 1
        require((len(prepared),len(played),len(stops),len(destroyed))==(n,0,0,n),label+': never-played wave lifecycle')
        require(not any(s['notification']==5 for s in ns),label+': unplayed marker')
    elif mo=='destroy-playing':require((len(prepared),len(played),len(stops),len(destroyed))==((2 if ordered else 1),1,1,(2 if ordered else 1)),label+': forced cue destruction lifecycle')
    elif mo=='unregister':
        require((len(prepared),len(played),len(stops),len(destroyed))==((2 if ordered else 1),1,0,0),label+': unregister observation counts')
        unregister=[s for s in rec if s.get('call')=='unregister'];require(unregister and all(s['hr']=='00000000'for s in unregister),label+': unregister failed')
        require(all(s['qpc']<=unregister[-1]['end']for s in ns),label+': callback after final unregister return')
    else:
        n=(4 if ordered else 1)*(2 if clone else 1)
        require(all(len(x)==n for x in (prepared,played,stops,destroyed)),label+': complete actual wave cardinality')
    identities={s['pointer']:s['bank_identity']for s in rec if 'bank_identity'in s and 'pointer'in s}
    def owners(indices):return [132 if i in indices else 4 for i in (0,1)]+[0]
    used={identities[s['bank']]for s in prepared}
    require(ss['destroyed_cues_2000']['bank_states']==owners(used) and ss['destroyed_cues_2000']['sound_states']==[0,0],label+': owner after cue destruction')
    if clone:
        require(all(calls[n]['hr']=='00000000'for n in ('create_clone','prepare_clone','play_clone')),label+': clone refused')
        pointer=next(s['pointer']for s in rec if s.get('cue_identity')==1)
        second={identities[s['bank']]for s in played if s['cue']==pointer}
        require(ss['destroyed_first_soundbank']['bank_states']==owners(second) and ss['destroyed_both_soundbanks']['bank_states']==[4,4,0],label+': independent clone owners')
    else:require(ss['destroyed_first_soundbank']['bank_states']==[4,4,0],label+': soundbank release')
    state=0 if mo in ('destroy-playing','unplayed-destroy') else 8 if interactive and not unplayed else 32
    require(ss['play_4500']['cue_states'][0]==state,label+': settled handle state')
    if mo!='no-props':
        counts=[s['instances']for s in rec if s.get('counts_at')=='play_4500']
        expected=[1 if interactive and not unplayed and mo!='destroy-playing'else 0]*(2 if clone else 1)
        # Probe queries SoundBank instance counts even after its cue is gone.
        require(counts==expected,label+': handle snapshot count')
    require(any(s.get('trace_end')and not s['overflow']for s in rec),label+': callback trace overflow')

def check(matrix):
    prov=json.loads((ROOT/'resource-lifetimes/PROVENANCE.json').read_text());rows=json.loads(matrix.read_text())['rows']
    expected=[(arch,pr,mo)for arch in p.DLLS for pr,mo in [('old-resident','keep'),('resident','normal')]+[(pr,mo)for pr in PROFILES for mo in MODES]]
    require(len(rows)==74 and [(r['arch'],r['profile'],r['mode'])for r in rows]==expected,'Require all74 scheduled cells, retaining control/shape repeats')
    require(digest(matrix)==prov['matrix_sha256'],'Matrix changed')
    for i,r in enumerate(rows):
        require(r['capture_index']==i and r['campaign']=='native-v5-lifetimes','Campaign/ordinal identity changed');check_row(r)
        if r['old_harness']:
            for k,v in prov['retained_control_identities'][r['arch']].items():require(r[k]==v,'Retained TQ-XACT-31 identity changed')
        else:
            require(r['source_sha256']==digest(REPO/prov['probe'])==prov['source_sha256'] and r['exe_sha256']==prov['probe_exe_sha256'][r['arch']],'Probe identity changed')
            folder=ROOT/prov['fixtures'][r['profile']]
            for n,h in r['banks_sha256'].items():require(digest(folder/n)==h,'Fixture changed')
        for kind in ('stdout','stderr'):
            raw=matrix.parent/Path(r[kind+'_path']).name;require(digest(raw)==r[kind+'_sha256'],'Raw log changed')
            if kind=='stdout':require([json.loads(s)for s in raw.read_text().splitlines()if s.startswith('{')]==r['records'],'Parsed records changed')
            else:require(any('loaddll:'in s and re.search(r'\bloaded\s+',s.lower()) and f"runtime-3.6-{r['arch']}/xactengine3_6.dll"in s.lower().replace('\\','/').replace('//','/') and s.rstrip().endswith(': native')for s in raw.read_text().splitlines()),'Actual native image missing')
    for f,h in prov['input_sha256'].items():require(digest(ROOT/f)==h,'Own input changed')
    print('HND-XACT-04:74 native lifecycle cells verified; unregister observation is not concurrent-quiescence/managed-API proof')

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('matrix',type=Path)
    try:check(a.parse_args().matrix)
    except (ValueError,OSError,KeyError,StopIteration)as e:a.exit(1,str(e)+'\n')
