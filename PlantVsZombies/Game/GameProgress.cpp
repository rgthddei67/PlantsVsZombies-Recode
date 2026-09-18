#include "GameProgress.h"
#include "Game/Board/Board.h"
#include "SceneManager.h"
#include "../DeltaTime.h"
#include "../ResourceManager.h"
#include "../ResourceKeys.h"
#include "../Logger.h"
#include <algorithm>
#include <memory>
#include <cmath>

namespace {
	constexpr int WAVES_PER_FLAG = 10;
	constexpr float FLAG_RAISE_HEIGHT = -10.0f;
	constexpr float FLAG_RAISE_DURATION = 1.5f;
}

GameProgress::GameProgress(Board* board)
	: mBoard(board)
{
	// mCurrentSliderValue/mTargetSliderValue/mLerpSpeed 均由头文件就地初始化
	if (!board) {
		LOG_ERROR("GameProgress") << "初始化失败，board为nullptr！";
		return;
	}

	this->CreateTransform(createPosition);

	m_flagMeter = std::make_unique<FlagMeter>(createPosition, 1.0f);

	// 从资源管理器获取纹理（返回 const Texture*）
	auto& rm = ResourceManager::GetInstance();
	using namespace ResourceKeys::Textures;
	const Texture* bgTex = rm.GetTexture(IMAGE_FLAG_METER);
	const Texture* fillTex = rm.GetTexture(IMAGE_FLAG_METERFULL);
	const Texture* headTex = rm.GetTexture(IMAGE_FLAGMETER_PART_HEAD);
	const Texture* middleTex = rm.GetTexture(IMAGE_FLAGMETERLEVELPROGRESS);

	m_flagMeter->SetImages(bgTex, fillTex, headTex, middleTex);
	if (board->IsColdStorage()) {
		m_flagMeter->SetImages(bgTex, fillTex, headTex, nullptr);
		m_playerIceMeter = std::make_unique<FlagMeter>(Vector(870, 531), 0.0f);
		m_playerIceMeter->SetImages(bgTex, fillTex, rm.GetTexture(IMAGE_COLD_STORAGE_ICE_HEAD), nullptr);
	}
}

GameProgress::~GameProgress()
{
	mBoard = nullptr;
	LOG_DEBUG("GameProgress") << "~GameProgress";
}

void GameProgress::Update()
{
	GameObject::Update();
	if (mBoard && mBoard->IsColdStorage()) {
		const auto& ice = mBoard->mColdStorage;
		// 复用原旗帜条从右侧填充的约定；数字保留超出初始量时的真实余额。
		m_flagMeter->SetProgress(1.0f - std::clamp(static_cast<float>(ice.enemyIce)
			/ std::max(1, ice.initialEnemyIce), 0.0f, 1.0f));
		m_playerIceMeter->SetProgress(1.0f - std::clamp(ice.playerIce / 200.0f, 0.0f, 1.0f));
		return;
	}
	float delta = DeltaTime::GetDeltaTime();
	if (m_flagMeter) {
		m_flagMeter->Update(delta);
	}

	mUpdateTimer += delta;

	if (mBoard && mUpdateTimer >= 0.4f)
	{
		mUpdateTimer = 0.0f;
		int currentWave = mBoard->mCurrentWave;
		int maxWave = mBoard->mMaxWave;
		mTargetSliderValue = 1.0f - (static_cast<float>(currentWave) / static_cast<float>(maxWave));
	}

	if (m_flagMeter)
	{
		mCurrentSliderValue += (mTargetSliderValue - mCurrentSliderValue) * mLerpSpeed * delta;

		if (std::fabs(mCurrentSliderValue - mTargetSliderValue) < 0.001f)
			mCurrentSliderValue = mTargetSliderValue;

		m_flagMeter->SetProgress(mCurrentSliderValue);
	}

	// 检测波次变化，触发旗子升起
	if (mBoard) {
		int currentWave = mBoard->mCurrentWave;
		if (currentWave != m_lastWave && currentWave > 0 && currentWave % WAVES_PER_FLAG == 0) {
			// SetupFlags 按第10、20……波的顺序保存旗子，所以波号可直接映射到索引。
			int flagIndex = currentWave / WAVES_PER_FLAG - 1;
			if (flagIndex >= 0 && flagIndex < m_flagCount) {
				m_flagMeter->RaiseFlag(flagIndex, FLAG_RAISE_HEIGHT, FLAG_RAISE_DURATION);
			}
		}
		m_lastWave = currentWave;
	}
}

