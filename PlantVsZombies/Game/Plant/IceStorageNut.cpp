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
		mInvulnerableRemaining = std::max(0.0f, mInvulnerableRemaining - delta);
		mCooldownRemaining = std::max(0.0f, mCooldownRemaining - delta);
	}
	WallNut::Update();
	// 只染色本体轨道表达护体，保留标准受击白光和动画；无敌结束立即恢复原色。
	mAnimator->SetTrackColor("anim_face", IsDamageImmune() ? SDL_Color{135, 225, 255, 255} : SDL_Color{255, 255, 255, 255});
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
	return mBoard && mBoard->IsColdStorage() ? u8"每次10冰块 · 恢复1000生命" : u8"每次100阳光 · 恢复1000生命";
}

bool IceStorageNut::TakeCrushImpact()
{
	if (!IsActive() || mIsPreview || IsSquished() || IsBungeeTargeted() || IsIceSealed() || IsDamageImmune()) return false;
	const int before = mPlantHealth;
	TakeDamage(kCrushDamage, DamageSource::ZOMBIE);
	if (!IsActive() || mPlantHealth <= 0 || mPlantHealth == before) return false;
	mInvulnerableRemaining = kInvulnerability;
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
	// 无敌仍占据挡车格；只有实际承伤后才后退，不能每帧把同一辆车推走。
	return {OccupiesGridSlot(), struck ? kVehicleRetreatCells * CELL_COLLIDER_SIZE_X : 0.0f};
}

void IceStorageNut::Draw(Graphics* g)
{
	WallNut::Draw(g);
	if (!g || mIsPreview || !IsActive() || IsSquished()) return;
	const Vector p = GetPosition();
	const glm::vec4 cyan(150, 245, 255, 255);
	const char* label = IsDamageImmune() ? u8"冰封护体" : mAutomatic ? u8"自动修复" : u8"手动修复";
	g->DrawText(label, ResourceKeys::Fonts::FONT_FZCQ, 12, cyan, p.x - 25, p.y + 29);
	const float timer = IsDamageImmune() ? mInvulnerableRemaining / kInvulnerability
		: mCooldownRemaining > 0 ? mCooldownRemaining / kRepairCooldown : 0.0f;
	if (timer > 0) {
		g->FillRect(p.x - 25, p.y + 46, 50, 5, glm::vec4(25, 55, 65, 230));
		g->FillRect(p.x - 24, p.y + 47, 48 * timer, 3, cyan);
	}
}

void IceStorageNut::SaveExtraData(nlohmann::json& j) const
{
	WallNut::SaveExtraData(j);
	j["nutInvulnerableRemaining"] = mInvulnerableRemaining;
	j["nutRepairCooldown"] = mCooldownRemaining;
	j["nutAutomatic"] = mAutomatic;
}

void IceStorageNut::LoadExtraData(const nlohmann::json& j)
{
	WallNut::LoadExtraData(j);
	const float immune = j.value("nutInvulnerableRemaining", 0.0f), cooldown = j.value("nutRepairCooldown", 0.0f);
	mInvulnerableRemaining = std::isfinite(immune) ? std::clamp(immune, 0.0f, kInvulnerability) : 0.0f;
	mCooldownRemaining = std::isfinite(cooldown) ? std::clamp(cooldown, 0.0f, kRepairCooldown) : 0.0f;
	mAutomatic = j.value("nutAutomatic", false);
	mAnimator->SetTrackColor("anim_face", IsDamageImmune() ? SDL_Color{135, 225, 255, 255} : SDL_Color{255, 255, 255, 255});
}
