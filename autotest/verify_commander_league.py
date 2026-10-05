"""Verify frozen league inputs, legal ledgers, scoring versions and held-out separation."""
import argparse
import hashlib
import json
import math
from pathlib import Path

from train_cold_storage import save
from commander_opening_review import is_opening


MAXIMUM_DEPLOYMENT = 192  # 正式最高同时部署名额；当前资本缩水不追溯取消已付款队列
BASE_DEPLOYMENT = 64  # 正式基础同时部署名额，不能作为所有队列的固定上限
PRECISION_TARGET_LIMIT = 3  # 同批独立目标上限；每株收费，共用瞄准和冷却
PRECISION_ICE_COST = 60  # 每株正式狙击费用，冰块


def conditioned_weights(policy, inputs):
    """Reconstruct the base/state-model stage before the shared currency conversion."""
    weights=policy['weights'][:]
    if policy.get('stateModel'):
        weights=[max(-500,min(500,w+sum(x*r[j] for x,r in zip(inputs,policy['stateModel']['coefficients']))))
                 for j,w in enumerate(weights)]
    return weights


def capital_utility_scale(inputs):
    """Reconstruct the native cubic curve only from independently supplied snapshot inputs."""
    budget=inputs['budget']
    capacity=max(0,min(inputs['capacity']+(3 if inputs['weatherStation'] else 0),
                       MAXIMUM_DEPLOYMENT+(3 if inputs['weatherStation'] else 0)))
    future=inputs.get('fundableUnlockTroopCost',0)
    assert 0<=future<=budget
    highest=max(inputs['highestAffordableTroopCost'],future)
    if budget<=0 or highest<=0 or capacity<=0:
        return 1.0
    reserve=highest*capacity+max(0,inputs['recoveryReserve'])
    return max(.01,min(1.0,min(1.0,reserve/budget)**3))


def effective_weights(policy, inputs, utility_scale=1.0):
    """Check the shared ice conversion; a supplied scale is not evidence of its source inputs."""
    weights=conditioned_weights(policy,inputs)
    if policy.get('netEconomy'):
        value=max(.01,min(500,abs(weights[4])))*max(.01,min(1.0,utility_scale))
        # Native AccountForIce intentionally does not clip this sum again: cash retains one price.
        weights[0]+=value
        weights[3]=value*max(0,min(1,weights[3]))
        weights[4],weights[5]=value,-value
        weights[6]=-abs(weights[6])
    return weights


def precision_targets(main, additional):
    """Verify the public transaction identities without converting invalid extra IDs into shots."""
    assert isinstance(additional,list)
    if main<=0:
        assert not additional
        return []
    targets=[main]+additional
    assert 1<=len(targets)<=PRECISION_TARGET_LIMIT
    assert all(isinstance(target,int) and not isinstance(target,bool) and target>0 for target in targets)
    assert len(set(targets))==len(targets)
    return targets


def precision_intents(skill):
    """Separate currently paid intent from an unlock-after-purchase numerical forecast."""
    actual=precision_targets(skill.get('precisionTarget',0),skill.get('precisionAdditionalTargets',[]))
    future=precision_targets(skill.get('forecastPrecisionTarget',0),skill.get('forecastPrecisionAdditionalTargets',[]))
    fee=skill.get('forecastPrecisionIce',0)
    start=skill.get('forecastPrecisionAimStartSeconds',0)
    assert math.isfinite(fee) and math.isfinite(start)
    if future:
        assert not actual and fee==len(future)*PRECISION_ICE_COST and start>0
    else:
        assert fee==0 and start==0
    return actual,future,fee


def future_followup_evidence(decision, previous, trace):
    """A consecutive committed search must advance a paid wave; sparse logs cannot prove a purchase."""
    if decision['serial']!=previous['serial']+1:
        return False
    # 正式波次只由新增成功付费队列推进；连续serial排除漏记中间采购造成的假归因。
    assert decision['wave']>previous['wave']
    if any(p.get('wave')==decision['wave'] and p['cost']>0 for p in decision.get('pending',[])):
        return True
    # 零延迟成员可能已出生，只有同一serial的实际付款观测可作为补充。
    return any(t['ice'].get('searchSerial')==decision['serial'] and t['ice'].get('commanderSpent',0)>0
               for t in trace)


