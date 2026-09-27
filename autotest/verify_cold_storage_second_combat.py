"""Compare real attacks from the same saved board, not just displayed buff values."""
from pathlib import Path
import json
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('build/clang-release/autotest/out/smoke_cold_storage_second_combat')


def read(name):
    return json.loads((root / f'{name}.json').read_text(encoding='utf-8'))


normal = 3000 - read('normal_fire')['zombiesByType']['ZOMBIE_GARGANTUAR']['bodyHealth']
boosted = 3000 - read('boosted_fire')['zombiesByType']['ZOMBIE_GARGANTUAR']['bodyHealth']
assert normal > 0 and 1.7 <= boosted / normal <= 2.4, (normal, boosted)
start = read('bite_start')['normalPlantsByCell']['2_4']['health']
end = read('bite_end')['normalPlantsByCell']['2_4']['health']
assert start - end >= 800, (start, end)
assert json.loads((root / 'status.json').read_text())['exitCode'] == 0
print(f'PASS: six-second damage {normal} -> {boosted}; boiler two-second bite damage {start-end}')
