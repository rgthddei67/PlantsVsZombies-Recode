#include "Crater.h"
#include "Game/Board/Board.h"
#include "Cell.h"
#include "GameObjectManager.h"
#include "../GameApp.h"
#include "../ResourceManager.h"
#include "../ResourceKeys.h"
#include "../DeltaTime.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr float kPoolBobAmplitude = 2.0f;	// 水格弹坑随水面上下浮动的振幅，单位：像素
	constexpr float kPoolBobRadiansPerFrame = 3.14159265f / 60.0f;	// 60Hz 下两秒完成一个浮动周期
	constexpr float kPoolBobRowPhase = 3.14159265f;	// 相邻水路交错半个浮动周期
	constexpr float kPoolBobColumnPhase = 3.14159265f / 4.0f;	// 相邻列错开八分之一浮动周期
}

Crater::Crater(Board* board, int row, int column, float timeLeft)
	: GameObject(ObjectType::OBJECT_NONE)
	, mRow(row), mColumn(column), mTimeLeft(timeLeft), mBoard(board)
{
	SetTag("Crater");
	SetName("Crater_" + std::to_string(row) + "_" + std::to_string(column));

	// 绘制锚点 = 格子左上 + (-8, +40)，镜像原版 GridItem::DrawCrater 的偏移
	const Vector center = mBoard
		? mBoard->GetCellCenterPosition(row, column)
		: Vector(CELL_INITALIZE_POS_X + column * CELL_COLLIDER_SIZE_X + 40.0f,
			CELL_INITALIZE_POS_Y + row * CELL_COLLIDER_SIZE_Y + 50.0f);
	const float cellHeight = mBoard ? mBoard->GetCellHeight() : CELL_COLLIDER_SIZE_Y;
	CreateTransform(
		Vector(center.x - 48.0f, center.y - cellHeight * 0.5f + 40.0f));
}

void Crater::Update()
{
	GameObject::Update();

	// 选卡期间 GameObjectManager 仍会更新场上对象；弹坑寿命只消耗实际战斗时间。
	if (!mBoard || mBoard->GetState() != BoardState::GAME) {
		return;
	}

	// 乘 dt：暂停冻结、倍速等比缩放（与原版 GridItemCounter 随游戏更新递减一致）
	mTimeLeft -= DeltaTime::GetDeltaTime();
	if (mTimeLeft <= 0.0f) {
		GameObjectManager::GetInstance().DestroyGameObject(this);
	}
}

void Crater::Draw(Graphics* g)
{
	auto tex = ResourceManager::GetInstance().GetTexture(GetTextureKey());
	if (!tex) return;

	float alpha = 255.0f;
	if (mTimeLeft < FADE_OUT_TIME) {
		alpha = 255.0f * std::max(0.0f, mTimeLeft / FADE_OUT_TIME);
	}

	Vector pos = GetTransform() ? GetTransform()->GetPosition() : Vector::zero();
	if (mBoard && mBoard->IsPoolSquare(mRow, mColumn)) {
		// 水格弹坑与水面植物共用同一相位口径，避免贴图静止在浮动的睡莲之下。
		const float phase = static_cast<float>(mBoard->mBoardFrame) * kPoolBobRadiansPerFrame
			+ static_cast<float>(mRow) * kPoolBobRowPhase
			+ static_cast<float>(mColumn) * kPoolBobColumnPhase;
		pos.y += std::sin(phase) * kPoolBobAmplitude;
	}
	else if (mBoard && mBoard->IsRoofBackground()) {
		// 原版两套屋顶贴图拥有不同留白，必须与其斜坡/平台锚点偏移配套使用。
		pos += mColumn < 5 ? Vector(16.0f, -16.0f) : Vector(18.0f, -9.0f);
	}
	g->DrawTexture(tex, pos.x, pos.y,
		static_cast<float>(tex->width), static_cast<float>(tex->height),
		0.0f, glm::vec4(255.0f, 255.0f, 255.0f, alpha));
}

const std::string& Crater::GetTextureKey() const
{
	using namespace ResourceKeys::Textures;

	const bool night = mBoard
		&& GameAPP::GetInstance().GetBackgroundIsNight(mBoard->mBackGround);
	const bool fading = mTimeLeft < CRATER_DURATION * 0.5f;

	// 地形优先于整张背景：泳池关的陆地行仍使用普通草地弹坑。
	if (mBoard && mBoard->IsPoolSquare(mRow, mColumn)) {
		if (night) {
			return fading ? IMAGE_CRATER_WATER_NIGHT_PART_1
				: IMAGE_CRATER_WATER_NIGHT_PART_0;
		}
		// 白天毁灭菇当前会睡眠，这条资源路径仍须保留给后续咖啡豆唤醒后的正式爆炸。
		// TODO(coffee-bean): 咖啡豆落地后补“白天泳池唤醒毁灭菇→生成水格弹坑”的端到端用例。
		return fading ? IMAGE_CRATER_WATER_DAY_PART_1
			: IMAGE_CRATER_WATER_DAY_PART_0;
	}

	if (mBoard && mBoard->IsRoofBackground()) {
		if (mColumn < 5) {
			return fading ? IMAGE_CRATER_ROOF_LEFT_PART_1
				: IMAGE_CRATER_ROOF_LEFT_PART_0;
		}
		return fading ? IMAGE_CRATER_ROOF_CENTER_PART_1
			: IMAGE_CRATER_ROOF_CENTER_PART_0;
	}

	return fading
		? (night ? IMAGE_CRATER_FADING_PART_1 : IMAGE_CRATER_FADING_PART_0)
		: (night ? IMAGE_CRATER_PART_1 : IMAGE_CRATER_PART_0);
}
