"""Verify measured move/bite multipliers, legal vault landing and almanac text."""
from pathlib import Path
import json

OUT=Path(__file__).resolve().parents[1]/'build/clang-release/autotest/out'


def read(suite,name):
    return json.loads((OUT/suite/name).read_text(encoding='utf-8'))


def main():
    suite='smoke_mine_fourth_interactions'
    baseline=read(suite,'move_baseline.json')['zombies'][0]
    for name,multiplier in [('move_drum.json',1.5),('move_golden.json',3.0)]:
        target=read(suite,name)['zombies'][0]
        assert target['id']==baseline['id']
        assert abs(target['horizontalMoveSpeedOn1000']-baseline['horizontalMoveSpeedOn1000']*multiplier)<=3
    for name,multiplier in [('bite_drum.json',1.75),('bite_golden.json',3.5)]:
        target=read(suite,name)['zombies'][0]
        assert target['isEating'] and target['currentBiteDamage']==50
        assert target['animExtraSpeedPct']==round(multiplier*100)
    state=read(suite,'elite_vault.json')
    assert {z['type'] for z in state['zombies']}=={'ZOMBIE_ELITE_POLEVAULTER','ZOMBIE_POLEVAULTER'}
    for zombie in state['zombies']:
        cell=zombie['row']*9+zombie['gridColumnOn1000']//1000
        assert not state['mine']['rocks'][cell] and state['mine']['connected'][cell]
    reward=read('smoke_mine_fourth_reward','reward.json')
    assert reward['plantAlmanacName']=='琥珀地衣' and reward['plantAlmanacDescriptionLineCount']>0
    almanac=read('smoke_mine_fourth_visuals','drummer_almanac.json')['zombieAlmanacInfo']
    assert almanac['name']=='震晶鼓手僵尸' and almanac['descriptionLineCount']>0
    for suite in ('smoke_mine_fourth','smoke_crystal_drummer','smoke_mine_fourth_waves',
                  'smoke_mine_fourth_reward','smoke_mine_fourth_interactions','smoke_mine_fourth_visuals'):
        assert read(suite,'status.json')['status']=='passed',suite
    print('Mine fourth: movement, bite frequency, golden ice, legal landings, text and suite statuses passed')


if __name__=='__main__':
    main()
