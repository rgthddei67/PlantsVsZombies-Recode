#include "Game/AutoTest/CobSparringTargets.h"
#include <cstdlib>
#include <iostream>

namespace {
using Json=nlohmann::json;
/** 用互相矛盾的原始坐标验证策略确实只读公共投影；不模拟实际绘制成功。 */
void Check(bool ok,const char* why) { if(!ok) {std::cerr<<why<<'\n';std::exit(1);} }
Json Entity() {return {{"id",7},{"type","ZOMBIE_NORMAL"},{"row",4},{"xInt",1140},
    {"bodyHealth",270},{"fogObscured",false},{"cobBlastTargetable",true},
    {"publicCobAim",{{"x",1095},{"row",0}}}};}
}
int main() {
    auto entity=Entity();
    auto observed=CobSparringTargets::Observe(Json::array({entity}),5,1100);
    Check(observed.size()==1 && observed[0]["xInt"]==1095 && observed[0]["row"]==0,
        "visible edge target uses in-bounds witness, not raw position");
    entity["xInt"]=-9000;entity["row"]=900;
    Check(CobSparringTargets::Observe(Json::array({entity}),5,1100)==observed,"raw position cannot change aim");
    entity=Entity();entity["publicCobAim"]=nullptr;
    auto hidden=CobSparringTargets::Observe(Json::array({entity}),5,1100);
    Check(hidden.empty(),"no public witness fails closed");
    entity["xInt"]=500;entity["bodyHealth"]=90000;entity["type"]="ZOMBIE_FLOOD_MORTAR";
    Check(CobSparringTargets::Observe(Json::array({entity}),5,1100)==hidden,
        "hidden position health and kind cannot influence targets");
    for (const char* flag:{"fogObscured","isDying"}) {
        entity=Entity();entity[flag]=true;
        Check(CobSparringTargets::Observe(Json::array({entity}),5,1100).empty(),"hidden or dying target rejected");
    }
    entity=Entity();entity["bodyHealth"]=0;
    Check(CobSparringTargets::Observe(Json::array({entity}),5,1100).empty(),"dead target rejected");
    entity=Entity();entity["cobBlastTargetable"]=false;
    Check(CobSparringTargets::Observe(Json::array({entity}),5,1100).empty(),"untargetable rejected");
    for(int x:{-1,1100,1200}) {
        entity=Entity();entity["publicCobAim"]["x"]=x;
        Check(CobSparringTargets::Observe(Json::array({entity}),5,1100).empty(),"outside public aim rejected");
    }
    for(int row:{-1,5}) {
        entity=Entity();entity["publicCobAim"]["row"]=row;
        Check(CobSparringTargets::Observe(Json::array({entity}),5,1100).empty(),"outside row rejected");
    }
    entity=Entity();entity["publicCobAim"]["x"]=1095.0;
    Check(CobSparringTargets::Observe(Json::array({entity}),5,1100).empty(),"malformed noninteger projection rejected");
    std::cout<<"cob sparring public targets passed\n";
}
