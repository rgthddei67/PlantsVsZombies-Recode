"""Verify frozen league inputs, legal ledgers, scoring versions and held-out separation."""
import argparse
import hashlib
import json
from pathlib import Path

from train_cold_storage import save


def effective_weights(policy, inputs):
    """Independently reconstruct native logged weights, including the optional shared ice value."""
    weights=policy['weights'][:]
    if policy.get('stateModel'):
        weights=[max(-500,min(500,w+sum(x*r[j] for x,r in zip(inputs,policy['stateModel']['coefficients']))))
                 for j,w in enumerate(weights)]
    if policy.get('netEconomy'):
        value=max(.01,min(500,abs(weights[4])))
        weights[0]=max(-500,min(500,weights[0]+value))
        weights[3]=value*max(0,min(1,weights[3]))
        weights[4],weights[5]=value,-value
        weights[6]=-abs(weights[6])
    return weights


def verify(directory):
    """Verify every completed engine batch without treating a passed test as a passed strength gate."""
    read=lambda p:json.loads(Path(p).read_text(encoding='utf-8'))
    identity=read(directory/'identity.json')
    for path,digest in identity['hashes'].items():
        assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest,path
    evaluation=read(directory/'evaluation.json')
    history=read(directory/'checkpoint.json')['history']
    if identity.get('stateOnly') and history:
        fixed=lambda p:{k:v for k,v in p.items() if k!='stateModel'}
        anchor=fixed(history[0]['population'][0])
        assert all(fixed(p)==anchor for h in history for p in h['population'])
    if identity.get('netEconomy'):
        assert evaluation['policies'][1]['netEconomy']
        assert all(p.get('netEconomy') for h in history for p in h['population'])
    checked_folders=set(); seeds={'training':set(),'holdout':set()}
    games=decisions=net_decisions=0; wall=0
    for score_file in directory.glob('*_scores.json'):
        batch=score_file.name.removesuffix('_scores.json')
        script=read(directory/(batch+'.json')); cached=read(score_file)
        assert hashlib.sha256(json.dumps(script,sort_keys=True).encode()).hexdigest()==cached['fingerprint']
        wall+=cached['wallSeconds']
        policies={}; current=None
        for c in script['commands']:
            if c['op']=='commander_experiment':
                current=c
                seeds['holdout' if batch.endswith('_holdout') else 'training'].add(c['seed'])
            elif c['op']=='commander_episode':
                policies[c['name']]=current
        for rows in cached['scores']:
            for row in rows:
                path=Path(row['result']); result=read(path); policy=policies[path.stem]
                if path.parent not in checked_folders:
                    assert read(path.parent/'status.json')['status']=='passed'
                    log=(path.parent/'run.log').read_text(encoding='utf-8')
                    assert 'script finished OK' in log and 'FAIL' not in log
                    assert all('rejected:' not in line or line.endswith(('collect_sun rejected: sun_unavailable',
                               'player_shovel rejected: no_shovel_target')) for line in log.splitlines())
                    checked_folders.add(path.parent)
                assert result['outcome']==row['outcome']
                if row['outcome']=='commander_win': assert result['final']['boardState']=='LOSE_GAME'
                if row['outcome']=='player_win': assert result['final']['coldStorage']['trophySpawned']
                assert result['initial']['testAudio']['muted'] and result['final']['testAudio']['muted']
                assert result.get('playerActions',True), 'Static-defense diagnostics are not league games'
                for ice in [result['initial']['coldStorage'],result['final']['coldStorage']]+[t['ice'] for t in result['trace']]:
                    assert ice['enemyIce']==ice['initialEnemyIce']+ice['supplied']+ice['workerIncome']+ice['killIncome']-ice['spent'],path
                    assert ice['pendingCount']==len(ice['pending'])
                    # 返还账本可暂留临终/被魅惑的对象，不能当活动名额；技能召唤也可越过购买容量。
                    assert ice['pendingCount']<=64
                    assert all(p['cost']>0 and p['remaining']>=0 for p in ice['pending'])
                for d in result['decisions']:
                    weights=effective_weights(policy,d['stateInputs'])
                    assert bool(policy.get('netEconomy'))==d.get('netEconomy',False)
                    assert bool(policy.get('anticipateBuilding'))==d.get('anticipateBuilding',False)
                    assert all(abs(a-b)<max(.002,abs(b)*.000003) for a,b in zip(d['effectiveWeights'],weights)),path
                    expected=sum(a*b for a,b in zip(d['features'],weights))+d['preferenceScore']
                    assert abs(expected-d['scoreOn100']/100)<max(.05,abs(expected)*.000005),path
                    if d.get('netEconomy'):
                        assert d['effectiveWeights'][5]==-d['effectiveWeights'][4]<0
                        assert d['effectiveWeights'][6]<=0
                        net_decisions+=1
                    decisions+=1
                games+=1
    assert not seeds['training'].intersection(seeds['holdout'])
    report={'games':games,'decisions':decisions,'netEconomyDecisions':net_decisions,'wallSeconds':wall,
            'allMuted':True,'ledgersAndScoresVerified':True,'holdoutSeparated':True,'gate':evaluation['gate']}
    save(directory/'verification.json',report)
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    verify(parser.parse_args().directory)