void GameProgress::Draw(Graphics* g)
{
	GameObject::Draw(g);
	if (mBoard && mBoard->IsColdStorage()) {
		const auto& ice = mBoard->mColdStorage;
		g->FillRect(848, 501, 252, 99, glm::vec4(20, 35, 40, 195));
		m_playerIceMeter->Draw(g);
		m_flagMeter->Draw(g);
		g->DrawGlyphRun(u8"植物冰块 " + std::to_string(ice.playerIce), ResourceKeys::Fonts::FONT_FZCQ,
			16, glm::vec4(180, 245, 255, 255), 860, 504);
		g->DrawGlyphRun(u8"僵尸 " + std::to_string(ice.enemyIce) + u8"冰  补给" +
			std::to_string(static_cast<int>(std::ceil(ice.supplyRemaining))) + u8"秒", ResourceKeys::Fonts::FONT_FZCQ,
			15, glm::vec4(255, 215, 170, 255), 860, 556);
		return;
	}
	if (m_flagMeter)
		m_flagMeter->Draw(g);
}

void GameProgress::SetupFlags(const Texture* stickTex, const Texture* flagTex)
{
	if (!m_flagMeter) return;

	m_flagMeter->ClearFlags();
	m_flagCount = 0;
	if (!mBoard) return;
	if (mBoard->IsColdStorage()) return;

	const int maxWave = mBoard->mMaxWave;
	if (maxWave <= 0) return;

	m_flagCount = maxWave / WAVES_PER_FLAG;
	for (int flagNumber = 1; flagNumber <= m_flagCount; ++flagNumber)
	{
		// 原版从进度条右端向左映射实际旗帜波：15波时第10波在1/3处，
		// 25波时第10/20波分别在3/5、1/5处，不能按旗子数量等距摆放。
		const int flagWave = flagNumber * WAVES_PER_FLAG;
		const float pos = 1.0f - static_cast<float>(flagWave) / static_cast<float>(maxWave);
		m_flagMeter->AddFlag(stickTex, flagTex, pos);
	}
}

void GameProgress::InitializeRaisedFlags(float raiseY)
{
	if (!m_flagMeter || !mBoard) return;
	const int raisedCount = std::min(mBoard->mCurrentWave / WAVES_PER_FLAG, m_flagCount);
	for (int i = 0; i < raisedCount; ++i) {
		m_flagMeter->SetFlagRaiseImmediate(i, raiseY);
	}
}

void GameProgress::LowerAllFlags(float duration)
{
	if (!m_flagMeter) return;
	// targetY=0 即基准位；RaiseFlag 会从当前升起高度平滑插值回 0，得到"下降"动画。
	int count = static_cast<int>(m_flagMeter->GetFlagCount());
	for (int i = 0; i < count; ++i) {
		m_flagMeter->RaiseFlag(i, 0.0f, duration);
	}
	// 同步重置波次记录，避免 Update 中 currentWave 与 m_lastWave 的判定残留上一轮状态
	m_lastWave = mBoard ? mBoard->mCurrentWave : 0;
}

void GameProgress::SnapProgressToCurrentWave()
{
	if (!mBoard || !m_flagMeter) return;
	if (mBoard->IsColdStorage()) { Update(); return; }

	int maxWave = mBoard->mMaxWave;
	if (maxWave <= 0) return;

	int currentWave = mBoard->mCurrentWave;
	float value = 1.0f - static_cast<float>(currentWave) / static_cast<float>(maxWave);

	mTargetSliderValue = value;
	mCurrentSliderValue = value;
	mUpdateTimer = 0.0f;
	m_lastWave = currentWave;
	m_flagMeter->SetProgress(mCurrentSliderValue);
}
