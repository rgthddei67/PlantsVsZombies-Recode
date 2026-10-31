#include "Workshop.h"
#include <algorithm>

namespace Workshop {
int World::SelectedPlan(const Cell& c) const {
    if (!HasSelectionInput(c.type)) return 0;
    return c.inputs[1] >= 0 ? static_cast<int>(mCells[c.inputs[1]].signal) : c.plan;
}
int World::ActivePlan(int i) const {
    return Valid(i) ? SelectedPlan(mCells[i]) : 0;
}
int World::ActiveRoute(int i) const { return !Valid(i) ? 0 : ActivePlan(i) == 1 ? mCells[i].route2 : mCells[i].route; }
int World::ActiveFilter(int i) const { return !Valid(i) ? -1 : ActivePlan(i) == 1 && (mCells[i].type == Machine::Feeder || mCells[i].type == Machine::Cannon) ? mCells[i].filter2 : mCells[i].filter; }

bool World::SetPlan(int i, int plan) {
    if (!Valid(i) || mPhase == Phase::Dead || !HasSelectionInput(mCells[i].type) || plan < 0 || plan > 1
        || (mCells[i].type == Machine::Cooler && plan == 1 && mCompletedWaves < 8)) return false;
    auto& c = mCells[i]; c.plan = plan;
    if (c.type == Machine::Cooler && !c.processing && c.inputs[1] < 0 && c.recipe != plan) {
        c.recipe = plan; c.clock = 0;
    }
    return true;
}
bool World::SetStrictSplit(int i, bool strict) {
    if (!Valid(i) || mPhase == Phase::Dead || mCells[i].type != Machine::Splitter) return false;
    mCells[i].strictSplit = strict; return true;
}

bool World::CanCountSource(int source, CounterMode mode) const {
    if (!Valid(source)) return false;
    const auto type = mCells[source].type;
    if (mode == CounterMode::Signal) return IsLogic(type);
    if (mode == CounterMode::Produced) return type == Machine::Grower || type == Machine::SporeGrower || type == Machine::MelonGrower
        || type == Machine::Cooler || type == Machine::SalvagePort;
    return mode == CounterMode::Transferred && (HasDirectionalOutput(type) || type == Machine::Feeder || type == Machine::Cannon);
}

/** 累计物料量由正式产出/移交处递增，不把同一拍的连续物料压成一个布尔边沿。 */
std::uint64_t World::CounterSourceTotal(const Cell& counter) const {
    const int source = counter.inputs[0];
    if (counter.counterMode == CounterMode::Signal || !CanCountSource(source, counter.counterMode)) return 0;
    const auto& counts = counter.counterMode == CounterMode::Produced ? mCells[source].producedItems : mCells[source].transferredItems;
    return counter.filter < 0 ? std::accumulate(counts.begin(), counts.end(), std::uint64_t{}) : counts[counter.filter];
}
void World::ResetCounterObservation(Cell& counter) {
    counter.count = 0; counter.signal = false;
    counter.previousInput = counter.inputs[0] >= 0 && mCells[counter.inputs[0]].signal;
    counter.counterObserved = CounterSourceTotal(counter);
}
bool World::SetCounterMode(int i, CounterMode mode) {
    if (!Valid(i) || mPhase == Phase::Dead || mCells[i].type != Machine::Counter
        || mode < CounterMode::Signal || mode > CounterMode::Transferred) return false;
    auto& c = mCells[i]; c.counterMode = mode;
    if (c.inputs[0] >= 0 && !CanCountSource(c.inputs[0], mode)) c.inputs[0] = -1;
    ResetCounterObservation(c); return true;
}
bool World::SetCounterOutput(int i, CounterOutput output) {
    if (!Valid(i) || mPhase == Phase::Dead || mCells[i].type != Machine::Counter
        || output < CounterOutput::Hold || output > CounterOutput::Toggle) return false;
    auto& c = mCells[i]; c.counterOutput = output; ResetCounterObservation(c); return true;
}
bool World::SetSetting(int i, int setting) {
    if (!Valid(i) || mPhase == Phase::Dead || setting < 1 || setting > 1000000) return false;
    auto& c = mCells[i];
    if (c.type != Machine::Timer && c.type != Machine::StockSensor && c.type != Machine::EnemySensor && c.type != Machine::Counter) return false;
    if (c.type == Machine::Timer && setting > 120) return false;
    c.setting = setting;
    if (c.type == Machine::Timer) c.clock = 0;
    if (c.type == Machine::Counter) ResetCounterObservation(c);
    return true;
}

bool World::BeginCannonVolley(int i, bool manualRequest) {
    if (!Valid(i) || mPhase != Phase::Assault || mCells[i].type != Machine::Cannon) return false;
    auto& c = mCells[i]; const auto ammo = static_cast<Ammo>(ActiveFilter(i));
    if (c.volleyRemaining > 0 || c.clock < CannonChargeSeconds || (!manualRequest && (!c.manual || !Enabled(i)))
        || (ammo != Ammo::Melon && ammo != Ammo::IceMelon) || c.Inventory(ammo) < CannonVolleySize) return false;
    c.Inventory(ammo) -= CannonVolleySize;
    c.volleyAmmo = ammo; c.volleyRoute = ActiveRoute(i); c.volleyRemaining = CannonVolleySize;
    c.volleyClock = 0; c.clock = 0; return true;
}
bool World::CannonShotReady(int i) const {
    return Valid(i) && mPhase == Phase::Assault && mCells[i].type == Machine::Cannon
        && mCells[i].volleyRemaining > 0 && mCells[i].volleyClock <= 0;
}
void World::CommitCannonShot(int i) {
    if (!CannonShotReady(i)) return;
    auto& c = mCells[i]; --c.volleyRemaining;
    ++c.transferredItems[static_cast<int>(c.volleyAmmo)];
    c.volleyClock = c.volleyRemaining > 0 ? c.volleyClock + CannonShotInterval : 0;
}
}
