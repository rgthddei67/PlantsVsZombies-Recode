"""Verify passive real-time recording without allowing mailbox gameplay actions."""
import json
from pathlib import Path
import time
from live import LiveClient


def verify(output):
    """Check that the real clock runs, observer commands cannot play, and records survive quit."""
    client=LiveClient(output)
    first=client.send([{'op':'observe'}])
    time.sleep(2.5)
    second=client.send([{'op':'advance','steps':120},{'op':'player_plant','slot':0,'row':0,'col':0}])
    assert all(r['reason']=='human_observation_read_only' for r in second['results'])
    assert second['state']['coldStorage']['elapsed']>first['state']['coldStorage']['elapsed']+1
    assert not second['state']['plants']
    assert second['state']['coldStorage']['productionRules']['maximumYield']==18
    assert second['state']['coldStorage']['productionRules']['initialYield']==4
    records=[json.loads(line) for line in (client.mailbox/'observations.jsonl').read_text().splitlines()]
    assert len(records)>=2 and records[-1]['simulationSteps']>records[0]['simulationSteps']
    assert all(row['state']['testAudio']['muted'] for row in records)
    client.send([{'op':'quit'}])
    print('Passive time, read-only mailbox, new production rules and persistent recording passed.')

if __name__=='__main__':
    verify(Path('build/clang-release/autotest/out/human_observation_contract'))
