#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Validate the22 genuine XACT3.6 cross-bank cells and raw identity receipts."""
import argparse,importlib.util,json,re
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
ROOT=REPO/'modules/audio/tests/CNA/Internal/Audio'
spec=importlib.util.spec_from_file_location('prep',Path(__file__).with_name('check-xact-preparation-shapes-oracle.py'))
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
require,digest=p.require,p.digest

def check_row(r):
    label=f"{r['arch']}/{r['profile']}/{r['mode']}"
    require(r['rc']==0,label+': incomplete native run')
    require(r['dll_sha256']==p.DLLS[r['arch']] and r['xact_load_events'] and all(x['implementation']=='native' for x in r['xact_load_events']),label+': wrong loaded image')
    if r['old_harness']:p.check_row(r);return
    rec=r['records'];sn={x['snapshot']:x for x in rec if 'snapshot'in x};profile=r['profile'];mode=r['mode'];clone=mode=='clone';dup=mode=='duplicateA';ordered=profile=='ordered';interactive=profile=='interactive'
    calls={x['call']:x for x in rec if 'call'in x}
    for name in ('prepare','play')+ (('create_clone','prepare_clone','play_clone') if clone else (('prepare_clone','play_clone') if mode=='same-soundbank' else ())):require(calls[name]['hr']=='00000000',label+': refused '+name)
    banks={x['pointer']:x['bank_identity'] for x in rec if 'bank_identity'in x and 'pointer'in x}
    waves=[x for x in rec if x.get('notification')==13];groups={}
    for w in waves:groups.setdefault(w['cue'],[]).append((banks[w['bank']],w['wave_index']))
    expected=[(0,0),(1,0),(0,0),(1,0)] if ordered else [(0,0)]
    require(all(sum(x.get('notification')==kind for x in rec)==len(waves) for kind in (12,14,16)),label+': incomplete wave preparation/stop/destruction lifecycle')
    require(len(groups)==(2 if clone or mode=='same-soundbank' else 1) and all(v==expected for v in groups.values()),label+': actual played identities/cardinality')
    require(sn['prepared_300']['bank_states']==([4,4,4 if dup else 0] if interactive else [132,4,4 if dup else 0]),label+': preparation ownership')
    owners=[132,132 if ordered else 4,4 if dup else 0]
    require(sn['play_400']['bank_states']==owners,label+': duplicate/selection owners')
    require(sn['destroyed_cues_2000']['bank_states']==owners and sn['destroyed_cues_2000']['sound_states']==[0,0],label+': persistent bank owner')
    released=[4,4,4 if dup else 0]
    require(sn['destroyed_first_soundbank']['bank_states']==(owners if clone else released),label+': first soundbank release')
    if clone:
        require(sn['destroyed_both_soundbanks']['bank_states']==released,label+': second soundbank release')
        x=next(x for x in rec if 'clone_input'in x)
        require(x['primary']!=x['clone'] and x['same_size'] and x['same_bytes']==(not ordered),label+': clone backing inputs')
    if profile=='admission' and mode!='normal':require(sn['play_400']['cue_states']==[32,8],label+': category foreign victim not retired')
    elif clone:require(sn['play_400']['cue_states']==[8,8],label+': unrelated cloned cues evicted')
    require(sn['play_4500']['cue_states'][0]==(8 if interactive else 32),label+': settled EOF state')
    counts=[x['instances'] for x in rec if x.get('counts_at')=='play_4500']
    require(counts==([1,1] if clone else [1]) if interactive else counts==([0,0] if clone else [0]),label+': settled cue counts')
    require(not any(banks[x['bank']]==2 for x in waves),label+': later duplicate selected')

def check(matrix):
    prov=json.loads((ROOT/'cross-bank/PROVENANCE.json').read_text());rows=json.loads(matrix.read_text())['rows']
    expected={(a,pr,mo) for a in p.DLLS for pr,mo in [('old-resident','keep'),('resident','normal'),('admission','normal'),('admission','same-soundbank'),('admission','clone')]+[(pr,mo)for pr in ('resident','ordered','interactive')for mo in ('duplicateA','clone')]}
    require(len(rows)==22 and {(r['arch'],r['profile'],r['mode'])for r in rows}==expected,'Require all22 distinct cells')
    require(digest(matrix)==prov['matrix_sha256'],'Matrix changed')
    for r in rows:
        check_row(r)
        if not r['old_harness']:
            require(r['source_sha256']==digest(REPO/prov['probe'])==prov['source_sha256'] and r['exe_sha256']==prov['probe_exe_sha256'][r['arch']],'Probe identity changed')
            folder=ROOT/('cross-bank/admission' if r['profile']=='admission' else 'variation-prefetch/ordered' if r['profile']=='ordered' else 'preparation-shapes/'+r['profile'])
            for n,v in r['banks_sha256'].items():require(digest(folder/n)==v,'Fixture differs')
        for kind in ('stdout','stderr'):
            raw=matrix.parent/Path(r[kind+'_path']).name;require(digest(raw)==r[kind+'_sha256'],'Raw log changed')
            if kind=='stdout':require([json.loads(s)for s in raw.read_text().splitlines()if s.startswith('{')]==r['records'],'Records changed')
            else:require(any('loaddll:'in s and re.search(r'\bloaded\s+',s.lower()) and f"runtime-3.6-{r['arch']}/xactengine3_6.dll" in s.lower().replace('\\','/').replace('//','/') and s.rstrip().endswith(': native')for s in raw.read_text().splitlines()),'Actual native load missing')
    for f,h in prov['files_sha256'].items():require(digest(ROOT/'cross-bank'/f)==h,'Corpus changed')
    print('HND-XACT-03:22 native cells verified; first duplicate wins, independent owners, sole cross-bank category victim replaced; no exact latency/cardinality generalization')

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('matrix',type=Path)
    try:check(a.parse_args().matrix)
    except (ValueError,OSError,KeyError,StopIteration)as e:a.exit(1,str(e)+'\n')
