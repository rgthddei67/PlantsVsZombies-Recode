#include "Zombie.h"
#include "Game/Board/Board.h"

void Zombie::ApplyPrismMark()
{
	if (IsActive() && !mIsDead && !mIsDying && !IsMindControlled()) mPrismMarkRemaining = 6.0f;
}

int Zombie::ScaleStatusDamage(int damage) const
{
	const int marked = SurvivalPerkManager::ScaleNumericDamage(damage, GetPrismMarkRemaining() > 0.0f ? 1.25 : 1.0);
	return mBoard ? mBoard->ScaleMineFogDamage(marked, this) : marked;
}
