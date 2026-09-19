"""Check that delayed support does not restart an expired decision interval, including after load."""
import json
from pathlib import Path
import sys


def verify(root):
    """Use the queued deadline and stable deployment totals instead of moving X/Y coordinates."""
    def read(name):
        return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))

    assert read('status')['status'] == 'passed'
    queued = read('queued')['coldStorage']
    deadline = max(z['remaining'] for z in queued['pending'])
    for name in ('uninterrupted', 'restored'):
        after = read(name)['coldStorage']
        elapsed = after['elapsed'] - queued['elapsed']
        assert deadline - 0.1 <= elapsed <= deadline + 0.2, (name, deadline, elapsed)
        assert after['decisions'] == queued['decisions'] + 1
        assert after['deployments'] == queued['deployments'] + len(queued['pending'])
    print('Dispatch verified: no extra interval after paid queue; identical deadline after snapshot load; no duplicated deployments.')


if __name__ == '__main__':
    verify(Path(sys.argv[1] if len(sys.argv) > 1 else
                'build/clang-release/autotest/out/smoke_cold_storage_dispatch_ai'))
