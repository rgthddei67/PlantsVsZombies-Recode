# Zombie identity equipment sources

Generated with the built-in ImageGen tool, transparent background. These are local equipment sprites; all body joints and animation timelines remain classic. Runtime crop, resize, uniform recoloring and detached-part composition are reproducible with `scripts/generate_zombie_identity_assets.ps1`. Reviewed inputs and outputs are recorded in its `.sha256.json` file.

Shared prompt:

> Use case: stylized-concept. Asset type: one isolated 2D equipment sprite for a classic hand-drawn Plants vs Zombies style zombie facing left in a 3/4 side view. Thick irregular dark brown outline, simple 3-tone painterly cel shading, chunky cartoon shapes, no realism, no gradients outside the object. Transparent background, single object centered with ample clear padding. No character, no head, no face, no text, no panels, no ground shadow. It will be reduced to about 70 pixels wide; clear silhouette.

- `zombie_identity_worker_source.png`: A navy blue winter worker knitted beanie, thick ivory folded cuff, small pale blue pompom, worn slanting on a round zombie scalp. Wide squat dome, open bottom rim shown in three quarter side view. The left end is the front of the hat. No dangling straps.
- `zombie_identity_boiler_source.png`: A pair of bulky industrial brass safety goggles on a dark leather forehead band, two round smoky amber lenses with pale glints. Side view facing left, left lens slightly smaller, compact horizontal silhouette, no loose trailing strap. To be worn above eyes on forehead; no helmet.
- `zombie_identity_healer_source.png`: An ivory white first-aid worker cap with dark green folded band and a bold solid green plus symbol, soft squat squared crown, slightly tilted. Side view facing left, broad front visible. No red cross, no lettering.

Second batch uses transparent two-cell equipment atlases, with headwear on the left and equipment on the right. Shared prompt: original 2009 PvZ painted sprites, chunky charcoal outlines, muted two-tone shading, simple at 50 pixels; separate complete objects, no characters, text, background or cast shadows.

- `coldguard`: navy fleece earflap cap and metal-rimmed cyan ice shield.
- `disaster`: ochre safety helmet with goggles and cooling canister with gauge, hose and cyan level window.
- `iceengineer`: ivory/orange hardhat with cyan lamp and leather tool harness.
- `insulator`: tan padded electrical cap and strapped three-plate ceramic breastplate.
- `grounding`: purple cone with copper coil and ball-tipped antenna, plus cable spool.
- `clockmaker`: midnight cap with gear/loupe and brass astronomical clock disk.
- `priest`: turquoise crystal crown on violet circlet and brass-caged aurora prism apparatus.
- `drummer`: brown miner helmet with violet crystals and a crystal-rimmed marching drum.

The four `*_damage_source.png` atlases derive three horizontally arranged states from the matching equipment: intact, lightly cracked and heavily cracked/chipped, with the same perspective and installation baseline. The deterministic exporter retains the old armor canvases, fits the visible components to the reviewed local rectangles, and rejects empty atlas cells. Detached limbs share costume art; equipment fragments use crops from their actual equipment. The current inputs and outputs are locked in `generate_zombie_identity_assets.sha256.json`.
