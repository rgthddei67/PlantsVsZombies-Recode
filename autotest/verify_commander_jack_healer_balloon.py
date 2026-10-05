"""Compare numeric ability portraits with controlled formal game transactions."""
import json
from pathlib import Path
import sys


def verify(folder):
    """Check stable units, real healing/damage and a forecast that leaves Board unchanged."""
    folder=Path(folder)
    load=lambda name: json.loads((folder/(name+'.json')).read_text(encoding='utf-8'))
    assert load('status')['status']=='passed'
    reports={name:load(name+'_forecast') for name in ('jack','elite','healer','balloon')}
    result={}
    for name,report in reports.items():
        assert report['boardUnchanged']
        result[name]=report['candidatePlans'][0]
        assert result[name]['legal']
    before,after=load('jack_before'),load('jack_after')
    assert not after['plants'] and not after['zombies']
    assert result['jack']['jackExplosions']==1
    assert result['jack']['features'][0]==after['coldStorage']['killIncome']-before['coldStorage']['killIncome']
    assert load('elite_before')['plants'][0]['health']-load('elite_after')['plants'][0]['health']==50
    assert result['elite']['jackThrows']>0 and result['elite']['jackBoxHits']>0
    flight=load('elite_flight')
    assert flight['zombies'][0]['eliteJackBoxInFlight']
    cancelled=load('elite_cancelled')
    assert not cancelled['zombies'] and cancelled['plants'][0]['health']==300
    assert load('elite_flight_forecast')['boardUnchanged']
    worker=lambda state:next(z for z in state['zombies'] if z['type']=='ZOMBIE_ICE_WORKER')
    wounded,healed=worker(load('healer_before')),worker(load('healer_after'))
    assert wounded['id']==healed['id'] and wounded['bodyHealth']==250 and healed['bodyHealth']==500
    assert result['healer']['healerAmount']==250 and result['healer']['healerRecipients']==1
    balloon=next(z for z in load('balloon_after')['zombies'] if z['type']=='ZOMBIE_BALLOON')
    assert balloon['bodyHealth']==270 and balloon['balloonHealth']==20 and balloon['balloonPhase']=='FLYING'
    assert result['balloon']['features'][0]==0 and result['balloon']['features'][1]==0
    ground=next(z for z in load('balloon_ground')['zombies'] if z['type']=='ZOMBIE_BALLOON')
    assert ground['id']==balloon['id'] and ground['balloonPhase']=='WALKING' and ground['balloonHealth']==0
    assert ground['bodyHealth']<270
    for name in ('blover_card_forecast','blover_live_forecast','magnet_forecast'):
        assert load(name)['boardUnchanged'] and load(name)['candidatePlans'][0]['legal']
    for name in ('blover_card_forecast','blover_live_forecast'):
        assert load(name)['candidatePlans'][0]['features'][2:4]==[0,0]
    assert not load('blover_after')['zombies']
    disarmed=load('magnet_after')['zombies'][0]
    assert disarmed['jackPhase']=='DISARMED' and not disarmed['magneticItemAvailable']
    assert load('magnet_forecast')['candidatePlans'][0]['jackExplosions']==0
    assert load('magnet_forecast')['candidatePlans'][0]['magneticExtractions']==1
    print('Formal jack/elite box, 250 worker healing, flight immunity, cactus landing, blover and magnetic disarming matched portraits.')


if __name__=='__main__':
    verify(sys.argv[1])
