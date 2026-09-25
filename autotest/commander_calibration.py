"""Fit a bounded production correction from real, cohort-attributed battle income.

Future reinforcements' production is excluded. Later escorts and player actions
still affect these observational returns; they are not isolated counterfactuals.
No third-party runtime or remote model is required.
"""
import math

FEATURES = 10
HORIZON = 60


def samples(episode):
    """Use complete horizons (or actual player victories), never censored timeouts."""
    traces = [t['ice'] for t in episode['trace']]
    final = episode['final']['coldStorage']
    traces.append(final)
    rows = []
    for decision in episode.get('decisions', []):
        raw = decision.get('rawProduction', 0)
        inputs = decision.get('productionInputs', [])
        if raw <= 0 or len(inputs) != FEATURES or not all(math.isfinite(x) for x in inputs):
            continue
        start, end = decision['elapsed'], decision['elapsed'] + HORIZON
        snapshot = next((t for t in traces if t['elapsed'] >= end), None)
        if snapshot is None:
            if episode['outcome'] != 'player_win':
                continue
            snapshot = final  # No further enemy production after the actual defeat.
        if snapshot['elapsed'] - start > 120 or 'productionEvents' not in snapshot:
            continue
        actual = sum(e['amount'] for e in snapshot['productionEvents']
                     if start < e['at'] <= end and e['wave'] <= decision['wave'])
        rows.append({'x': inputs, 'raw': raw, 'actual': actual,
                     'target': min(1, actual / raw), 'elapsed': start, 'wave': decision['wave']})
    return rows


def fit(rows, min_leaf=12, depth=3):
    """Fit currency-unit squared error (not equally weighted ratios); split whole games in the caller."""
    if not rows:
        raise ValueError('No complete production observations')
    nodes = []

    def grow(group, remaining):
        at = len(nodes)
        # 预测 600 冰错一半和预测 6 冰错一半，经济影响不同；按真实冰单位拟合误差。
        mass = lambda r: max(1.0,r.get('raw',1.0))**2
        total_weight = sum(mass(r) for r in group)
        mean = sum(mass(r)*r['target'] for r in group) / total_weight
        nodes.append({'feature': -1, 'left': -1, 'right': -1, 'threshold': 0, 'value': mean})
        baseline = sum(mass(r)*(r['target'] - mean) ** 2 for r in group)
        best = None
        if remaining and len(group) >= 2 * min_leaf:
            for feature in range(FEATURES):
                ordered = sorted(group, key=lambda r: r['x'][feature])
                total = sum(mass(r)*r['target'] for r in ordered)
                squared = sum(mass(r)*r['target'] ** 2 for r in ordered)
                left_sum = left_squared = left_weight = 0
                for cut in range(1, len(ordered)):
                    target = ordered[cut - 1]['target']
                    weight = mass(ordered[cut-1])
                    left_weight += weight
                    left_sum += weight*target
                    left_squared += weight*target ** 2
                    if cut < min_leaf or len(ordered) - cut < min_leaf:
                        continue
                    a, b = ordered[cut - 1]['x'][feature], ordered[cut]['x'][feature]
                    if a == b:
                        continue
                    loss = (left_squared - left_sum ** 2 / left_weight + squared - left_squared
                            - (total - left_sum) ** 2 / (total_weight - left_weight))
                    if loss < baseline - 1e-6 and (best is None or loss < best[0]):
                        best = (loss, feature, (a + b) / 2, ordered[:cut], ordered[cut:])
        if best:
            _, feature, threshold, left, right = best
            nodes[at].update(feature=feature, threshold=threshold,
                             left=grow(left, remaining - 1), right=grow(right, remaining - 1))
        return at

    grow(rows, depth)
    return {'schema': 1, 'featureCount': FEATURES, 'nodes': nodes}


def predict(model, inputs):
    """Same forward-only traversal as the native inference implementation."""
    node = model['nodes'][0]
    while node['feature'] >= 0:
        node = model['nodes'][node['left'] if inputs[node['feature']] <= node['threshold'] else node['right']]
    return node['value']


def metrics(rows, model=None):
    """Report ice-unit error as well as forecast bias; empty groups cannot qualify."""
    if not rows:
        return {'count': 0}
    forecasts = [r['raw'] * (predict(model, r['x']) if model else 1) for r in rows]
    return {'count': len(rows), 'mae': sum(abs(p-r['actual']) for p,r in zip(forecasts,rows))/len(rows),
            'bias': sum(p-r['actual'] for p,r in zip(forecasts,rows))/len(rows)}
