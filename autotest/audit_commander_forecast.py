"""Measure one purchased plan over its forecast horizon with later purchases disabled."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import secrets
from train_cold_storage import ROOT, INITIAL, episode_commands, save


def audit(output, policy_path=None, arenas=None, opponent='adaptive', seed=None, static_defense=False):
    """Keep subsequent reinforcement out of forecast/actual income comparisons."""
    game = ROOT / 'build/clang-release'
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    policy = json.loads((policy_path or game/'resources/ai/cold_storage_policy.json').read_text(encoding='utf-8'))
    candidates = [policy] if policy_path else [policy, {'weights':INITIAL,'preferences':{}},
                                            {'weights':policy['weights'],'preferences':{}}]
    previous = json.loads((output/'identity.json').read_text()) if (output/'identity.json').exists() else {}
    seed = seed if seed is not None else previous.get('seed',secrets.randbelow(2**30))
    arenas = arenas or ('developing','fortress','fortress_10_2')
    identity = {'exeSha256':hashlib.sha256((game/'PlantsVsZombies.exe').read_bytes()).hexdigest(),
                'coreSha256':hashlib.sha256((ROOT/'autotest/train_cold_storage.py').read_bytes()).hexdigest(),
                'auditSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'staticDefense':static_defense,'candidates':candidates,'arenas':list(arenas),'opponent':opponent,'seed':seed}
    if (output/'identity.json').exists() and json.loads((output/'identity.json').read_text()) != identity:
        raise RuntimeError('Use a new directory for a changed audit')
    save(output/'identity.json',identity)
    commands=[]
    cases=[]
    for i, candidate in enumerate(candidates):
        for j, arena in enumerate(arenas):
            name=f'candidate_{i}_case_{j}'
            portfolio = candidate.get('searchVersion',1) == 2
            horizon = 120 if portfolio else 90 if candidate.get('stateModel') else 60
            commands += episode_commands(candidate,seed+j,arena,opponent,horizon,name)[:-1]
            commands += [{'op':'set_spawn_paused','value':True},
                         {'op':'plan_ice_attack'}, {'op':'dump_state','name':name+'_prediction.json'},
                         {'op':'set_cold_storage','state':{'decisionRemaining':60}},
                         {'op':'set_spawn_paused','value':False},
                         {'op':'commander_episode','playerActions':not static_defense,'opponent':opponent,'seconds':31,'name':name+'_dispatch','timeout':61}]
            # 新版最晚 60 秒出生。中途只延后下次 AI 购买，不改资源/已付队列，避免第 60 秒混入另一案。
            if portfolio:
                commands += [{'op':'set_cold_storage','state':{'decisionRemaining':60}},
                             {'op':'commander_episode','playerActions':not static_defense,'opponent':opponent,'seconds':30,'name':name+'_late_dispatch','timeout':60}]
            commands += [{'op':'dump_state','name':name+'_freeze.json'},
                         {'op':'set_spawn_paused','value':True},
                         {'op':'commander_episode','playerActions':not static_defense,'opponent':opponent,'seconds':horizon-(61 if portfolio else 31),'name':name,'timeout':horizon}]
            cases.append((name,arena))
    commands.append({'op':'quit'})
    script=output/(output.name+'.json')
    save(script,{'muteAudio':True,'batchStepsPerFrame':32,'commands':commands})
    subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
                    str(ROOT/'autotest/run_commander_batch.ps1'),'-GameDirectory',str(game),'-Script',str(script)],check=True)
    evidence=game/'autotest/out'/output.name
    assert json.loads((evidence/'status.json').read_text())['status']=='passed'
    rows=[]
    for name,arena in cases:
        before=json.loads((evidence/(name+'_prediction.json')).read_text())['coldStorage']
        freeze=json.loads((evidence/(name+'_freeze.json')).read_text())['coldStorage']
        dispatch=json.loads((evidence/(name+'_dispatch.json')).read_text())
        late_path=evidence/(name+'_late_dispatch.json')
        late=json.loads(late_path.read_text()) if before.get('searchVersion',1)==2 else None
        result=json.loads((evidence/(name+'.json')).read_text())
        after=result['final']['coldStorage']
        assert freeze['pendingCount']==0 or (late or dispatch)['outcome']!='timeout', 'Live matches must dispatch every paid unit before freezing'
        assert freeze['spent']==before['spent'], 'Dispatch phase must not buy an extra plan'
        assert after['spent']==before['spent'], 'Audit must not mix in later purchases'
        assert result['final']['testAudio']['muted']
        assert result['playerActions']==(not static_defense) and dispatch['playerActions']==(not static_defense)
        if late:
            assert late['playerActions']==(not static_defense) and late['final']['coldStorage']['spent']==before['spent']
            if static_defense: assert not late['playerPlantings']
        if static_defense: assert not result['playerPlantings'] and not dispatch['playerPlantings']
        # 交战可延长到 90/120 秒，但产冰特征仍只统计前 60 秒。
        actual_production = sum(e['amount'] for e in after['productionEvents']
                                if before['elapsed'] < e['at'] <= before['elapsed']+60)
        plantings=dispatch['playerPlantings'].copy()
        if late:
            for kind,count in late['playerPlantings'].items(): plantings[kind]=plantings.get(kind,0)+count
        for kind,count in result['playerPlantings'].items():
            plantings[kind]=plantings.get(kind,0)+count
        rows.append({'case':name,'arena':arena,'predicted':before['searchFeatures'],
                     'baseline':before['searchBaselineFeatures'],'preferenceScore':before['searchPreferenceScore'],
                     'plan':before['pending'],'spent':before['commanderSpent'],
                     'actualIncome':actual_production,
                     'actualKills':after['killIncome']-before['killIncome'],
                     'survivingPaid':sum(p['cost'] for p in after['refundableCosts'])+sum(p['cost'] for p in after['pending']),
                     'playerPlantings':plantings,'result':str(evidence/(name+'.json')),
                     'dispatchResult':str(evidence/(name+'_dispatch.json'))})
    save(output/'report.json',{'identity':identity,'cases':rows,
         'limitation':('Static-defense diagnostic with later player inputs disabled; not a strength evaluation.' if static_defense else
                       'Single-plan calibration: later player actions are real; predicted policy is approximate, not an identical replay.')})
    for row in rows:
        print(row['case'],row['arena'],'spent',row['spent'],'income predicted/actual',round(row['predicted'][4]),row['actualIncome'],
              'kills predicted/actual',round(row['predicted'][0]),row['actualKills'],'preference',round(row['preferenceScore']))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--policy',type=Path,help='Inspect this frozen policy only')
    parser.add_argument('--arenas',nargs='+')
    parser.add_argument('--opponent',default='adaptive')
    parser.add_argument('--static-defense',action='store_true',help='Disable later player inputs for diagnostic isolation, never a strength evaluation')
    parser.add_argument('--seed',type=int,default=None)
    args=parser.parse_args()
    audit(args.output,args.policy,args.arenas,args.opponent,args.seed,args.static_defense)
