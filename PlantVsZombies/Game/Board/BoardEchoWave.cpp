#include "Game/Board/Board.h"
#include "Game/Zombie/Zombie.h"
#include "Game/IceWall.h"
#include "Graphics.h"
#include <algorithm>
#include <cmath>

namespace {
	constexpr int kRange = 6; // 最短通路射程，格数
	constexpr int kDamage = 100; // 每轮对每个目标的完整基础伤害
	constexpr float kSpeed = 4.0f; // 声波传播速度，格/游戏秒
	constexpr float kHalfWidth = 0.18f; // 声波前沿半宽，格

	bool Eligible(const Zombie* z) {
		return z && z->IsActive() && !z->IsDying() && !z->IsMindControlled() && !z->IsFlying()
			&& z->CanBeTargetedByProjectile(false);
	}

	/** 把格内位置投影到发射时冻结的入边；不会跨墙寻找更短捷径。 */
	float PathDistance(const Board& board, const Board::EchoWave& wave, const Vector& pos, int r) {
		if (!board.IsMineBackground() && pos.x < board.GetCellCenterPosition(wave.row,wave.column).x) return -1;
		const int c = static_cast<int>(std::floor((pos.x - board.GetCellCenterPosition(0,0).x)
			/ CELL_COLLIDER_SIZE_X + 0.5f));
		if (c < 0 || c >= board.mColumns || r < 0 || r >= board.mRows) return -1;
		const int d = wave.distances[r * board.mColumns + c];
		if (d < 0) return -1;
		const float dx = (pos.x - board.GetCellCenterPosition(r,c).x) / CELL_COLLIDER_SIZE_X;
		const float dy = (pos.y - board.GetZombieSpawnY(r,pos.x)) / board.GetCellHeight();
		float result = static_cast<float>(d) + std::abs(dx) + std::abs(dy);
		for (const auto v : {std::array<int,2>{0,-1},{0,1},{-1,0},{1,0}}) {
			const int nr = r + v[0], nc = c + v[1];
			if (nr < 0 || nr >= board.mRows || nc < 0 || nc >= board.mColumns) continue;
			if (d > 0 && wave.distances[nr * board.mColumns + nc] == d - 1)
				result = std::min(result, d - dx * v[1] - dy * v[0]);
		}
		return result;
	}
}

Board::EchoWave Board::BuildEchoWave(int row, int column) const
{
	EchoWave wave;
	wave.row = row; wave.column = column;
	wave.distances.assign(mRows * mColumns, -1);
	if (row < 0 || row >= mRows || column < 0 || column >= mColumns) return wave;
	std::vector<int> queue{row * mColumns + column};
	wave.distances[queue[0]] = 0;
	for (size_t head = 0; head < queue.size(); ++head) {
		const int cell = queue[head], r = cell / mColumns, c = cell % mColumns;
		if (wave.distances[cell] >= kRange) continue;
		for (const auto v : {std::array<int,2>{0,1},{0,-1},{-1,0},{1,0}}) {
			if (!IsMineBackground() && (v[0] != 0 || v[1] != 1)) continue;
			const int nr = r + v[0], nc = c + v[1];
			if (nr < 0 || nr >= mRows || nc < 0 || nc >= mColumns) continue;
			const int next = nr * mColumns + nc;
			if (wave.distances[next] >= 0 || (IsMineBackground()
				&& (mMineGrid.rock[next] || !mMineGrid.connected[next]))) continue;
			wave.distances[next] = wave.distances[cell] + 1;
			queue.push_back(next);
		}
	}
	return wave;
}

/** 按本次可走路线查询声波能接触的敌人与独立冰墙。 */
bool Board::HasEchoTarget(int row, int column)
{
	const EchoWave wave = BuildEchoWave(row,column);
	bool found = false;
	for (int r = 0; r < mRows && !found; ++r) {
		if (std::none_of(wave.distances.begin() + r * mColumns, wave.distances.begin() + (r+1) * mColumns,
			[](int distance) { return distance >= 0; })) continue;
		mEntityRegistry.ForEachZombieInRow(r, [&](const Zombie* z) {
			if (!Eligible(z)) return;
			const float distance = PathDistance(*this,wave,z->GetPosition(),z->mRow);
			if (distance >= 0 && distance <= kRange) found = true;
		});
	}
	if (!found) if (const IceWall* wall = GetIceWall()) {
		const float distance = PathDistance(*this,wave,Vector(wall->GetCenterX(),
			GetZombieSpawnY(wall->GetRow(),wall->GetCenterX())),wall->GetRow());
		found = distance >= 0 && distance <= kRange;
	}
	return found;
}