def verify_focused_scope(evaluation, candidate):
    """A mixed 11-6 holdout may pass its local gate but cannot claim full/masked chapter coverage."""
    gate=evaluation['gate']
    assert gate['scope']=='focused_mixed_11_6' and not gate['chapterGatePassed']
    allowed={('normal:opening_11_6','ice_bunker_mixed'),('normal:opening_10_6','pine_elite')}
    assert all(tuple(case[:2]) in allowed for case in evaluation['cases'])
    assert gate['full']['games']==gate['masked']['games']==0
    old,new=evaluation['scores'][:2]
    assert len(old)==len(new)==len(evaluation['cases'])
    gains=0
    for key,arena,opponent,minimum in (
            ('focusedMixed','normal:opening_11_6','ice_bunker_mixed',3),
            ('transfer','normal:opening_10_6','pine_elite',1)):
        rows=[(a,b,c) for a,b,c in zip(old,new,evaluation['cases']) if tuple(c[:2])==(arena,opponent)]
        observed=gate[key]
        old_wins=sum(a['outcome']=='commander_win' for a,b,c in rows)
        new_wins=sum(b['outcome']=='commander_win' for a,b,c in rows)
        lost=sum(a['outcome']=='commander_win' and b['outcome']!='commander_win' for a,b,c in rows)
        seeds=len({c[2] for a,b,c in rows})
        assert (observed['games'],observed['independentSeeds'],observed['oldWins'],observed['newWins'],observed['lostWins']) == (len(rows),seeds,old_wins,new_wins,lost)
        assert observed['passed']==(seeds>=minimum and new_wins>=old_wins and lost==0)
        gains+=new_wins-old_wins
    passed=gate['focusedMixed']['passed'] and gate['transfer']['passed'] and gains>=2
    for comparison in ('reference','stateAblation'):
        if comparison in gate:
            assert gate[comparison]['scope']=='focused_mixed_11_6' and not gate[comparison]['chapterGatePassed']
            passed=passed and gate[comparison]['passed']
    assert gate['newWinGain']==gains and gate['passed']==passed
    assert not candidate['leagueGatePassed'] and candidate['focusedGatePassed']==gate['passed']


