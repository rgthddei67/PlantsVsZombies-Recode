"""Verify singleton legality and future replacement sampling without board mutation."""
import json
import sys
from pathlib import Path

folder = Path(sys.argv[1])
read = lambda name: json.loads((folder / (name + '.json')).read_text(encoding='utf-8-sig'))
assert read('status')['status'] == 'passed'
assert 'script finished OK' in (folder / 'run.log').read_text(encoding='utf-8-sig')
before, after = read('before'), read('after')
assert before['plantCount'] == after['plantCount'] == 2
assert before['normalPlantsByCell'] == after['normalPlantsByCell']
assert before['coldStorage']['enemyIce'] == after['coldStorage']['enemyIce']
alive, dead = read('alive'), read('dead')
assert alive['boardUnchanged'] and dead['boardUnchanged']
assert alive['futureRowStrikeCells'] == alive['futurePlanternCells'] == 1
assert dead['futureRowStrikeCells'] > 1 and dead['futurePlanternCells'] == 1
report = {'passed': True, 'realSingletonRulePreserved': True, 'futureReplacementRetained': True, 'readOnly': True}
(folder / 'unique_replacement_verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report))
