#pragma once

// 仅由 ColdStorageSearch.cpp 在私有推演作用域包含，复用正式预测的伤害、评分和生命层入口。
/** 用逻辑原点落格，场外不夹到边缘水格；当前水面不推测未来涨退。 */
static bool ForecastWaterAt(const Snapshot& s,int row,float x) {
    const int col=static_cast<int>(std::floor((x-s.gridLeft)/s.cellWidth));
    return row>=0 && row<s.rows && col>=0 && col<s.columns && s.waterCells[row*s.columns+col];
}

/** 换气不吃硬控、减速或鼓舞；重新入水从完整潜水周期开始。 */
static void AdvanceDiverAir(const Snapshot& s,float time,std::vector<Unit>& units) {
    for(auto& u:units) {
        auto& d=u.diver;
        if(!d.present || u.body.health<=0 || u.body.spawnAt>time) continue;
        const bool water=ForecastWaterAt(s,u.body.row,u.body.x+u.body.blastAnchorOffset);
        if(!water || !d.inWater) {d.breathing=false;d.airRemaining=HeavyDiverRules::SubmergedSeconds;}
        d.inWater=water;
        if(!water) continue;
        d.airRemaining-=kStep;
        while(d.airRemaining<=0) {
            d.breathing=!d.breathing;
            d.airRemaining+=d.breathing ? HeavyDiverRules::BreathingSeconds : HeavyDiverRules::SubmergedSeconds;
        }
    }
}

/** 同一次弹丸先选最小稳定 ID 的伞，再扣独立耐久；最后一发完整拦截且无溅射/控制。 */
static bool InterceptParasol(const Snapshot& s,float time,std::vector<Unit>& units,int row,float x,
    float damage,ConstructionStats& stats) {
    const int col=static_cast<int>(std::floor((x-s.gridLeft)/s.cellWidth));
    if(col<0 || col>=s.columns) return false;
    Unit* best=nullptr;
    for(auto& u:units) {
        if(!u.parasol || u.parasolHealth<=0 || u.headless || u.body.health<=0
            || u.body.spawnAt>time || u.body.stopped>0) continue;
        const int own=static_cast<int>(std::floor((u.body.x+u.body.blastAnchorOffset-s.gridLeft)/s.cellWidth));
        if(own<0 || own>=s.columns || std::abs(u.body.row-row)>LotusParasolRules::Radius
            || std::abs(own-col)>LotusParasolRules::Radius) continue;
        if(!best || u.id<best->id) best=&u;
    }
    if(!best) return false;
    best->parasolHealth=std::max(0.0f,best->parasolHealth-damage*LotusParasolRules::DamageMultiplier);
    ++stats.parasolBlocks;stats.parasolAbsorbed+=damage;
    return true;
}

/** 从右向左只找第一组植物；普通层和南瓜遮挡后排，但不遮挡本组基座。 */
static int WatersideFrontPlant(const Snapshot& s,const Unit& u,const std::vector<Plant>& plants,bool salvage,int* column=nullptr) {
    const float x=u.body.x+u.body.blastAnchorOffset;
    for(int col=s.columns-1;col>=0;--col) {
        const float distance=x-(s.gridLeft+(col+.5f)*s.cellWidth);
        if(distance<-.5f*s.cellWidth) continue;
        if(distance>(salvage ? SalvageZombieRules::RangeCells : SludgeZombieRules::RangeCells)*s.cellWidth) break;
        int best=-1;
        for(size_t i=0;i<plants.size();++i) {
            const auto& p=plants[i];
            if(p.health<=0 || p.layer>2 || !OccupiesPlantCell(p,u.body.row,col)) continue;
            if(best<0 || p.layer>plants[best].layer
                || (p.layer==plants[best].layer && p.foundationKind==1)) best=static_cast<int>(i);
        }
        if(best>=0 && (salvage || plants[best].sludgeTargetable)) {
            if(column) *column=col;
            return best;
        }
    }
    return -1;
}

