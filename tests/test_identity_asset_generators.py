"""Regenerate legacy assets in isolation and protect the current equipment art.

Run with a Python environment containing Pillow and with pwsh on PATH.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ART = Path('build/clang-release/resources/image/reanim')
LOCKS = ('generate_zombie_identity_assets.sha256.json', 'area11_unit_assets.sha256.json',
         'generate_cold_storage_defense_assets.sha256.json', 'generate_mine_fourth_assets.sha256.json')
LEGACY = ('generate_cold_storage_defense_assets.py', 'generate_area11_unit_assets.py',
          'generate_mine_fourth_assets.py', 'generate_grounding_zombie_assets.ps1',
          'generate_ice_wall_engineer_assets.ps1', 'generate_area8_finale_assets.ps1',
          'generate_insulator_armor.ps1')


def lock_paths(name):
    data = json.loads((ROOT / 'scripts' / name).read_text())
    groups = data.values() if 'inputs' in data else (data,)
    return {Path(key.replace('\\', '/')) for group in groups for key in group}


def verify_canisters():
    """Every partial image must retain the same shell and show less liquid than full."""
    from PIL import Image
    stages = [Image.open(ROOT / ART / f'Disaster_canister_{i}.png').convert('RGBA') for i in range(5)]
    pixels = [list(image.get_flattened_data()) for image in stages]
    assert all(image.size == (88, 116) for image in stages)
    assert len({image.tobytes() for image in stages}) == 5
    liquid = {i for i, (r, g, b, a) in enumerate(pixels[4])
              if a >= 160 and g > r * 1.5 and b > r * 1.4}
    assert liquid, 'Full canister has no visible liquid'
    filled = [{i for i in liquid if image[i] == pixels[4][i]} for image in pixels]
    assert not filled[0] and filled[4] == liquid
    assert all(filled[i] < filled[i + 1] for i in range(4)), 'Liquid must rise at every stage'
    for image in pixels[:-1]:
        assert all(a[3] == b[3] for a, b in zip(image, pixels[4])), 'Fill changed silhouette'
        assert all(a == b for a, b in zip(image, pixels[4])
                   if not (b[1] > b[0] * 1.5 and b[2] > b[0] * 1.4)), 'Fill changed shell'


def verify_legacy_generators():
    """Execute the real exporters without allowing their writes into the checkout."""
    pwsh = shutil.which('pwsh')
    assert pwsh, 'pwsh is required'
    identity_paths = lock_paths(LOCKS[0])
    prefixes = ('ColdChain_shield', 'CrystalDrummer_drum', 'Disaster_canister_',
                'Zombie_aurora_device', 'Zombie_polar_clock_disk', 'Zombie_insulator_armor',
                'Zombie_grounding_cone', 'Zombie_icewall_engineer_hat')
    protected = {p for p in identity_paths if p.parent == ART and p.name.startswith(prefixes)}
    assert len(protected) == 22
    required = set(identity_paths)
    for name in LOCKS[1:]:
        paths = lock_paths(name)
        assert not protected & paths, f'{name} still claims migrated equipment'
        required.update(paths)
    required.update(Path(p) for p in ('build/clang-release/resources/resources.xml',
                                     'build/clang-release/resources/manifest.txt',
                                     'build/clang-release/resources/image/PlantImage/Marigold.png',
                                     'build/clang-release/resources/image/PlantImage/LilyPad.png'))
    scratch = (ROOT / 'build' / 'asset-generator-tests').resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='identity-', dir=scratch) as directory:
        workspace = Path(directory).resolve()
        assert workspace.is_relative_to(scratch)
        for relative in required:
            target = workspace / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        scripts = workspace / 'scripts'
        scripts.mkdir(exist_ok=True)
        for name in (*LEGACY, 'generate_zombie_identity_assets.ps1', LOCKS[0]):
            shutil.copy2(ROOT / 'scripts' / name, scripts / name)
        # Omit legacy provenance locks so every exporter really runs. Its output
        # ownership is checked against today's equipment, not its historical art.
        digest = lambda p: hashlib.sha256((workspace / p).read_bytes()).hexdigest()
        expected = {p: digest(p) for p in protected}
        for name in LEGACY:
            command = [sys.executable, '-B'] if name.endswith('.py') else [pwsh, '-NoProfile', '-File']
            result = subprocess.run([*command, str(scripts / name)], cwd=workspace,
                                    capture_output=True, text=True, encoding='utf-8', errors='replace')
            assert result.returncode == 0, f'{name}: {result.stdout}\n{result.stderr}'
            assert {p: digest(p) for p in protected} == expected, f'{name} rewrote migrated equipment'
            print(f'{name}: equipment preserved', flush=True)


if __name__ == '__main__':
    verify_canisters()
    verify_legacy_generators()
    print('Canister fill and equipment generator ownership: passed')
