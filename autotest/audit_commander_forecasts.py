"""Compare logged forecasts with real returns; observational windows are not counterfactual labels."""
import argparse
import json
from pathlib import Path

from commander_calibration import samples
from train_cold_storage import save


def windows(episode):
    """Use complete horizons or terminal games; mark later purchases and bounded trace overshoot."""
    traces = [t['ice'] for t in episode['trace']] + [episode['final']['coldStorage']]
    decisions = episode.get('decisions', [])
    production = {(r['elapsed'], r['wave']): r for r in samples(episode)}
    rows = []
    for d in decisions:
        start = d['elapsed']
        horizon = 90 if d.get('adaptive') else 60
        end = start + horizon
        observed = next((t for t in traces if t['elapsed'] >= end), None)
        if observed is None:
            if episode['outcome'] == 'timeout':
                continue
            observed = traces[-1]
        if observed['elapsed'] < start or observed['elapsed'] > end + 11:
            continue
        production_row = production.get((start, d['wave']))
        later = any(start < other['elapsed'] <= end and other['features'][5] > 0 for other in decisions)
        f = d['features']
        # 仅标出价值函数反例，不据此替 AI 下令：自身预测无伤害/推进/残存资产，产冰不抵花费，支出却加分。
        rewarded_loss = (d.get('effectiveWeights',[0]*8)[5] > 0 and f[5] > f[4]
                         and all(f[i] <= 0 for i in (0,1,2,3,7)))
        rows.append({'predictedKills': d['features'][0],
                     'actualKills': max(0, observed['killIncome'] - d['killIncome']),
                     'rawProduction': production_row['raw'] if production_row else None,
                     'predictedProduction': d['features'][4] if production_row else None,
                     'actualProduction': production_row['actual'] if production_row else None,
                     'laterPurchases': later, 'spent': d['features'][5],
                     'rewardedPredictedLoss': rewarded_loss,
                     'stockpile': d.get('enemyIce', 0), 'elapsed': start,
                     'traceOvershoot': max(0, observed['elapsed']-end)})
    return rows


def errors(rows, predicted, actual):
    """Report signed and absolute error without counting missing/censored labels as zero."""
    pairs = [(r[predicted],r[actual]) for r in rows if r.get(predicted) is not None]
    if not pairs:
        return {'count': 0}
    return {'count': len(pairs), 'mae': sum(abs(p-a) for p,a in pairs)/len(pairs),
            'bias': sum(p-a for p,a in pairs)/len(pairs),
            'predictedMean': sum(p for p,a in pairs)/len(pairs),
            'actualMean': sum(a for p,a in pairs)/len(pairs)}


def audit(evaluation):
    """Summarize each frozen policy separately; results never enter candidate selection."""
    reports = []
    for scores in evaluation['scores']:
        rows = []
        for score in scores:
            rows.extend(windows(json.loads(Path(score['result']).read_text(encoding='utf-8'))))
        groups = {'all': rows, 'withoutLaterPurchases': [r for r in rows if not r['laterPurchases']],
                  'highStockpile': [r for r in rows if r['stockpile'] >= 800]}
        reports.append({name: {'windows':len(group), 'kills':errors(group,'predictedKills','actualKills'),
                              'rawProduction':errors(group,'rawProduction','actualProduction'),
                              'production':errors(group,'predictedProduction','actualProduction'),
                              'rewardedPredictedLosses':sum(r['rewardedPredictedLoss'] for r in group),
                              'predictedKillsButNone':sum(r['predictedKills']>0 and r['actualKills']==0 for r in group)}
                        for name,group in groups.items()})
    return {'policies':reports, 'note':'Kill income is measured at the first trace at/after the horizon (<=11s overshoot). '
            'Later purchases and player actions can affect returns. Production excludes later cohorts. '
            'No causal action ranking or exact breach probability is inferred; 800 is an analysis bin, not an AI threshold.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evaluation',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    save(args.output,audit(json.loads(args.evaluation.read_text(encoding='utf-8'))))
