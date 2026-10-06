"""验证正式策略确实比较经验候选，诊断购物车使用合法价格且不修改实际钱包。"""
import json
import sys
from pathlib import Path

root=Path(sys.argv[1]) if len(sys.argv)>1 else Path('build/clang-release/autotest/out/smoke_commander_pressure_formations')
for name,costs in [('ranged_formations',{'wait':0,'guarded_shooters':48,'fast_screen':72}),
                   ('existing_front',{'wait':0,'shooters_only':40})]:
    report=json.loads((root/(name+'.json')).read_text(encoding='utf-8'))
    assert report['branches'] and all(branch['experiencedEvaluated']>0 for branch in report['branches'])
    for branch in report['branches']:
        assert sum(a['cost'] for a in branch['actions'])<=report['budget']
    for plan in report['candidatePlans']:
        assert plan['features'][5]==costs[plan['name']],(name,plan['name'],plan['features'])
state=json.loads((root/'real_wallet_unchanged.json').read_text(encoding='utf-8'))
assert state['coldStorage']['spent']==0
assert 'script finished OK' in (root/'run.log').read_text(encoding='utf-8')
print('Published strategy evaluates experienced formations; partner prices, cash and read-only comparisons passed')
