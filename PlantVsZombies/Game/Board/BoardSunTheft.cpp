#include "Board.h"
#include <algorithm>

Board::SunTheftRecord Board::GetSunTheftRecord(int zombieID) const
{
	const auto it = mSunTheftLedger.find(zombieID);
	return it == mSunTheftLedger.end() ? SunTheftRecord{} : it->second;
}

int Board::CommitSunTheft(int zombieID, int requested)
{
	if (zombieID <= 0 || requested <= 0) return 0;
	auto& record = mSunTheftLedger[zombieID];
	if (record.disabled || record.escaped) return 0;
	const int amount = std::max(0, std::min({requested, mSun, kSunTheftCapacity - record.stolen}));
	// 扣款和入罐在同一提交边沿，余额不会被预告阶段预扣。
	mSun -= amount;
	record.stolen += amount;
	record.carried += amount;
	return amount;
}

void Board::CloseSunTheft(int zombieID, bool escaped)
{
	const auto it = mSunTheftLedger.find(zombieID);
	if (it == mSunTheftLedger.end()) return;
	auto& record = it->second;
	if (!escaped && !record.escaped) mSun += std::min(record.carried, std::max(0, MAX_SUN - mSun));
	record.carried = 0;
	record.escaped = record.escaped || escaped;
	// 保留记录直到整关结束；钟匠重建同 ID 时不能复制已返还阳光。
}

void Board::DisableSunTheft(int zombieID)
{
	if (zombieID > 0) mSunTheftLedger[zombieID].disabled = true;
}
