#include "Board.h"
#include "Game/Zombie/DisasterEngineerZombie.h"
#include "Game/Zombie/DisasterEngineerRules.h"
#include "Game/Plant/ThunderFlowerRules.h"
#include "Game/AutoTest/TestDriver.h"
#include "GameApp.h"
#include "Graphics.h"
#include <algorithm>

namespace {
constexpr float kArcLifetime = .18f; // 雷种命中电弧的显示游戏秒数

/** 命中后短暂保留电弧端点；仅负责演出，不继续伤害或追踪死亡目标。 */
class ThunderArc final : public GameObject {
public:
	ThunderArc(Vector origin, std::vector<Vector> ends) : mOrigin(origin), mEnds(std::move(ends)) {}
	void Update() override {
		mRemaining -= DeltaTime::GetDeltaTime();
		if (mRemaining <= 0) { SetActive(false); GameObjectManager::GetInstance().DestroyGameObject(this); }
	}
	void Draw(Graphics* g) override {
		const float alpha = 255 * std::clamp(mRemaining/kArcLifetime, 0.0f, 1.0f);
		for (const auto& end : mEnds) {
			const Vector mid((mOrigin.x+end.x)*.5f+9, (mOrigin.y+end.y)*.5f-12);
			for (int offset=-2; offset<=2; ++offset) {
				const glm::vec4 color = offset==0 ? glm::vec4(232,250,255,alpha) : glm::vec4(86,133,255,alpha*.55f);
				g->DrawLine(mOrigin.x, mOrigin.y+offset, mid.x, mid.y+offset, color);
				g->DrawLine(mid.x, mid.y+offset, end.x, end.y+offset, color);
			}
		}
	}
private:
	Vector mOrigin;
	std::vector<Vector> mEnds;
	float mRemaining = kArcLifetime;
};
}

void Board::ApplyPlantAshAttack(const std::vector<int>& targets, const std::function<void(Zombie*)>& apply)
{
	std::vector<int> protectedIDs;
	auto ids = mEntityRegistry.GetAllZombieIDs();
	std::sort(ids.begin(), ids.end());
	// 在任何伤害前冻结邻近名单、消费罐子，消除行桶顺序及同次来源死亡的影响。
	for (const int id : ids) {
		auto* engineer = dynamic_cast<DisasterEngineerZombie*>(mEntityRegistry.GetZombie(id));
		if (!engineer || !engineer->IsActive() || engineer->IsDying() || !engineer->HasHead()
			|| !engineer->HasFullCanister()) continue;
		const size_t firstProtected=protectedIDs.size();
		bool used = false;
		for (const int worker : engineer->GetProtectedWorkerIDs()) {
			if (std::find(targets.begin(),targets.end(),worker)==targets.end()
				|| std::find(protectedIDs.begin(),protectedIDs.end(),worker)!=protectedIDs.end()) continue;
			protectedIDs.push_back(worker); used = true;
			if (auto* z=mEntityRegistry.GetZombie(worker)) z->SetGlowingTimer(.3f);
		}
		if (used) {
			engineer->ConsumeCanister();
			// 免伤已经冻结，来源可能随后被这次灰烬杀死；测试在这里留事件，不能只靠下一秒活体计数。
			if(GameAPP::mAutoTestMode) TestDriver::GetInstance().RecordEngineerAshProtection(mColdStorage.elapsed,
				engineer->mRow,id,std::vector<int>(protectedIDs.begin()+firstProtected,protectedIDs.end()));
		}
	}
	for (const int id : targets) {
		if (std::find(protectedIDs.begin(),protectedIDs.end(),id)!=protectedIDs.end()) continue;
		if (auto* z=mEntityRegistry.GetZombie(id); z && z->IsActive() && !z->IsDying()) apply(z);
	}
}

void Board::CreateThunderImpact(const Vector& position, int row, int damage)
{
	std::vector<std::pair<float,int>> targets;
	const float radius = ThunderFlowerRules::RadiusCells*CELL_COLLIDER_SIZE_X;
	for (int r=std::max(0,row-1); r<=std::min(mRows-1,row+1); ++r)
		mEntityRegistry.ForEachZombieInRow(r, [&](Zombie* z) {
			if (!z || !z->IsActive() || z->IsDying() || z->IsMindControlled()
				|| !z->CanBeTargetedByProjectile(false) || MineBlocksSegment(position,z->GetPosition())) return;
			const auto* collider=z->GetColliderComponent();
			if (!collider) return;
			const auto bounds=collider->GetBoundingBox();
			if (bounds.x>position.x+radius || bounds.x+bounds.w<position.x-radius) return;
			const float dx=bounds.x+bounds.w*.5f-position.x;
			const float dy=(r-row)*GetCellHeight();
			targets.emplace_back(dx*dx+dy*dy,z->mZombieID);
		});
	std::sort(targets.begin(),targets.end());
	int controlled=0;
	std::vector<Vector> ends;
	for (const auto& entry:targets) if (auto* z=mEntityRegistry.GetZombie(entry.second)) {
		ends.push_back(z->GetPosition()+Vector(0,-30));
		z->TakeProjectileDamage(damage,DamageSource::PLANT,0,false,false,false,
			PlantDamageOrigin::FromPlant(PlantType::PLANT_THUNDERFLOWER));
		if (controlled<ThunderFlowerRules::ControlLimit && z->ApplyThunderParalysis()) ++controlled;
	}
	if (ends.empty()) ends.push_back(position+Vector(22,-24));
	GameObjectManager::GetInstance().CreateGameObject<ThunderArc>(LAYER_EFFECTS_WORLD,position,std::move(ends));
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP,.15f);
}
