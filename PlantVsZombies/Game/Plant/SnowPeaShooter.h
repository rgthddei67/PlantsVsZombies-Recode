#pragma once
#ifndef _SNOWPEA_SHOOTER_H
#define _SNOWPEA_SHOOTER_H
#include "Shooter.h"

class SnowPeaShooter : public Shooter {
public:
	using Shooter::Shooter;

protected:
	void ShootBullet() override
	{
		if (!mBoard) return;
		Vector bulletPosition = GetPosition() + Vector(30, -30);
		if (mBoard->CreatePlantBullet(BulletType::BULLET_SNOWPEA, mRow, bulletPosition, mPlantType))
			mBoard->RecordWorkshopShot(true);
	}
};

#endif
