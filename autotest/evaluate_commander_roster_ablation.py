"""Frozen-policy paired test of a land roster with/without one purchasable unit."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import secrets
import subprocess

from train_cold_storage import ROOT, episode_commands, save, score, battle_progress


def roster_commands(policy, seed, arena, opponent, seconds, name, units):
    """Change only the experiment's legal purchase roster; all remaining choices stay with the AI."""
    commands = episode_commands(policy,seed,arena,opponent,seconds,name,True)
    roster = next(c for c in commands if c['op']=='commander_roster')
    roster['units'] = units
    return commands


def evaluate(directory, output, excluded, seconds):
    """Freeze inputs and pair scenarios across both rosters; never select or publish a policy here."""
    read=lambda p:json.loads(Path(p).read_text(encoding='utf-8'))
    evaluation=read(directory/'evaluation.json')
    units=read(directory/'catalog.json')['units']
    if excluded not in units:
        raise ValueError('Excluded unit is not in the recorded land registry')
    output.mkdir(parents=True,exist_ok=True)
    previous=read(output/'identity.json') if (output/'identity.json').exists() else {}
    seed=previous.get('seed',secrets.randbelow(2**30))
    game=ROOT/'build/clang-release'
    identity={'seed':seed,'excluded':excluded,'seconds':seconds,
              'hashes':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in
                        (directory/'evaluation.json',directory/'catalog.json',Path(__file__),
                         ROOT/'autotest/train_cold_storage.py',game/'PlantsVsZombies.exe',game/'resources/gamedata.json')}}
    if previous and previous!=identity:
        raise RuntimeError('Ablation inputs changed; use a new directory')
    save(output/'identity.json',identity)
    rng=random.Random(seed)
    cases=[(a,o,rng.randrange(2**30)) for a,o in
           [('opening','builder'),('opening','ash'),('fortress','hunter'),
            ('elite_cluster','adaptive'),('banked_varied_cooling','counter'),('economy','hunter')]]
    commands=[]; jobs=[]
    for index,policy in enumerate(evaluation['policies']):
        for mode in ('full','excluded'):
            legal=[u for u in units if mode=='full' or u!=excluded]
            for j,(arena,opponent,case_seed) in enumerate(cases):
                name=f'candidate_{index}_{mode}_{j}'
                commands+=roster_commands(policy,case_seed,arena,opponent,seconds,name,legal)
                jobs.append((index,mode,j,name))
    commands.append({'op':'quit'})
    script=output/(output.name+'.json')
    save(script,{'muteAudio':True,'batchStepsPerFrame':32,'commands':commands})
    subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
                    str(ROOT/'autotest/run_commander_batch.ps1'),'-GameDirectory',str(game),'-Script',str(script)],check=True)
    evidence=game/'autotest/out'/output.name
    assert read(evidence/'status.json')['status']=='passed'
    log=(evidence/'run.log').read_text(encoding='utf-8')
    assert 'script finished OK' in log and 'FAIL' not in log
    scores=[{'full':[],'excluded':[]} for _ in evaluation['policies']]
    for index,mode,j,name in jobs:
        result=read(evidence/(name+'.json')); final=result['final']['coldStorage']
        assert result['final']['testAudio']['muted']
        assert final['enemyIce']==final['initialEnemyIce']+final['supplied']+final['killIncome']+final['workerIncome']-final['spent']
        if mode=='excluded':
            assert excluded not in result['initial']['coldStorage']['availableUnits']
            assert final['deploymentTypes'].get(excluded,0)==0
        scores[index][mode].append({'case':j,'outcome':result['outcome'],'score':score(result),
                                   'progress':battle_progress(result),'excludedUnitPurchases':final['deploymentTypes'].get(excluded,0),
                                   'result':str(evidence/(name+'.json'))})
    summary=[{'fullWins':sum(r['outcome']=='commander_win' for r in s['full']),
              'excludedWins':sum(r['outcome']=='commander_win' for r in s['excluded']),
              'fullWinsUsingExcluded':sum(r['outcome']=='commander_win' and r['excludedUnitPurchases']>0 for r in s['full'])}
             for s in scores]
    save(output/'report.json',{'identity':identity,'cases':cases,'scores':scores,'summary':summary,
         'note':'Frozen-policy roster ablation, not retraining. Missing a preferred unit can require adaptation; outcomes include AI response to the changed pool. No policy was published.'})
    print(json.dumps(summary),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--exclude',default='ZOMBIE_ELITE_DANCER')
    parser.add_argument('--seconds',type=int,default=420)
    args=parser.parse_args()
    if not 120<=args.seconds<=1200:
        parser.error('seconds must be 120..1200')
    evaluate(args.directory,args.output,args.exclude,args.seconds)