def verify(directory):
    """Verify every completed engine batch without treating a passed test as a passed strength gate."""
    read=lambda p:json.loads(Path(p).read_text(encoding='utf-8'))
    identity=read(directory/'identity.json')
    for path,digest in identity['hashes'].items():
        assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest,path
    evaluation=read(directory/'evaluation.json')
    checkpoint=read(directory/'checkpoint.json')
    history=checkpoint['history']
    assert evaluation['policies'][1]==read(directory/'frozen_policy.json')==checkpoint['champion']
    skipped=evaluation['gate'].get('reason')=='unchanged_policy'
    if skipped:
        assert evaluation['policies'][0]==evaluation['policies'][1]
        assert not evaluation['cases'] and evaluation['scores']==[[],[]] and not evaluation['gate']['passed']
    if identity.get('curriculum')=='mixed' and not skipped:
        verify_focused_scope(evaluation,read(directory/'candidate_policy.json'))
    if identity.get('curriculum') in ('openings','coached'):
        assert all(is_opening(c[0]) for c in evaluation['cases'])
        assert all(is_opening(c[0]) for h in history for c in h['cases'])
        if identity.get('origin')!='fresh' and not skipped:
            diagnostics=read(directory/'diagnostics.json')
            assert diagnostics['informationalOnly'] and diagnostics['policies']==evaluation['policies']
            assert all(not is_opening(c[0]) for c in diagnostics['cases'])
    if identity.get('origin')=='fresh':
        paired=Path(identity['pairedTraining'])
        warm=read(paired/'checkpoint.json')['history']
        initial=read(directory/'initial_policy.json')
        assert len(history)==len(warm)
        assert all(a['cases']==b['cases'] and len(a['population'])==len(b['population']) for a,b in zip(history,warm))
        assert not any(any(row) for row in initial['preferences'].values())
        assert not any(any(row) for row in initial['stateModel']['coefficients'])
        assert 'productionCalibration' not in initial
        assert all('productionCalibration' not in p for h in history for p in h['population'])
        # Finalists face a newly drawn common test, not either arm's selection cases or the warm test reused for tuning.
        previous_seeds={c[2] for h in warm for c in h['cases']}
        previous_seeds.update(c[2] for c in read(paired/'evaluation.json')['cases'])
        assert not previous_seeds.intersection(c[2] for c in evaluation['cases'])
    if identity.get('stateOnly') and history:
        fixed=lambda p:{k:v for k,v in p.items() if k!='stateModel'}
        anchor=fixed(history[0]['population'][0])
        assert all(fixed(p)==anchor for h in history for p in h['population'])
    if identity.get('netEconomy'):
        assert evaluation['policies'][1]['netEconomy']
        assert all(p.get('netEconomy') for h in history for p in h['population'])
    checked_folders=set(); seeds={'training':set(),'holdout':set(),'diagnostic':set()}
    games=decisions=net_decisions=0; wall=0
    capital_curves=missing_capital_inputs=pending_above_current_limit=0
    precision_casts={1:0,2:0,3:0}
    future_precision={1:0,2:0,3:0}
    future_paid_followups=future_missing_purchase_inputs=0
    for score_file in directory.glob('*_scores.json'):
        batch=score_file.name.removesuffix('_scores.json')
        script=read(directory/(batch+'.json')); cached=read(score_file)
        assert hashlib.sha256(json.dumps(script,sort_keys=True).encode()).hexdigest()==cached['fingerprint']
        wall+=cached['wallSeconds']
        policies={}; current=None
        for c in script['commands']:
            if c['op']=='commander_experiment':
                current=c
                split='holdout' if batch.endswith('_holdout') else 'diagnostic' if batch.endswith('_fixtures') else 'training'
                seeds[split].add(c['seed'])
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
                    # 只检查公开状态边界：费用可能缩减当前容量，已付款队列不会被追溯撤销。
                    # 免费召唤可使hostileCount超限；它也不能冒充已付款新队列的名额来源。
                    assert BASE_DEPLOYMENT<=ice['deploymentLimit']<=MAXIMUM_DEPLOYMENT
                    assert 0<=ice['pendingCount']<=MAXIMUM_DEPLOYMENT
                    pending_above_current_limit+=ice['pendingCount']>ice['deploymentLimit']
                    assert all(p['cost']>0 and p['remaining']>=0 for p in ice['pending'])
                    precision_targets(ice.get('strikeTargetID',-1),ice.get('strikeAdditionalTargetIDs',[]))
                previous={'serial':result['initial']['coldStorage'].get('searchSerial',0),
                          'wave':result['initial']['coldStorage']['decisions']}
                for d in result['decisions']:
                    logged=d['effectiveWeights']
                    assert len(logged)==len(d['features'])==8 and all(math.isfinite(v) for v in logged+d['features'])
                    scale=1.0
                    if policy.get('netEconomy'):
                        base=conditioned_weights(policy,d['stateInputs'])
                        unscaled=max(.01,min(500,abs(base[4])))
                        # 当前episode缺少采样容量和最高合法可付兵价；观察到的折价仅用于
                        # 校验各货币项同价及有界关系，不冒称独立重建了资本曲线。
                        scale=logged[4]/unscaled
                        assert .01-.00001<=scale<=1+.00001,path
                        if 'cashScaleInputs' in d:
                            independent=capital_utility_scale(d['cashScaleInputs'])
                            assert abs(scale-independent)<max(.00001,abs(independent)*.00001),path
                            capital_curves+=1
                            scale=independent
                        else:
                            missing_capital_inputs+=1
                    weights=effective_weights(policy,d['stateInputs'],scale)
                    assert bool(policy.get('netEconomy'))==d.get('netEconomy',False)
                    assert bool(policy.get('anticipateBuilding'))==d.get('anticipateBuilding',False)
                    assert bool(policy.get('anticipateEconomy'))==d.get('playerEconomy',{}).get('enabled',False)
                    assert policy.get('searchVersion',1)==d.get('searchVersion',1)
                    assert all(abs(a-b)<max(.002,abs(b)*.000003) for a,b in zip(logged,weights)),path
                    skill=d.get('specialAbilities',{})
                    targets,future,future_fee=precision_intents(skill)
                    # 评分算式由公开有效权重/特征独立重加；不把缺失的快照输入藏进评分校验。
                    expected=sum(a*b for a,b in zip(d['features'],logged))+d['preferenceScore']
                    predicted_precision_fee=len(targets)*PRECISION_ICE_COST+future_fee
                    if predicted_precision_fee and not d.get('netEconomy'):
                        expected-=max(0,1+logged[5])*predicted_precision_fee
                    opponent=d.get('opponent',{'assets':0,'baselineAssets':0,'weight':0,'score':0})
                    assert abs(opponent['weight']-policy.get('opponentWeight',0))<.0001
                    pressure=opponent['weight']*(opponent['baselineAssets']-opponent['assets'])
                    assert abs(opponent['score']-pressure)<max(.03,abs(pressure)*.00001)
                    expected+=opponent['score']
                    assert abs(expected-d['scoreOn100']/100)<max(.05,abs(expected)*.000005),path
                    if d.get('netEconomy'):
                        assert d['effectiveWeights'][5]==-d['effectiveWeights'][4]<0
                        assert d['effectiveWeights'][6]<=0
                        net_decisions+=1
                    if predicted_precision_fee:
                        assert d['features'][5]+.001>=predicted_precision_fee,path
                    if targets:
                        precision_casts[len(targets)]+=1
                    if future:
                        future_precision[len(future)]+=1
                        if future_followup_evidence(d,previous,result['trace']):
                            future_paid_followups+=1
                        else:
                            future_missing_purchase_inputs+=1
                    previous=d
                    decisions+=1
                games+=1
    assert not seeds['training'].intersection(seeds['holdout'])
    assert not seeds['diagnostic'].intersection(seeds['holdout'] | seeds['training'])
    report={'games':games,'decisions':decisions,'netEconomyDecisions':net_decisions,'wallSeconds':wall,
            'allMuted':True,'ledgersAndScoresVerified':True,'holdoutSeparated':True,
            'capitalCurveIndependentlyVerified':net_decisions>0 and missing_capital_inputs==0,
            'capitalCurveDecisionsVerified':capital_curves,'capitalCurveDecisionsMissingInputs':missing_capital_inputs,
            'capitalCurveLimitation':'missing snapshot capacity/affordable troop cost; shared currency conversion only'
                                    if missing_capital_inputs else None,
            'deploymentVerification':'public state bounds only; purchase admission covered by dedicated engine tests',
            'pendingAboveCurrentLimitObservations':pending_above_current_limit,
            'precisionDecisionsByTargetCount':precision_casts,
            'forecastPrecisionDecisionsByTargetCount':future_precision,
            'forecastPaidFollowupEvidenceDecisions':future_paid_followups,
            'forecastPurchaseEvidenceMissingDecisions':future_missing_purchase_inputs,
            'forecastFeesNotPrepaidIndependentlyVerified':False if any(future_precision.values()) else None,
            'forecastPaymentLimitation':'future fees verified only in forecast; exact live purchase/ability split is not logged'
                                        if any(future_precision.values()) else None,
            'scope':evaluation['gate'].get('scope','focused_mixed_11_6' if identity.get('curriculum')=='mixed' else 'chapter_league'),
            'chapterGatePassed':evaluation['gate'].get('chapterGatePassed',evaluation['gate']['passed']),
            'evaluationSkipped':skipped,'gate':evaluation['gate']}
    save(directory/'verification.json',report)
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    verify(parser.parse_args().directory)
