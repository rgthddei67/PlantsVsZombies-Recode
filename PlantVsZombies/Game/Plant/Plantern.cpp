#include "Plantern.h"

#include "../AudioSystem.h"
#include "Game/Board/Board.h"
#include "Game/Board/BoardPresentation.h"
#include "../ShadowComponent.h"
#include "../../ResourceKeys.h"
#include "../../DeltaTime.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kFuelFullHintSeconds = 1.8f; // 雾火溢出后卡槽提示的持续游戏秒
}

void Plantern::SetupPlant()
{
	Plant::SetupPlant();
	if (auto* shadow = GetShadow()) {
		shadow->SetScale(Vector(0.82f, 0.82f));
		shadow->SetOffset(Vector(0.0f, 29.0f));
	}
}

void Plantern::PlantUpdate()
{
	const float deltaTime = DeltaTime::GetDeltaTime();
	mFuelFullHintTimer = std::max(0.0f, mFuelFullHintTimer - deltaTime);
	// 4-1 只承担迷雾视觉教学；4-2～4-9 与复用完整雾机制的 6-9 启用燃料和索敌压力。
	if (!mBoard || !mBoard->SupportsPlanternMechanics()
		|| mGear == PlanternGear::OFF || mFuel <= 0.0f) {
		return;
	}

	const float previousFuel = mFuel;
	mFuel = std::max(0.0f, mFuel - GetCurrentBurnRate() * deltaTime);
	if (previousFuel >= LOW_FUEL_THRESHOLD && mFuel < LOW_FUEL_THRESHOLD) {
		// 只在阈值下降沿提示：持续低燃料不刷屏，补回阈值后再次跌破仍会重新提醒。
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_CLICKFAILED, 0.5f);
		if (BoardPresentation* presentation = mBoard->GetPresentation()) {
			presentation->ShowPlanternLowFuelWarning();
		}
	}
}

void Plantern::Draw(Graphics* g)
{
	if (g && HasUsableLight()) {
		const Vector center = GetVisualAnchorPosition() + Vector(0.0f, -9.0f);
		const float gear = static_cast<float>(static_cast<int>(mGear));
		const float pulse = 0.94f + 0.06f
			* std::sin(static_cast<float>(mBoard ? mBoard->mBoardFrame : 0) * 0.08f);
		const float radius = (24.0f + gear * 5.0f) * pulse;
		g->FillCircle(center.x, center.y, radius,
			glm::vec4(255.0f, 205.0f, 72.0f, 28.0f), 32);
		g->FillCircle(center.x, center.y, radius * 0.56f,
			glm::vec4(255.0f, 235.0f, 145.0f, 42.0f), 28);
	}
	Plant::Draw(g);
}

void Plantern::Die()
{
	mPendingFuel = 0.0f;
	if (mBoard) mBoard->NotifyPlanternRemoved(mPlantID);
	Plant::Die();
}

void Plantern::SaveExtraData(nlohmann::json& j) const
{
	j["fuel"] = mFuel;
	j["pendingFuel"] = mPendingFuel;
	j["gear"] = static_cast<int>(mGear);
}

void Plantern::LoadExtraData(const nlohmann::json& j)
{
	const float savedFuel = std::clamp(
		j.value("fuel", INITIAL_FUEL), 0.0f, FUEL_CAPACITY);
	const float savedPendingFuel = std::clamp(
		j.value("pendingFuel", 0.0f), 0.0f, FUEL_CAPACITY);
	// MistFuel 不单独持久化；读档时把已预留的在途燃料结算，既不丢奖励也不留下永久占位。
	mPendingFuel = 0.0f;
	SetFuel(savedFuel + savedPendingFuel);
	const int gear = std::clamp(j.value("gear", static_cast<int>(PlanternGear::LOW)),
		static_cast<int>(PlanternGear::OFF), static_cast<int>(PlanternGear::HIGH));
	mGear = static_cast<PlanternGear>(gear);
	mFuelFullHintTimer = 0.0f;
}

float Plantern::AddFuel(float amount)
{
	if (amount <= 0.0f) return 0.0f;
	const float available = std::max(0.0f, FUEL_CAPACITY - mFuel - mPendingFuel);
	const float accepted = std::min(amount, available);
	mFuel += accepted;
	if (accepted + 0.001f < amount) {
		mFuelFullHintTimer = kFuelFullHintSeconds;
	}
	return accepted;
}

float Plantern::ReserveFuel(float amount)
{
	if (amount <= 0.0f) return 0.0f;
	const float storageAvailable = std::max(
		0.0f, FUEL_CAPACITY - mFuel - mPendingFuel);
	// 允许一次爆发兑现本波完整预算，但不能把数波未领取奖励同时灌入灯芯。
	const float intakeLimit = GetWaveIntakeLimit();
	const float intakeAvailable = std::max(0.0f, intakeLimit - mPendingFuel);
	const float accepted = std::min({ amount, storageAvailable, intakeAvailable });
	mPendingFuel += accepted;
	if (accepted + 0.001f < amount) {
		mFuelFullHintTimer = kFuelFullHintSeconds;
	}
	return accepted;
}

float Plantern::GetWaveIntakeLimit() const
{
	return mBoard
		? static_cast<float>(mBoard->GetMistFuelWaveBudget()
			* mBoard->GetPerkManager().GetMistFuelMultiplier())
		: FUEL_CAPACITY;
}

void Plantern::DeliverReservedFuel(float amount)
{
	const float delivered = std::min(std::max(0.0f, amount), mPendingFuel);
	mPendingFuel -= delivered;
	AddFuel(delivered);
}

void Plantern::SetFuel(float fuel)
{
	mFuel = std::clamp(fuel, 0.0f, FUEL_CAPACITY);
}

void Plantern::SetGear(PlanternGear gear)
{
	const int value = std::clamp(static_cast<int>(gear),
		static_cast<int>(PlanternGear::OFF), static_cast<int>(PlanternGear::HIGH));
	mGear = static_cast<PlanternGear>(value);
}

float Plantern::GetCurrentBurnRate() const
{
	return GetBurnRate(mGear);
}

float Plantern::GetBurnRate(PlanternGear gear) const
{
	return PlanternRules::BurnRate(gear,mBoard ? PlanternRules::Scarcity(mBoard->IsWeatherStation(),mBoard->mCurrentWave,mBoard->GetMistFuelScarcityFactor()) : 0.0f);
}
