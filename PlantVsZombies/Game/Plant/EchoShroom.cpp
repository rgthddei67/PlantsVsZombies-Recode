#include "EchoShroom.h"
#include "Game/Board/Board.h"

void EchoShroom::SetupPlant()
{
	mShootTime = 2.0f;
	mShootTimer = 1.0f;
	FumeShroom::SetupPlant();
}

void EchoShroom::OnWakeUp()
{
	Shroom::OnWakeUp();
	mShootTimer = 1.0f;
}

bool EchoShroom::HasZombieInRow()
{
	return mBoard && mBoard->HasEchoTarget(mRow, mColumn);
}

void EchoShroom::FireFume()
{
	if (!mBoard || GetSleepState()) return;
	mBoard->EmitEchoWave(mRow, mColumn);
	AudioSystem::PlaySound("SOUND_FUME", 0.18f);
}
