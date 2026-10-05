"""Verify a real paid unlock transaction, separate from natural battle results."""
import json
import math
import sys
from pathlib import Path

folder=Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/clang-release/autotest/out/smoke_commander_precision_unlock')
assert json.loads((folder/'status.json').read_text())['status']=='passed'
assert 'script finished OK' in (folder/'run.log').read_text(encoding='utf-8-sig')
states=[json.loads((folder/(name+'.json')).read_text()) for name in ('before','forecast','paid','resolved')]
before,forecast,paid,resolved=states
for state in states:
    c=state['coldStorage']
    assert c['enemyIce']==c['initialEnemyIce']+c['supplied']+c['workerIncome']+c['killIncome']-c['spent']
b,f,p,r=[state['coldStorage'] for state in states]
assert b['decisions']==9 and f['decisions']==10
assert f['searchPrecisionTargetID']==0 and not f['searchPrecisionAdditionalTargetIDs']
assert f['strikeTargetID']<0 and not f['strikeAdditionalTargetIDs']
assert f['strikeCooldownRemaining']==0
future=[f['searchForecastPrecisionTargetID']]+f['searchForecastPrecisionAdditionalTargetIDs']
assert 1<=len(future)<=3 and len(set(future))==len(future) and all(i>0 for i in future)
assert f['searchForecastPrecisionIce']==60*len(future)
assert math.isfinite(f['searchForecastPrecisionAimStartSeconds']) and f['searchForecastPrecisionAimStartSeconds']>0
scale=before['timeScaleOn1000']/1000
actual_budget=f['planningBudgetMs']
assert abs(actual_budget*scale-600)<.002, 'the dispatcher must divide its base budget by time scale'
expected_start=3+2*actual_budget*scale/1000
assert abs(f['searchForecastPrecisionAimStartSeconds']-expected_start)<.002, 'prediction must use the actual worker budget in game seconds'
assert not b['pending'] and not before['zombies']
army=sum(unit['cost'] for unit in f['pending'])
assert army>0 and f['commanderSpent']==army and f['spent']-b['spent']==army
assert b['enemyIce']-f['enemyIce']==army, 'the future skill must not be prepaid'
assert forecast['plantCount']==before['plantCount']
assert f['enemyIce']>=f['searchForecastPrecisionIce']
targets=[p['strikeTargetID']]+p['strikeAdditionalTargetIDs']
assert 1<=len(targets)<=3 and len(set(targets))==len(targets) and all(i>0 for i in targets)
assert not p['searchForecastPrecisionTargetID'] and p['searchForecastPrecisionIce']==0
assert p['strikeCooldownRemaining']>0 and p['strikeAimRemaining']>0
assert not forecast['zombies'] and not paid['zombies'], 'paused deployment keeps the paid queue intact'
assert len(p['pending'])>=len(f['pending'])
new_army=sum(unit['cost'] for unit in p['pending'])-army
assert new_army>=0
assert p['spent']-f['spent']==60*len(targets)+new_army
assert p['searchPrecisionEvaluated']>0, 'the unlocked decision must search its current world again'
alive={plant['id'] for plant in resolved['plants']}
assert not alive.intersection(targets) and r['strikeTargetID']<0 and not r['strikeAdditionalTargetIDs']
assert r['spent']==p['spent'], 'resolving paid strikes must not charge twice'
original={plant['id']:plant for plant in paid['plants']}
refund=sum(p['plantCosts'][original[i]['placementType']]*(p['difficulty']+2)//4 for i in targets)
assert r['killIncome']-p['killIncome']==refund
report={'passed':True,'timeScale':scale,'actualPlanningBudgetMs':actual_budget,
        'forecastAimStartGameSeconds':f['searchForecastPrecisionAimStartSeconds'],
        'troopFee':army,'forecastSkillFee':f['searchForecastPrecisionIce'],
        'actualSkillFee':60*len(targets),'futureFeesNotPrepaid':True,'freshSearchEvaluated':True,
        'actualTargetsDifferentFromForecast':set(targets)!=set(future)}
(folder/'precision_unlock_verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report))
