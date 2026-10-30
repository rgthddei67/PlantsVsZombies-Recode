# 水岸工程船素材来源

使用内置 ImageGen，原始透明母图保存在本目录。`../generate_engineering_ship_assets.ps1` 只进行 Alpha 裁边、缩放、舱盖拆片和专用 reanim 打包；输入及输出 SHA-256 记录在 `engineering_ship.sha256.json`，更换母图后先检查小尺寸合成，再显式使用 `-UpdateLock`。

正式图片位于 `build/clang-release/resources/image/` 及其 `reanim/` 子目录，时间轴为 `resources/reanim/EngineeringShipZombie.reanim`。动画由船壳、炮塔、驾驶员、两片舱盖组成，所有轨道均为 24 帧；没有新增动画帧事件。换位、锁格预警、开炮、散热和机械死亡读取正式玩法状态，散热跨半血不会让已经开启的舱盖重新闭合。

## 分件母图

`engineering_ship_parts_source.png`。内置 ImageGen 生成；Hull、Cannon、Captain、Hatch 分别裁切，保留透明 Alpha。

> Use case: stylized-concept. Asset type: a single production 2D game sprite PARTS SHEET for one amphibious zombie engineering boat boss, in the hand-painted cartoon visual language of classic Plants vs Zombies. Transparent background, no floor, no text, no labels. This is a separated rig asset, NOT a finished scene. Arrange exactly FOUR isolated non-overlapping parts in a clean 2 by 2 grid with generous transparent gaps; every part faces LEFT and shares orthographic side-view perspective. TOP LEFT: the boat's broad squat teal and rusty yellow riveted hull with a dark rubber continuous tank tread underneath, chunky metal bumpers, porthole, recessed circular orange-red turbine core on the near side; no turret or driver on top. Hull shape is pointed bow to LEFT, blunt stern to RIGHT. TOP RIGHT: a short chunky industrial water-cannon assembly with a thick pipe barrel aimed LEFT, dark nozzle, brass coupling, pressure tank and a mounting base; no hull or driver. BOTTOM LEFT: a small green-grey comic zombie ship captain upper torso in a battered orange construction helmet and navy overalls, large goofy eyes and teeth, hands gripping a little ship wheel, separated at the waist; no legs, no boat. BOTTOM RIGHT: a PAIR OF CLOSED TEAL ARMORED SHUTTERS joined as one circular hatch cover with riveted yellow rim, sized to cover the hull's circular turbine core, fully opaque; this is one separate overlay piece. Crisp dark brown contour, soft painted highlights and shadow, readable chunky shapes, dents, a few rust patches and rivets, charming rather than realistic, clear silhouette at small game size. View slightly from above just enough to see top planes but no isometric perspective. Consistent soft upper-left lighting. Four fully rendered parts, no other doodles, no water, no wakes, no grids, no cast shadow outside the parts. Real transparent alpha.

## 水炮与飞沫

`engineering_ship_water_source.png`。内置 ImageGen 生成，完整炮流及左端飞沫分别打包为运行纹理；预警复用现有玉米炮瞄准标记，与蓝色飞沫和倒计时一起显示危险格。

> Use case: stylized-concept. Asset type: one isolated VFX sprite for a hand-painted cartoon Plants vs Zombies style game. A forceful industrial water-cannon jet aimed horizontally to the LEFT: narrow nozzle-origin at the RIGHT edge and a wider foaming impact burst at the LEFT, entire isolated water plume visible with comfortable transparent margin. White foamy bright core surrounded by layered turquoise and icy cyan sheets, curled splashing edges, large clear droplets and smaller mist flecks, translucent soft edges and streaks showing powerful motion. Strong taper so this can be stretched to connect a cannon on right to target on left. Crisp beautiful painted cartoony effect, no outline around outer mist, no realistic photograph. Wide horizontal composition, aspect around 3:1 for actual plume. NO cannon, NO hull, NO characters, NO letters, NO floor, NO backdrop, no rectangular glow, real transparent alpha.

## 半血裂损船壳

`engineering_ship_damaged_source.png`。以裁好的船壳为唯一编辑目标，通过内置 ImageGen 增加裂缝、凹痕与烟熏，保留轮廓及核心位置。正常与受损图输出尺寸相同，按当前生命派生换图，治疗与读档可恢复。

> Edit this exact cartoon game sprite into its half-health damaged state. Preserve the identical canvas aspect, exact external silhouette, facing direction, perspective, scale, positions of the circular orange turbine core, porthole, cannon mounting top, hull edges, rubber track and all wheels, and transparent alpha background. Only add two clearly readable jagged dark cracks in the teal hull metal, scraped pale metal around one bent rivet, a dent on the yellow top rail, and dark soot stains around the turbine vent on the right. Keep the circular turbine fully visible and at the exact same position so an existing hatch overlay still aligns. Moderate damage, still operational. No fire, no smoke, no particles, no fragments outside silhouette, no new objects, no changes to the overall dimensions or composition. This is an in-place texture swap for a rigged video-game character.