/** 双基座按正式优先级选水中睡莲、其次花盆；无合格基座不穿过前组。 */
static int SalvageFoundation(const Snapshot& s,const Unit& u,const std::vector<Plant>& plants) {
    int col=-1;
    const int front=WatersideFrontPlant(s,u,plants,true,&col);
    if(front<0) return -1;
    int selected=-1;
    for(size_t i=0;i<plants.size();++i) {
        const auto& p=plants[i];
        if(p.health<=0 || p.row!=u.body.row || p.column!=col || p.foundationKind==0) continue;
        if(p.foundationKind==1 && !s.waterCells[p.row*s.columns+p.column]) continue;
        if(selected<0 || p.foundationKind<plants[selected].foundationKind) selected=static_cast<int>(i);
    }
    if(selected<0 || !plants[selected].salvageable || plants[selected].immuneRemaining>0
        || plants[selected].burstProtection.invulnerable>0) return -1;
    return selected;
}

/** 未提交动作硬控即取消；装填按可行动秒推进，前摇停步并禁止重复啃食。 */
static bool AdvanceWatersideAction(const Snapshot& s,float time,Unit& u,float active,float rain,
    bool controlled,bool slowed,std::vector<Plant>& plants,std::vector<SludgeRay>& rays,
    Weights& features,ConstructionStats& stats) {
    if(!u.salvage.present && !u.sludge.present) return false;
    const bool salvage=u.salvage.present;
    float& windup=salvage ? u.salvage.windup : u.sludge.windup;
    float& reload=salvage ? u.salvage.cooldown : u.sludge.reload;
    int& targetID=salvage ? u.salvage.targetID : u.sludge.targetID;
    const auto cancel=[&] {
        if(windup>0) reload=salvage ? SalvageZombieRules::RetrySeconds : SludgeZombieRules::Retry;
        windup=0;targetID=0;
    };
    if(controlled || u.headless || (salvage && !u.salvage.hasHook)) {cancel();return false;}
    if(active<=0) return windup>0;
    const float rate=GoldenIceRules::Amplify(rain,u.goldenStacks)
        *(slowed ? GoldenIceRules::Amplify(ZombieMovementRules::NormalSlowAnimationFactor,u.goldenStacks) : 1);
    const float elapsed=active*rate;
    const int target=salvage ? SalvageFoundation(s,u,plants) : WatersideFrontPlant(s,u,plants,false);
    if(windup>0) {
        if(target<0 || plants[target].id!=targetID) {cancel();return false;}
        const float old=windup;
        windup=std::max(0.0f,windup-elapsed);
        if(windup>0) return true;
        targetID=0;
        if(salvage) {
            DamagePlant(plants[target],plants[target].health,false,features);
            reload=SalvageZombieRules::Cooldown;++stats.salvages;
        } else {
            rays.push_back({u.body.row,u.body.x+u.sludge.muzzleOffset,time+old/std::max(.001f,rate),
                SludgeZombieRules::RangeCells*s.cellWidth});
            reload=SludgeZombieRules::Reload;++stats.sludgeShots;
            if(u.helmHealth>0) {
                const float healed=std::min(float(SludgeZombieRules::ArmorRepair),std::max(0.0f,u.maximumHelm-u.helmHealth));
                u.helmHealth+=healed;u.body.health+=healed;stats.sludgeRepaired+=healed;
                const float credit=std::min(u.blastCredit,u.body.value*healed/std::max(1.0f,u.maximumBody+u.maximumHelm+u.maximumShield));
                u.blastCredit-=credit;features[6]-=credit;
            }
        }
        return true;
    }
    reload=std::max(0.0f,reload-elapsed);
    if(reload>0 || target<0 || u.body.x+u.body.blastAnchorOffset>s.gridLeft+s.columns*s.cellWidth) return false;
    targetID=plants[target].id;
    windup=salvage ? SalvageZombieRules::Windup : SludgeZombieRules::Windup;
    return true;
}