/** 提交独立于发射者生命周期的路线快照。 */
void Board::EmitEchoWave(int row, int column)
{
	mEchoWaves.push_back(BuildEchoWave(row,column));
}

/** 推进所有前沿，在扫过区间结算每个目标一次并回收走完射程的声波。 */
void Board::UpdateEchoWaves(float delta)
{
	if (delta <= 0.0f) return;
	for (EchoWave& wave : mEchoWaves) {
		const float previous = wave.elapsed * kSpeed;
		wave.elapsed += delta;
		const float current = std::min(static_cast<float>(kRange), wave.elapsed * kSpeed);
		if (!wave.hitIceWall) if (IceWall* wall = GetIceWall()) {
			const float distance = PathDistance(*this,wave,Vector(wall->GetCenterX(),
				GetZombieSpawnY(wall->GetRow(),wall->GetCenterX())),wall->GetRow());
			if (distance >= 0 && distance <= kRange && distance >= previous-kHalfWidth && distance <= current+kHalfWidth) {
				wave.hitIceWall = true;
				wall->TakeProjectileDamage(GetPerkManager().ScalePlantDamage(kDamage),false);
			}
		}
		for (int r = 0; r < mRows; ++r) mEntityRegistry.ForEachZombieInRow(r,[&](Zombie* z) {
			if (!Eligible(z) || std::find(wave.hitIDs.begin(),wave.hitIDs.end(),z->mZombieID) != wave.hitIDs.end()) return;
			const float distance = PathDistance(*this,wave,z->GetPosition(),z->mRow);
			if (distance < 0 || distance > kRange || distance < previous - kHalfWidth || distance > current + kHalfWidth) return;
			wave.hitIDs.push_back(z->mZombieID);
			z->TakeDamage(kDamage,DamageSource::PLANT,false,false,false,PlantDamageOrigin::FromPlant(PlantType::PLANT_ECHOSHROOM));
		});
	}
	mEchoWaves.erase(std::remove_if(mEchoWaves.begin(),mEchoWaves.end(),[](const EchoWave& wave) {
		return wave.elapsed * kSpeed > kRange + kHalfWidth;
	}),mEchoWaves.end());
}

/** 沿冻结通路入边绘制多道弧线，分支和转角与伤害前沿一致。 */
void Board::DrawEchoWaves(Graphics* g) const
{
	if (!g) return;
	for (const EchoWave& wave : mEchoWaves) {
		const float front = wave.elapsed * kSpeed;
		for (int cell = 0; cell < static_cast<int>(wave.distances.size()); ++cell) {
			const int d = wave.distances[cell];
			if (d < 1 || front < d - 1 || front > d) continue;
			const int r = cell / mColumns, c = cell % mColumns;
			for (const auto v : {std::array<int,2>{0,-1},{0,1},{-1,0},{1,0}}) {
				const int nr = r + v[0], nc = c + v[1];
				if (nr < 0 || nr >= mRows || nc < 0 || nc >= mColumns || wave.distances[nr*mColumns+nc] != d-1) continue;
				const Vector a = GetCellCenterPosition(nr,nc), b = GetCellCenterPosition(r,c);
				const Vector p = a + (b-a) * (front - d + 1);
				// 三道弧线有主体与亮芯；沿每条入边推进，转角和分支均可读。
				for (int band = 0; band < 3; ++band) for (int segment = -5; segment < 5; ++segment) {
					const float s0 = segment * 0.1f, s1 = (segment+1) * 0.1f;
					const float f0 = (1-s0*s0*3)*8 - band*7, f1 = (1-s1*s1*3)*8 - band*7;
					const Vector q0(p.x - v[1]*f0 + v[0]*s0*65,p.y - v[0]*f0 - v[1]*s0*65);
					const Vector q1(p.x - v[1]*f1 + v[0]*s1*65,p.y - v[0]*f1 - v[1]*s1*65);
					for (int offset = -2; offset <= 2; ++offset)
						g->DrawLine(q0.x+v[1]*offset,q0.y+v[0]*offset,q1.x+v[1]*offset,q1.y+v[0]*offset,
							glm::vec4(95,190,230,110-band*25));
					g->DrawLine(q0.x,q0.y,q1.x,q1.y,glm::vec4(193,253,255,230-band*55));
				}
			}
		}
	}
}
