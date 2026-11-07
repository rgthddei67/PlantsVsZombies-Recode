#pragma once

#include "Shooter.h"

class ThreePeater : public Shooter {
public:
	using Shooter::Shooter;

	const Animator* GetHeadAnimator2() const { return mHeadAnim2.get(); }
	const Animator* GetHeadAnimator3() const { return mHeadAnim3.get(); }

	/** 保存攻击计时器及三个独立头部 Animator 的完整播放状态。 */
	void SaveExtraData(nlohmann::json& j) const override;
	/** 恢复三个头部的播放状态，保证射击中读档仍只完成当前一轮。 */
	void LoadExtraData(const nlohmann::json& j) override;

	void PlantUpdate() override;
	/** 工坊升级同步调整三个头的当前前摇，保留已预留弹药与事件消费状态。 */
	bool TryUpgradeWorkshop() override;

protected:
	/** 创建三个附着头部，并在上头帧 73 注册整轮同步发射事件。 */
	void SetupPlant() override;
	/** 在同一逻辑帧按下路、本行、上路顺序创建整轮三颗豌豆。 */
	void ShootBullet() override;

private:
	bool mWorkshopVolleyReserved = false; // 三弹整轮原子预留，前摇读档不得再扣弹
	std::shared_ptr<Animator> mHeadAnim2;
	std::shared_ptr<Animator> mHeadAnim3;

	std::shared_ptr<Animator> CreateHeadAnimator(
		const char* idleTrack, const char* attachTrack,
		float basePoseX, float basePoseY);
	/** 判断指定有效行前方是否存在可攻击僵尸。 */
	bool HasTargetInRow(int row) const;
	/** 按 C# 规则检查本行及相邻两行，只需任一行命中即可启动整轮。 */
	bool HasTargetInThreeRows() const;
	bool IsValidRow(int row) const;
	/** 同时启动三个头的一次性射击动画，边界头也参与补偿射击。 */
	void StartVolley(float attackSpeedMultiplier);
	/** 从统一枪口创建豌豆；越界目标补发到本行，相邻行启用斜向轨迹。 */
	void FireRow(int targetRow);
};
