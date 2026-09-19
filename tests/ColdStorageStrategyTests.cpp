#include "Game/AI/ColdStorageStrategy.h"
#include <cstdlib>
#include <iostream>

/** Deterministic counterfactuals: triggering splash, existing targets, spacing and paid arrivals. */
int main()
{
	using namespace ColdStorageStrategy;
	SplashField field;
	field.directDps[1] = field.melonDps[1] = 27;
	field.plantX[1] = 300;
	SplashUnit guard;
	guard.row = 0; guard.x = 1080; guard.speed = 20; guard.health = 270; guard.value = 40;
	SplashUnit worker = guard;
	worker.x = 1120; worker.health = 500; worker.value = 150;
	SplashUnit bucket = guard;
	bucket.row = 1; bucket.x = 1140; bucket.health = 1500; bucket.value = 8; bucket.spawnAt = 1;
	auto check = [](bool condition, const char* name) {
		if (!condition) { std::cerr << "FAILED: " << name << '\n'; std::exit(1); }
	};
	const float ordinary = ForecastSplashExternality(field, {guard, worker}, {bucket});
	field.melonSlowDuty[1] = field.directSlowDuty[1] = 1;
	field.slowDuration[1] = 7.5f;
	const float winter = ForecastSplashExternality(field, {guard, worker}, {bucket});
	check(ordinary > 0 && winter > ordinary, "winter splash costs include lost progress");
	SplashUnit ahead = bucket;
	ahead.x = 650; ahead.spawnAt = 0;
	check(ForecastSplashExternality(field, {guard, worker, ahead}, {bucket}) == 0, "existing nearer target keeps splash away");
	SplashUnit alreadyTargeted = ahead;
	alreadyTargeted.x = 1100;
	check(ForecastSplashExternality(field, {guard, worker, alreadyTargeted}, {bucket}) == 0,
		"splash already hitting the same allies is not charged again");
	ahead.row = 0;
	check(ForecastSplashExternality(field, {ahead}, {bucket}) == 0, "far forward ally is outside splash");
	bucket.spawnAt = 25;
	check(ForecastSplashExternality(field, {guard, worker}, {bucket}) == 0, "later arrival does not immediately draw fire");
	bucket.spawnAt = 1;
	guard.spawnAt = worker.spawnAt = 1;
	check(ForecastSplashExternality(field, {guard, worker}, {bucket}) > 0, "paid waiting allies count after spawning");
	field.melonDps[1] = 0;
	check(ForecastSplashExternality(field, {guard, worker}, {bucket}) == 0, "single lane fire has no melon collateral");
	std::cout << "Splash counterfactuals passed; melon=" << ordinary << ", winter=" << winter << '\n';
}
