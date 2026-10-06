#pragma once

/** 本次命中开始前的本格南瓜同时保护伤害和水压；不根据扣血后的状态补结算。 */
static bool HasFloodShell(const std::vector<Plant>& plants,const Plant& target) {
    return std::any_of(plants.begin(),plants.end(),[&](const Plant& p){
        return p.health>0 && p.layer==2 && p.row==target.row && p.column==target.column;
    });
}

// 仅由 ColdStorageSearch.cpp 在私有推演作用域内包含，共用已有伤害与南瓜事务。
/** 九格范围先冻结保护者集合，保证一个保护壳每枚水弹只承伤一次。 */
static std::vector<int> FloodRecipients(const Snapshot& s,const std::vector<Plant>& plants,int row,int column) {
    std::vector<int> result;
    for(size_t i=0;i<plants.size();++i) {
        const auto& p=plants[i];
        if(p.health<=0 || std::abs(p.row-row)>1 || std::abs(p.column-column)>1) continue;
        int shell=-1;
        for(size_t j=0;j<plants.size();++j) if(plants[j].health>0 && plants[j].layer==2
            && plants[j].row==p.row && plants[j].column==p.column) {shell=static_cast<int>(j);break;}
        const int recipient=shell>=0?shell:static_cast<int>(i);
        if(std::find(result.begin(),result.end(),recipient)==result.end()) result.push_back(recipient);
    }
    return result;
}

static bool FloodProtected(const std::vector<Plant>& plants,int row,int column,float time) {
    return std::any_of(plants.begin(),plants.end(),[&](const Plant& p) {
        return p.health>0 && p.shutdownUntil<=time && p.airborneDefenseRadius>=0
            && std::abs(p.row-row)<=p.airborneDefenseRadius && std::abs(p.column-column)<=p.airborneDefenseRadius;
    });
}

/** 后台用有界收益评分选点，不嵌套主线程的蒙特卡洛，也不读取其开关。 */
static int FindFloodTarget(const Snapshot& s,const Unit& unit,const std::vector<Plant>& plants,float time,int rain) {
    int best=-1;float score=-1;
    for(int row=std::max(0,unit.body.row-1);row<=std::min(s.rows-1,unit.body.row+1);++row)
        for(int col=0;col<s.columns;++col) {
            if(std::none_of(plants.begin(),plants.end(),[&](const Plant& p){return p.health>0 && p.row==row && p.column==col;}))continue;
            const float x=s.gridLeft+(col+.5f)*s.cellWidth;
            const float distance=unit.body.x+unit.body.blastAnchorOffset-x;
            if(distance<0 || distance>FloodMortarRules::RangeCells*s.cellWidth) continue;
            const auto recipients=FloodRecipients(s,plants,row,col);if(recipients.empty())continue;
            float value=0;
            for(int index:recipients) {
                const auto& p=plants[index];
                value+=std::min(p.health,float(FloodMortarRules::Damage(rain)*(p.layer==2?FloodMortarRules::PumpkinMultiplier:1)));
            }
            for(const auto& p:plants) if(p.health>0 && p.shutdownUntil<=time && !HasFloodShell(plants,p) && std::abs(p.row-row)<=1 && std::abs(p.column-col)<=1)
                value+=p.dps*(1-FloodMortarRules::AttackMultiplier)*std::max(0.0f,FloodMortarRules::SlowSeconds-std::max(0.0f,p.floodSlowUntil-time));
            if(FloodProtected(plants,row,col,time)) value=0;
            if(value>score) {score=value;best=row*s.columns+col;}
        }
    return best;
}

/** 首装、后续雨势装填与已发射水弹各自推进；死亡/控制只影响尚未提交的炮击。 */
static void AdvanceFloodMortars(const Snapshot& s,float time,int rain,std::vector<Unit>& units,
    std::vector<Plant>& plants,std::vector<FloodShot>& shots,Weights& features) {
    for(auto& unit:units) {
        if(!unit.floodMortar || unit.body.health<=unit.floodStopHealth || unit.body.spawnAt>time+kStep) continue;
        const float active=std::max(0.0f,time+kStep-std::max(time,unit.body.spawnAt)-unit.body.stopped);
        const float old=unit.floodReload;
        unit.floodReload=std::max(0.0f,old-active);
        if(unit.floodReload>0 || active<=0) continue;
        const int cell=FindFloodTarget(s,unit,plants,time,rain);if(cell<0)continue;
        shots.push_back({cell/s.columns,cell%s.columns,time+std::min(old,kStep)+FloodMortarRules::FlightSeconds,float(FloodMortarRules::Damage(rain))});
        unit.floodReload=FloodMortarRules::Reload(rain);
    }
    for(auto it=shots.begin();it!=shots.end();) {
        if(it->at>time+kStep) {++it;continue;}
        if(!FloodProtected(plants,it->row,it->column,time)) {
            const auto recipients=FloodRecipients(s,plants,it->row,it->column);
            for(auto& p:plants) if(p.health>0 && !HasFloodShell(plants,p) && std::abs(p.row-it->row)<=1 && std::abs(p.column-it->column)<=1)
                p.floodSlowUntil=it->at+FloodMortarRules::SlowSeconds;
            for(int index:recipients) DamagePlant(plants[index],it->damage*(plants[index].layer==2?FloodMortarRules::PumpkinMultiplier:1),false,features);
        }
        it=shots.erase(it);
    }
}

/** 竹矛按实际飞行方向消费至多五个稳定目标；已离膛竹矛不随植物死亡消失。 */
static void AdvanceBambooRays(const Snapshot& s,float time,std::vector<Unit>& units,std::vector<BambooRay>& rays) {
    for(auto it=rays.begin();it!=rays.end();) {
        const float next=it->x+RainBambooRules::Speed*kStep;
        std::vector<std::pair<float,size_t>> hits;
        for(size_t i=0;i<units.size();++i) {
            const auto& unit=units[i];const auto& u=unit.body;
            if(u.health<=0 || u.spawnAt>time+kStep || u.row!=it->row || !CanTargetProjectile(unit,false)
                || std::find(it->hitIDs.begin(),it->hitIDs.end(),unit.id)!=it->hitIDs.end()
                || u.x+u.boundsOffset>next || u.x+u.boundsOffset+u.boundsWidth<it->x) continue;
            hits.emplace_back(std::max(it->x,u.x+u.boundsOffset),i);
        }
        std::sort(hits.begin(),hits.end());
        for(const auto& [x,index]:hits) {
            if(it->hitIDs.size()>=5)break;
            ApplyDamage(units[index],RainBambooRules::Damage[it->hitIDs.size()],false,false,false,it->origin);
            it->hitIDs.push_back(units[index].id);
        }
        it->x=next;
        if(it->hitIDs.size()>=5 || next>s.rightEdge+80) it=rays.erase(it);else ++it;
    }
}
