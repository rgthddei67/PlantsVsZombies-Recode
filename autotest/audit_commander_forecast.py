"""Measure a single purchased plan for 60 real seconds with later AI purchases disabled."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from train_cold_storage import ROOT, INITIAL, episode_commands, save


def audit(output):
    """Keep subsequent reinforcement out of forecast/actual income comparisons."""
    game = ROOT / 'build/clang-release'
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    policy = json.loads((game/'resources/ai/cold_storage_policy.json').read_text())
    candidates = [policy, {'weights':INITIAL,'preferences':{}},
                  {'weights':policy['weights'],'preferences':{}}]
    identity = {'exeSha256':hashlib.sha256((game/'PlantsVsZombies.exe').read_bytes()).hexdigest(),
                'coreSha256':hashlib.sha256((ROOT/'autotest/train_cold_storage.py').read_bytes()).hexdigest(),
                'candidates':candidates}
    if (output/'identity.json').exists() and json.loads((output/'identity.json').read_text()) != identity:
        raise RuntimeError('Use a new directory for a changed audit')
    save(output/'identity.json',identity)
    commands=[]
    cases=[]
    for i, candidate in enumerate(candidates):
        for j, arena in enumerate(('developing','fortress','fortress_10_2')):
            name=f'candidate_{i}_case_{j}'
            commands += episode_commands(candidate,25001+j,arena,'adaptive',60,name)[:-1]
            commands += [{'op':'set_spawn_paused','value':True},
                         {'op':'plan_ice_attack'}, {'op':'dump_state','name':name+'_prediction.json'},
                         {'op':'set_cold_storage','state':{'decisionRemaining':10000}},
                         {'op':'set_spawn_paused','value':False},
                         {'op':'commander_episode','opponent':'adaptive','seconds':60,'name':name,'timeout':90}]
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
        result=json.loads((evidence/(name+'.json')).read_text())
        after=result['final']['coldStorage']
        assert after['spent']==before['spent'], 'Audit must not mix in later purchases'
        assert result['final']['testAudio']['muted']
        rows.append({'case':name,'arena':arena,'predicted':before['searchFeatures'],
                     'baseline':before['searchBaselineFeatures'],'preferenceScore':before['searchPreferenceScore'],
                     'plan':before['pending'],'spent':before['commanderSpent'],
                     'actualIncome':after['workerIncome']-before['workerIncome'],
                     'actualKills':after['killIncome']-before['killIncome'],
                     'survivingPaid':sum(p['cost'] for p in after['refundableCosts'])+sum(p['cost'] for p in after['pending']),
                     'playerPlantings':result['playerPlantings'],'result':str(evidence/(name+'.json'))})
    save(output/'report.json',{'identity':identity,'cases':rows,
         'limitation':'Single-plan calibration: later player actions are real; predicted policy is approximate, not an identical replay.'})
    for row in rows:
        print(row['case'],row['arena'],'spent',row['spent'],'income predicted/actual',round(row['predicted'][4]),row['actualIncome'],
              'kills predicted/actual',round(row['predicted'][0]),row['actualKills'],'preference',round(row['preferenceScore']))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    audit(parser.parse_args().output)
