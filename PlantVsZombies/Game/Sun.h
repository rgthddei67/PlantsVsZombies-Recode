#pragma once
#ifndef _SUN_H
#define _SUN_H
#include "../ResourceKeys.h"
#include "../DeltaTime.h"
#include "../GameRandom.h"
#include "../GameApp.h"
#include "Coin.h"
#include "Game/Board/Board.h"

class Sun : public Coin {
protected:
	int SunPoint = 25;	// 收集后增加的阳光点数
	Vector mParabolaTarget; // 抛物线运动的目标位置
	Vector mParabolaStart;  // 抛物线起始位置
	float mParabolaHeight = 0.0f;  // 抛物线高度
	float mParabolaTime = 0.0f; // 抛物线运动时间
	bool mIsParabola = false; // 是否正在进行抛物线运动
	bool mShouldStartLinearFall = false; // 是否需要开始匀速下落
	bool mShouldStartParabolaFall = false; // 是否需要开始抛物线运动
	bool mIsLinearFalling = false; // 是否正在进行匀速下落
	Vector mLinearTarget; // 匀速下落的目标位置
	bool mIsInitialized = false; // 是否已初始化

public:
	Sun(Board* board, const Vector& position, float scale = 0.9f,
		const std::string& tag = "Sun", bool needScaleAnimation = false, bool autoDestroy = true)
		: Coin(board, AnimationType::ANIM_SUN, position,
			Vector(55, 55), Vector(0, 0), 10.0f, scale, tag, needScaleAnimation, autoDestroy)
	{
		speedFast = 700.0f;
		speedSlow = 500.0f;
		slowDownDistance = 130.0f;
		this->mCoinType = CoinType::COIN_SUN;
		mStartScale = 0.2f;  // 出现动画从0.2开始，而非接近0
	}

	// TODO 析构函数会自动调用基类的析构函数 不能自己写

	/** 创建点击附件后恢复在途领取，避免 Start 把读档阳光重新变成可点击状态。 */
	void Start() override {
		Coin::Start();
		mIsInitialized = true;
		if (mIsCollected) {
			StartMoveToTarget(Vector(160, 10), speedFast, speedSlow, slowDownDistance);
			return;
		}

		if (mShouldStartLinearFall) {
			mShouldStartLinearFall = false;
			StartLinearFall();
		}
		if (mShouldStartParabolaFall) {
			mShouldStartParabolaFall = false;
			StartParabolaFall();
		}
	}

	/** 结束本次领取并且只结算一次；待移除对象不再有存档资格。 */
	void OnReachTargetBack() override
	{
		if (mIsDestroyed) return;
		Coin::OnReachTargetBack();
		mBoard->AddSun(SunPoint);
	}

	/** 仅用于 Start 前恢复领取承诺；入账仍由实际抵达计数器触发。 */
	void RestoreCollectedState(bool collected) { mIsCollected = collected; }

	void SetOnClickBack(ClickableComponent* clickComponent) override
	{
		if (clickComponent == nullptr) return;
		clickComponent->onClick = [this]() {
			this->mIsCollected = true;
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_COLLECTSUN, 0.5f);
			StartMoveToTarget(Vector(160, 10), speedFast, speedSlow, slowDownDistance);
			};
	}

	// 匀速下落
	void StartLinearFall() {
		// 检查组件是否已初始化
		if (!mIsInitialized) {
			mShouldStartLinearFall = true;
			return;
		}

		Vector currentPos = GetPosition();
		// 目标位置：x不变，y在80~420之间随机
		float targetY = GameRandom::Range(80.0f, 420.0f);
		mLinearTarget = Vector(currentPos.x, targetY);
		mIsLinearFalling = true;
	}

	// 抛物线运动
	void StartParabolaFall() {
		// 检查组件是否已初始化
		if (!mIsInitialized) {
			mShouldStartParabolaFall = true;
			return;
		}

		mParabolaStart = GetPosition();

		// 水平方向随机偏移 -50~50
		float offsetX = GameRandom::Range(-50.0f, 50.0f);

		// 下落距离 20~50
		float fallDistance = GameRandom::Range(20.0f, 50.0f);

		mParabolaTarget = Vector(mParabolaStart.x + offsetX, mParabolaStart.y + fallDistance);

		// 抛物线高度（基于下落距离计算）
		mParabolaHeight = fallDistance * 0.8f;

		mIsParabola = true;
		mParabolaTime = 0.0f;
	}

	void UpdateLinearFall() {
		if (!mIsLinearFalling) return;

		Vector currentPos = GetPosition();
		Vector direction = mLinearTarget - currentPos;
		float distance = direction.magnitude();

		// 如果到达目标位置，停止下落
		if (distance < 1.0f) {
			mIsLinearFalling = false;
			// 到达位置后保持可点击状态
			return;
		}

		// 匀速下落速度 - 适当增加速度
		float speed = 50.0f; // 增加下落速度

		if (distance > 0) {
			Vector normalizedDir = direction / distance;
			Vector newPos = currentPos + normalizedDir * speed * DeltaTime::GetDeltaTime();
			SetPosition(newPos);
		}
	}

	// 更新抛物线运动
	void UpdateParabola() {
		if (!mIsParabola) return;

		mParabolaTime += DeltaTime::GetDeltaTime();

		// 抛物线运动时间（根据距离调整）
		float totalTime = 2.0f; // 增加运动时间

		if (mParabolaTime >= totalTime) {
			// 运动结束
			mIsParabola = false;
			SetPosition(mParabolaTarget);
			return;
		}

		// 计算进度 (0~1)
		float t = mParabolaTime / totalTime;

		// 水平方向匀速运动
		float currentX = mParabolaStart.x + (mParabolaTarget.x - mParabolaStart.x) * t;

		// 垂直方向抛物线运动
		float currentY = mParabolaStart.y + (mParabolaTarget.y - mParabolaStart.y) * t
			- mParabolaHeight * 4.0f * t * (1.0f - t);

		SetPosition(Vector(currentX, currentY));
	}

	/** 推进领取或下落；结束生命周期后不得继续自动领取或移动。 */
	void Update() override {
		Coin::Update();
		if (mIsDestroyed) return;
		if (!mIsLinearFalling && !mIsParabola && !isMovingToTarget && !mIsScaling && !mIsCollected
			&& GameAPP::GetInstance().mAutoCollected) {
			this->mIsCollected = true;
			AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_COLLECTSUN, 0.5f, 0);
			StartMoveToTarget(Vector(160, 10), speedFast, speedSlow, slowDownDistance);
		}

		if (isMovingToTarget) return;
		if (mIsParabola) {
			UpdateParabola();
		}
		else if (mIsLinearFalling) {
			UpdateLinearFall();
		}
	}
};

// 小阳光：行为与 Sun 完全相同，仅视觉缩放 0.6、价值 15。
// 刻意定义在 Sun.h 内——GameInfoSaver 需要 dynamic_cast<SmallSun*>，
// 凡能看到 Sun 的翻译单元都已 #include 本头，零额外 include、零 incomplete-type 风险。
class SmallSun : public Sun {
public:
	SmallSun(Board* board, const Vector& position, float scale = 0.6f,
		const std::string& tag = "SmallSun", bool needScaleAnimation = false, bool autoDestroy = true)
		: Sun(board, position, scale, tag, needScaleAnimation, autoDestroy)
	{
		SunPoint = 15;	// 收集后增加 15 阳光（普通 Sun 为 25）
	}
};
#endif
