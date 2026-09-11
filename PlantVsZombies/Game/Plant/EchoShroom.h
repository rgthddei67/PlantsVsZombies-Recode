#pragma once
#include "FumeShroom.h"

/** 回声菇复用大喷菇运动与发射事件，在途攻击归 Board 所有。 */
class EchoShroom final : public FumeShroom {
public:
	using FumeShroom::FumeShroom;
protected:
	void SetupPlant() override;
	void OnWakeUp() override;
	bool HasZombieInRow() override;
	void FireFume() override;
};