/** 在途泥团先消费镜片，再命中南瓜/本体/睡莲/花盆；不向下层溢出。 */
static void AdvanceSludgeRays(const Snapshot& s,float time,std::vector<SludgeRay>& rays,
    std::vector<Plant>& plants,Weights& features,ConstructionStats& stats) {
    for(auto it=rays.begin();it!=rays.end();) {
        const float travel=std::min(it->remaining,SludgeZombieRules::ProjectileSpeed*std::clamp(time+kStep-it->launchedAt,0.0f,kStep));
        const float next=it->x-travel;
        Plant* hit=nullptr;float best=std::numeric_limits<float>::max();
        for(auto& p:plants) {
            if(p.health<=0 || p.row!=it->row || p.layer>2) continue;
            const float right=s.gridLeft+(p.column+std::max(1,p.footprintColumns))*s.cellWidth;
            const float left=s.gridLeft+p.column*s.cellWidth;
            if(it->x<left || next>right) continue;
            const float distance=std::max(0.0f,it->x-right);
            if(std::max(time,it->launchedAt)+distance/SludgeZombieRules::ProjectileSpeed>=p.counterBlastAt) continue;
            const int priority=p.hostileMirrors>0 ? 10 : p.layer*2+(p.foundationKind==1 ? 1 : 0);
            const int previous=hit ? (hit->hostileMirrors>0 ? 10 : hit->layer*2+(hit->foundationKind==1 ? 1 : 0)) : -1;
            if(!hit || distance<best || (distance==best && priority>previous)) {hit=&p;best=distance;}
        }
        if(hit) {
            if(hit->hostileMirrors>0) --hit->hostileMirrors;
            else if(DamagePlant(*hit,it->damage*(hit->pumpkin ? SludgeZombieRules::PumpkinMultiplier : 1),false,features)) {
                ++stats.sludgeHits;
                if(hit->health>0) hit->sludgeUntil=std::max(time,it->launchedAt)+best/SludgeZombieRules::ProjectileSpeed+SludgeZombieRules::SlowSeconds;
            }
        }
        it->x=next;it->remaining-=travel;it->launchedAt=time+kStep;
        if(hit || it->remaining<=0 || next<s.gridLeft-60) it=rays.erase(it);else ++it;
    }
}

/** 只有接敌时推进抡锚；换目标重开前摇，命中走普通顶层伤害，不能当作巨人压扁。 */
static void AdvanceDiverSmash(Unit& u,Plant& target,float active,float rate,Weights& features,ConstructionStats& stats) {
    auto& d=u.diver;
    if(u.headless) return;
    if(d.eatingTargetID!=target.id) {d.eatingTargetID=target.id;d.smashRemaining=HeavyDiverRules::Windup;}
    d.smashRemaining-=active*rate;
    if(d.smashRemaining>0) return;
    DamagePlant(target,d.damage,false,features);++stats.diverSmashes;
    d.smashRemaining=std::max(0.0f,d.smashRemaining+HeavyDiverRules::SmashCycle);
}

/** 拆基座后的上层保留正式救援窗口；环境死亡没有僵尸击杀返冰或部队击杀见证。 */
static void AdvanceWatersideSupport(const Snapshot& s,std::vector<Plant>& plants) {
    if(!s.watersideHazards) return;
    for(auto& p:plants) {
        if(p.health<=0 || !p.supportHazard) continue;
        if(p.foundationKind==2 && p.row>=0 && p.row<s.rows && p.column>=0 && p.column<s.columns
            && s.potCells[p.row*s.columns+p.column]) continue;
        int hazard=0;
        for(int offset=0;offset<std::max(1,p.footprintColumns);++offset) {
            const int col=p.column+offset;
            if(col<0 || col>=s.columns || p.row<0 || p.row>=s.rows) continue;
            const int cell=p.row*s.columns+col;
            const bool water=s.waterCells[cell];
            if(p.aquatic) {if(!water) {hazard=3;break;}continue;}
            const bool lily=std::any_of(plants.begin(),plants.end(),[&](const Plant& base){
                return base.health>0 && base.row==p.row && base.column==col && base.foundationKind==1;
            });
            if(water && !p.amphibious && !lily) {hazard=1;break;}
            if(!water && s.potCells[cell] && std::none_of(plants.begin(),plants.end(),[&](const Plant& base){
                return base.health>0 && base.row==p.row && base.column==col && base.foundationKind==2;
            })) hazard=2;
        }
        if(hazard!=p.supportHazardKind || hazard==0) p.supportHazardElapsed=0;
        p.supportHazardKind=hazard;
        if(hazard==0) continue;
        p.supportHazardElapsed+=kStep;
        const float duration=hazard==1 ? WatersideFloodRules::DrowningSeconds
            : hazard==2 ? WatersideFloodRules::PotGraceSeconds : WatersideFloodRules::AquaticDrySeconds;
        if((hazard==3 && p.foundationKind==1) || p.supportHazardElapsed>=duration) ConsumeForecastPlant(p);
    }
}
