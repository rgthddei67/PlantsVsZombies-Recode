#include "Trophy.h"
#include "Game/Board/Board.h"
#include "AudioSystem.h"
#include "AdventureProgression.h"
#include "Game/Board/BoardPresentation.h"
#include "../GameApp.h"
#include "../DeltaTime.h"
#include "GameObjectManager.h"
#include "../ResourceManager.h"
#include "../ResourceKeys.h"
#include "../Graphics.h"

Trophy::Trophy(Board* board, const Vector& position)
	: GameObject(ObjectType::OBJECT_NONE)
	, mBoard(board)
{
	SetTag("Trophy");

	CreateTransform();
	GetTransform()->SetPosition(position);
	SetScale(BASE_SCALE);

	// 碰撞体仅供 ClickableComponent 做点击命中，不参与碰撞系统
	auto* collider = CreateCollider(Vector(60, 60), Vector(0, 0), ColliderType::CIRCLE);
	collider->layerMask = CollisionLayer::NONE;
	collider->collisionMask = CollisionLayer::NONE;
}

void Trophy::Start()
{
	GameObject::Start();
	SetScale(APPEAR_START_SCALE);  // 出现时从缩放起始值开始

	// 注册点击组件
	auto clickComponent = CreateClickable();
	if (clickComponent)
	{
		clickComponent->IsClickable = true;
		SetOnClickBack(clickComponent);
	}
}

void Trophy::SetOnClickBack(ClickableComponent* click)
{
	if (!click) return;
	click->onClick = [this]() {
		if (mIsGrowing) return;

		mStartPosition = GetPosition();
		mTargetPosition = Vector(SCENE_WIDTH / 2, SCENE_HEIGHT / 2);

		mIsGrowing = true;
		mIsMoving = true;
		mBoard->AcceptTrophy();
		AudioSystem::StopMusic();
		AudioSystem::PlaySound(ResourceKeys::Sounds::SOUND_WINMUSIC, 0.5f);
		auto& gameApp = GameAPP::GetInstance();
		gameApp.mGameInfoSaver.DeleteLevelData(mBoard);
		AdvanceAdventureProgress();
		gameApp.mGameInfoSaver.SavePlayerInfo();

		// 禁用点击，防止重复触发
		if (auto c = GetClickable())
			c->IsClickable = false;
		};
}

void Trophy::AdvanceAdventureProgress()
{
	if (!mBoard) return;

	auto& gameApp = GameAPP::GetInstance();
	mUnlockedPlant = AdventureProgression::AdvanceProgress(mBoard->mLevel,
		gameApp.mAdventureLevel, gameApp.mHaveCards);
}

void Trophy::Update()
{
	if (!mIsGrowing) {
		UpdateAppearScale();
		GameObject::Update();
		return;
	}
	GameObject::Update();

	mGrowTimer += DeltaTime::GetDeltaTime();
	float t = mGrowTimer / GROW_DURATION;
	if (t > 1.0f) t = 1.0f;
	// cubic ease-out：先快后慢
	float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
	SetScale(BASE_SCALE + (MAX_SCALE - BASE_SCALE) * eased);

	if (mIsMoving) {
		Vector newPos = Vector::lerp(mStartPosition, mTargetPosition, eased);
		SetPosition(newPos);
	}

	if (mGrowTimer >= GROW_DURATION) {
		if (mBoard && mBoard->GetPresentation())
			mBoard->GetPresentation()->SetReadyToBackMenu(mUnlockedPlant);
		GameObjectManager::GetInstance().DestroyGameObject(this);
	}
}

void Trophy::UpdateAppearScale()
{
	if (!mAppearing) return;

	mAppearTimer += DeltaTime::GetDeltaTime();
	float t = mAppearTimer / APPEAR_DURATION;
	t = t * t * t; // 立方缓入，开始慢，结束快
	if (t >= 1.0f) {
		t = 1.0f;
		mAppearing = false;
	}
	SetScale(APPEAR_START_SCALE + (BASE_SCALE - APPEAR_START_SCALE) * t);
}

void Trophy::Draw(Graphics* g)
{
	auto* transform = GetTransform();
	float scale = transform ? transform->GetScale() : 1.0f;
	Vector pos = GetPosition();

	if (mIsGrowing) {
		float t = mGrowTimer / GROW_DURATION;
		if (t > 1.0f) t = 1.0f;
		float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

		// 计算光圈大小和透明度
		float glowRadius = INITIAL_GLOW_RADIUS + (MAX_GLOW_RADIUS - INITIAL_GLOW_RADIUS) * eased;

		// 使用纹理绘制光圈（保持批处理）
		auto glowTex = ResourceManager::GetInstance().GetTexture(ResourceKeys::Textures::IMAGE_GLOWING);
		if (glowTex) {
			float glowSize = glowRadius * 2.0f;
			g->DrawTexture(glowTex,
				pos.x - glowSize * 0.5f,
				pos.y - glowSize * 0.5f,
				glowSize, glowSize,
				0.0f,
				glm::vec4(255.0f, 255.0f, 255.0f, 240.0f));
		}
	}

	// 绘制奖杯本体
	auto tex = ResourceManager::GetInstance().
		GetTexture(ResourceKeys::Textures::IMAGE_TROPHY);
	if (tex) {
		float w = tex->width * scale;
		float h = tex->height * scale;
		g->DrawTexture(tex, pos.x - w * 0.5f, pos.y - h * 0.5f, w, h);
	}
}

Vector Trophy::GetPosition() const
{
	if (GetTransform()) {
		return GetTransform()->GetPosition();
	}
	return Vector::zero();
}

void Trophy::SetPosition(const Vector& newPos)
{
	if (GetTransform()) {
		GetTransform()->SetPosition(newPos);
	}
}

void Trophy::SetScale(float scale)
{
	if (GetTransform()) {
		GetTransform()->SetScale(scale);
	}
}
