"""Verify real ladder portraits, shared finite movement and formal paid ash cleanup."""
import argparse
import json
from pathlib import Path


def verify(folder):
    """Use stable IDs and positions relative to the untouched wall, not post-wait absolute coordinates."""
    read=lambda name: json.loads((folder/(name+'.json')).read_text(encoding='utf-8-sig'))
    assert read('status')['status']=='passed'
    assert 'script finished OK' in (folder/'run.log').read_text(encoding='utf-8-sig')
    before,after,cleared=(read(name) for name in ('before','after','cleared'))
    portrait=read('portrait')
    assert portrait['boardUnchanged']
    plan=portrait['candidatePlans'][0]
    assert plan['legal'] and plan['ladderPlaced']>=1 and plan['ladderClimbs']>=2
    births=[p for p in portrait['legalOptions'] if p['type']=='ZOMBIE_LADDER']
    assert births and all(p['birthLadder'] and p['birthCanClimb'] and p['birthLadderPlacementSeconds']>0 for p in births)
    old={p['id']:p for p in before['plants']}
    new={p['id']:p for p in after['plants']}
    front=[p for p in old.values() if (p['row'],p['col'])==(2,5)]
    assert len(front)==2 and all(new[p['id']]['health']==p['health'] for p in front)
    rear=[p for p in old.values() if (p['row'],p['col'])==(2,1)]
    assert sum(new.get(p['id'],{}).get('health',0) for p in rear)<sum(p['health'] for p in rear)
    wall=after['cells'][2][5]['centerXInt']
    ids={z['id'] for z in before['zombies']}
    passed=[z for z in after['zombies'] if z['id'] in ids and z['bodyHealth']>0 and z['xInt']<wall-30]
    assert len(passed)==2 and after['ladderCount']==1
    assert cleared['ladderCount']==0
    assert after['coldStorage']['playerIce']-cleared['coldStorage']['playerIce']==after['coldStorage']['plantCosts']['PLANT_JALAPENO']
    result={'passed':True,'realBirthDuration':True,'sharedPathWithIntactWall':True,
            'formalPaidAshCleanup':True,'normalMatchStrengthVerified':False}
    (folder/'ladder_forecast_verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder',type=Path)
    verify(parser.parse_args().folder)
