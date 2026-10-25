# 水岸第四组素材来源

使用内置 ImageGen，透明母图保存在本目录。`../generate_waterside_fourth_assets.py` 完成图集裁切、缩放和首帧卡图合成，输入/输出 SHA-256 锁定在同名 `.sha256.json`；源图改变时需审核后显式 `--update-lock`。

浮光葵保留 Sunflower 的茎、花瓣和表情分件，新增浮叶跟随底茎；荷伞保留 NormalZombie 的身体、走路、游泳、啃食、断肢和死亡，装备跟随 `anim_innerarm2`。两者没有新增动画帧事件。荷伞母图按左、中、右提取完整、轻损、重损状态。

## 荷伞装备

母图：`lotus_parasol_source.png`。

> Create a production sprite sheet for a classic Plants vs Zombies style 2D hand-painted cartoon game: ONLY the equipment for a Lotus Parasol Zombie. A giant broad green lotus-leaf parasol, seen from side at a slight elevated three-quarter angle, dark green thick contour, bright lime leaf veins, soft painterly highlights; a slender woody bamboo handle extends from the underside down toward a grip at bottom right. No character, hands, face, background, text, ground shadow. Transparent background. Three clearly separated equally sized panels in a single horizontal strip: LEFT fully intact leaf parasol; CENTER the EXACT SAME parasol with modest torn edge and two small holes; RIGHT the EXACT SAME parasol with large tears and several visible holes, still recognizable. All three have identical handle position, perspective, dimensions, and center. Each complete canopy-plus-handle silhouette is taller than wide, leaf canopy about 1.5 times as wide as high and occupies the top half. Wooden handle ends at same grip in all three. Each panel has generous empty transparent margins, no frame or labels. Game-readable thick outlines at 140 pixels high, rounded natural leaf lobes, no metal, no rain, no glow.

## 浮光葵叶座

母图：`float_sunflower_leaves_source.png`。

> Create one isolated game sprite of a floating leaf rosette base for a new amphibious sunflower named Floating Sunflower, in the original classic Plants vs Zombies 2D painted cartoon art style. Equipment/base ONLY, no flower head, no stem, no face, no pot, no water, no sparkles, no scene, no text, no cast shadow. Three overlapping thick rounded teal-green aquatic leaves arranged as a low wide oval floating raft, with a modest open gap at the center rear for inserting an existing sunflower stem. Each leaf has fine warm yellow-green branching veins, a slightly scalloped curved edge with dark brown-green outline, creamy highlights, and chunky readable shaping. Viewpoint matches the original PvZ garden side-on oblique top-down camera. Low flat proportions: width about 2.6 times height. Organic warm watercolor/cel shading, clear thick contours, no photorealism. Centered complete silhouette, generous transparent margin. True transparent background. This will be used at 90 pixels wide below the original articulated sunflower stalk, so keep the leaf silhouette simple and polished.
