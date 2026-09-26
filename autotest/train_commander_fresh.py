"""Train from unlearned parameters on a completed warm run's training cases only.

Both frozen finalists face new paired holdouts. Never read the warm holdout to
select parameters, and never copy its preferences, calibration or coefficients.
"""
import argparse
import copy
import hashlib
import random
import secrets
from pathlib import Path

from train_cold_storage import ROOT, INITIAL, run_batch, save
from train_cold_storage_all import CONTEXT, mutate, new_state_model, net_economy_policy
from train_commander_league import read, curriculum_templates, draw_cases, grouped_key, gate, exploratory_restart
from commander_opening_review import review


def initial_policy(names, anticipate_building=True, opponent=False, anticipate_economy=False):
    """Use code defaults and zero learned terms; no checkpoint participates in initialization."""
    policy=net_economy_policy({'weights':INITIAL[:], 'preferences':{name:[0]*len(CONTEXT) for name in names},
                              'stateModel':new_state_model(), 'trainingUnits':list(names),
                              'searchVersion':2, 'anticipateBuilding':anticipate_building,
                              'anticipateEconomy':anticipate_economy})
    if opponent:
        policy['opponentWeight']=0
    return policy


def population(parent, rng, size, generation, restarts):
    """Start with independent objectives, then retain the champion plus local and broad exploration."""
    result=[copy.deepcopy(parent)]
    while len(result)<size:
        if generation==0 or len(result)<=restarts:
            result.append(exploratory_restart(parent,rng,neutral=generation==0 or len(result)%2==0))
        else:
            result.append(mutate(parent,rng,.6 if len(result)%2 else 1.2))
    return result


def train(args):
    """Pair only training scenarios; freeze scratch weights before drawing unseen evaluation cases."""
    warm=args.paired_run.resolve()
    checkpoint=read(warm/'checkpoint.json')
    origin=checkpoint['identity']
    history=checkpoint['history']
    if len(history)!=origin['generations'] or not history:
        raise ValueError('The paired warm training must finish all generations first')
    game=ROOT/'build/clang-release'
    # Pairing requires the exact engine/rules and learning code, not merely similarly named runs.
    for path,digest in origin['hashes'].items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest:
            raise ValueError('Paired input changed: '+path)
    if not (origin.get('netEconomy') and origin.get('searchVersion')==2):
        raise ValueError('Pair requires the net-economy version 2 model')
    output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    previous=read(output/'identity.json') if (output/'identity.json').exists() else {}
    seed=previous.get('seed',secrets.randbelow(2**30))
    tracked=[Path(__file__),warm/'checkpoint.json',warm/'frozen_policy.json',warm/'catalog.json']
    hashes=dict(origin['hashes'])
    hashes.update({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked})
    identity={'schema':1,'seed':seed,'origin':'fresh','pairedTraining':str(warm),
              'netEconomy':True,'searchVersion':2,'generations':len(history),
              'population':origin['population'],'curriculum':origin['curriculum'],
              'seconds':origin['seconds'],'longSeconds':origin['longSeconds'],'hashes':hashes}
    if previous and previous!=identity:
        raise ValueError('Inputs changed; use a new output directory')
    save(output/'identity.json',identity)
    names=read(warm/'catalog.json')['units']
    architecture=read(warm/'frozen_policy.json')
    champion=initial_policy(names,architecture.get('anticipateBuilding',False),'opponentWeight' in architecture,
                            architecture.get('anticipateEconomy',False))
    save(output/'initial_policy.json',champion)
    rng=random.Random(seed)
    fresh_history=[]
    save(output/'checkpoint.json',{'identity':identity,'history':fresh_history,'champion':champion})
    for prior in history:
        generation=prior['generation']
        cases=prior['cases']  # Training seeds only; warm policies/scores never enter selection.
        candidates=population(champion,rng,origin['population'],generation,origin.get('restarts',0))
        scores=run_batch(game,output,output.name+f'_generation_{generation}',candidates,cases,all_zombies=True)
        winner=max(range(len(candidates)),key=lambda i:grouped_key(scores[i],cases))
        champion=copy.deepcopy(candidates[winner])
        fresh_history.append({'generation':generation,'cases':cases,'population':candidates,'scores':scores,'winner':winner})
        save(output/'checkpoint.json',{'identity':identity,'history':fresh_history,'champion':champion})
    save(output/'frozen_policy.json',champion)
    _,_,templates=curriculum_templates(origin['curriculum'])
    # A new entropy stream is independent of both evolutionary searches and the old warm holdout.
    holdout_rng=random.Random(secrets.randbits(64))
    holdout_path=output/'holdout_cases.json'
    cases=read(holdout_path) if holdout_path.exists() else draw_cases(templates,holdout_rng,origin['seconds'],origin['longSeconds'],origin['curriculum'])
    save(holdout_path,cases)
    incumbent=read(game/'resources/ai/cold_storage_policy.json')
    incumbent['trainingUnits']=names
    inherited=read(warm/'frozen_policy.json')
    policies=[incumbent,champion,inherited]
    scores=run_batch(game,output,output.name+'_holdout',policies,cases,all_zombies=True)
    report=gate(scores[0],scores[1],cases)
    report['warmStart']=gate(scores[2],scores[1],cases)
    report['passed']=report['passed'] and report['warmStart']['passed']
    evaluation={'identity':identity,'cases':cases,'policies':policies,'scores':scores,'gate':report}
    save(output/'evaluation.json',evaluation)
    save(output/'opening_review.json',review(evaluation))
    artifact={k:v for k,v in champion.items() if k!='trainingUnits'}
    artifact.update(schema=1,validated=False,leagueGatePassed=report['passed'],identity=identity,
                    note='Fresh-start comparison only; inspect paired evidence before publishing. Shipped policy is unchanged.')
    save(output/'candidate_policy.json',artifact)
    print(report,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--paired-run',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    train(parser.parse_args())
