#include "IceStorageNut.h"
#include "Game/Board/Board.h"
#include "ResourceManager.h"
#include "Graphics.h"
#include "DeltaTime.h"
#include <cmath>

using namespace IceStorageNutRules;

void IceStorageNut::SetupPlant()
{
	WallNut::SetupPlant();
	mPlantHealth = mPlantMaxHealth = kHealth;
	InvalidateDamageTexture();
	UpdateTexture(false);
}

const std::string& IceStorageNut::GetBodyTextureKey() const
{
	static const std::string key = "IMAGE_ICESTORAGENUT_BODY";
	return key;
}

const std::string& IceStorageNut::GetCrackedTextureKey(int stage) const
{
	static const std::string first = "IMAGE_ICESTORAGENUT_CRACKED1", second = "IMAGE_ICESTORAGENUT_CRACKED2";
	return stage >= 2 ? second : first;
}

void IceStorageNut::Update()
{
	if (!mIsPreview && mBoard && mBoard->mBoardState == BoardState::GAME && !DeltaTime::IsPaused()) {
		const float delta = DeltaTime::GetDeltaTime();
		mCooldownRemaining = std::max(0.0f, mCooldownRemaining - delta);
	}
	WallNut::Update();
	// 旧档的 Animator 可能仍带护体染色；取消无敌后统一恢复本体原色。
	mAnimator->SetTrackColor("anim_face", SDL_Color{255, 255, 255, 255});
}

void IceStorageNut::PlantUpdate()
{
	WallNut::PlantUpdate();
	// 自动挡只购买完整的一千生命；手动挡仍可为紧急保命主动补少量血。
	if (mAutomatic && mPlantMaxHealth - mPlantHealth >= kRepairHealth) TryActivate();
}

bool IceStorageNut::IsReadyToActivate() const
{
	return mBoard && mBoard->mBoardState == BoardState::GAME && !mBoard->mTrophySpawned
		&& !DeltaTime::IsPaused() && !mIsPreview && IsActive() && !IsSquished()
		&& !IsBungeeTargeted() && !IsActionPaused() && mPlantHealth > 0
		&& mPlantHealth < mPlantMaxHealth && mCooldownRemaining <= 0.0f;
}

bool IceStorageNut::CanAffordActivation() const
{
	return mBoard && (mBoard->IsColdStorage()
		? mBoard->mColdStorage.playerIce >= kRepairIce : mBoard->GetSun() >= kRepairSun);
}

bool IceStorageNut::TryActivate()
{
	if (!IsReadyToActivate() || !mBoard->TrySpendPlantAbilityResource(kRepairIce, kRepairSun)) return false;
	mPlantHealth = std::min(mPlantMaxHealth, mPlantHealth + kRepairHealth);
	mCooldownRemaining = kRepairCooldown;
	UpdateTexture(false);
	SetGlowingTimer(0.25f);
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BLEEP, 0.35f);
	return true;
}

std::string IceStorageNut::GetManualAbilityDescription() const
{
	return mBoard && mBoard->IsColdStorage()
		? u8"每次" + std::to_string(kRepairIce) + u8"冰块 · 恢复1000生命"
		: u8"每次" + std::to_string(kRepairSun) + u8"阳光 · 恢复1000生命";
}

bool IceStorageNut::TakeCrushImpact()
{
	if (!IsActive() || mIsPreview || IsSquished() || IsBungeeTargeted() || IsIceSealed()) return false;
	const int before = mPlantHealth;
	TakeDamage(kCrushDamage, DamageSource::ZOMBIE);
	if (!IsActive() || mPlantHealth <= 0 || mPlantHealth == before) return false;
	UpdateTexture();
	AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_BONK, 0.5f);
	return true;
}

void IceStorageNut::ResolveGargantuarSmash()
{
	TakeCrushImpact();
}

VehicleCrushResponse IceStorageNut::ResolveVehicleCrush()
{
	const bool struck = TakeCrushImpact();
	// 存活时继续挡车；只在实际承伤后推退，由车辆重新接近形成两次接触的间隔。
	return {OccupiesGridSlot(), struck ? kVehicleRetreatCells * CELL_COLLIDER_SIZE_X : 0.0f};
}

void IceStorageNut::Draw(Graphics* g)
{
	WallNut::Draw(g);
	if (!g || mIsPreview || !IsActive() || IsSquished()) return;
	const Vector p = GetPosition();
	const glm::vec4 cyan(150, 245, 255, 255);
	const char* label = mAutomatic ? u8"自动修复" : u8"手动修复";
	g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, 12, cyan, p.x - 25, p.y + 29);
	const float timer = mCooldownRemaining / kRepairCooldown;
	if (timer > 0) {
		g->FillRect(p.x - 25, p.y + 46, 50, 5, glm::vec4(25, 55, 65, 230));
		g->FillRect(p.x - 24, p.y + 47, 48 * timer, 3, cyan);
	}
}

void IceStorageNut::SaveExtraData(nlohmann::json& j) const
{
	WallNut::SaveExtraData(j);
	j["nutRepairCooldown"] = mCooldownRemaining;
	j["nutAutomatic"] = mAutomatic;
}

void IceStorageNut::LoadExtraData(const nlohmann::json& j)
{
	WallNut::LoadExtraData(j);
	const float cooldown = j.value("nutRepairCooldown", 0.0f);
	mCooldownRemaining = std::isfinite(cooldown) ? std::clamp(cooldown, 0.0f, kRepairCooldown) : 0.0f;
	mAutomatic = j.value("nutAutomatic", false);
	// 故意不读取旧 nutInvulnerableRemaining，不让续局恢复已删除的能力。
	mAnimator->SetTrackColor("anim_face", SDL_Color{255, 255, 255, 255});
}
