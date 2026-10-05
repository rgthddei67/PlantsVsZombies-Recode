"""Verify legitimate double-gold economy outside blocked preferred cells."""
import json
import sys
from pathlib import Path

folder=Path(sys.argv[1])
read=lambda name:json.loads((folder/(name+'.json')).read_text(encoding='utf-8-sig'))
assert read('status')['status']=='passed'
assert 'script finished OK' in (folder/'run.log').read_text(encoding='utf-8-sig')
before,first,mature=read('before'),read('first_state'),read('mature_state')
for state in (first,mature):
    cells={(p['row'],p['col']):p['type'] for p in state['plants'] if p['health']>0}
    assert cells[(0,4)]==cells[(4,4)]=='PLANT_SUNSHROOM'
    assert state['coldStorage']['playerIce']>=20
assert before['cards'][0]['ready'] and before['cards'][1]['ready']
assert first['cards'][0]['cooldown'] and first['cards'][1]['cooldown']
assert read('first')['playerPlantings'].get('PLANT_MARIGOLD',0)>=2
for name in ('first','mature'):
    episode=read(name)
    assert not episode['externalSun']['enabled']
    assert episode['playerActions']
assert mature['coldStorage']['playerIce']<100 and mature['sun']>125
result={'passed':True,'bothRealSlotsUsed':True,'preferredCellsPreserved':True,'reservedAshBudgetPreserved':True}
(folder/'mixed_exchange_space_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))
