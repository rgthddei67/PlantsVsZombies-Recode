"""Check detached-part rendering and no repeated limb burst on save restoration."""
import json
import sys
from pathlib import Path
root = Path(sys.argv[1])
def read(name):
    return json.loads((root / (name + '.json')).read_text(encoding='utf-8'))
assert read('status')['status'] == 'passed'
for part, effect in [('arm', 'FogFerryArmOff'), ('head', 'FogFerryHeadOff')]:
    detached = read(part + '_detach')
    z = detached['zombiesByType']['ZOMBIE_FOG_FERRY']
    assert z['ferryLimbResourcesReady'] and z['ferryStumpVisible']
    assert not z['hasArm'] and not z['armVisible']
    particles = detached['particleEffectsByName'][effect]
    assert len(particles) == 1 and particles[0]['activeParticleCount'] == 1
    assert particles[0]['renderProbeReady'] and particles[0]['renderQuadCount'] == 1
    bounds = particles[0]['worldBounds']
    assert bounds['widthInt'] >= (35 if part == 'head' else 12)
    assert bounds['heightInt'] >= (35 if part == 'head' else 28)
    restored = read(part + '_restored')
    assert restored['zombiesByType']['ZOMBIE_FOG_FERRY']['ferryStumpVisible']
    assert restored['particleEffectNameCounts'].get(effect, 0) == 0
    if part == 'head':
        assert not z['hasHead'] and not z['headVisible']
        assert not restored['zombiesByType']['ZOMBIE_FOG_FERRY']['headVisible']
print('Fog ferry limb resources, rendered particles, stump and reload state passed')
