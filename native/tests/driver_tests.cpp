#include "driver.hpp"
#include "tui.hpp"
#include <iostream>

namespace vlr {
int driverTests() {
    int count=0;
    const auto check=[&](bool value,const char *message){if(!value)throw std::runtime_error(message);count++;};
    Driver worker(true);
    std::vector<std::string> notes;
    auto found=worker.discover(false,false,notes);
    check(found.size()==2,"Driver bridge did not expose two independent zones");
    check(found[0].core()&&found[1].core()&&found[0].id!=found[1].id,"Native driver zone identities collided");
    Config config; config.catalog=found;
    for(const auto &d:found) config.device(d);
    config.validate();
    auto roundtrip=Config::from(config.json());
    check(roundtrip.json()==config.json(),"Native driver metadata did not survive config roundtrip");
    auto targets=plans(config);
    check(targets.size()==2,"Native backend excluded driver outputs");
    Json snapshot={{"Config",config.json()},{"Revision",0},{"Message","Ready"},{"Animating",false}};
    const auto menu=menuSnapshot(snapshot,0,2);
    check(menu.find(L"Auto / Offline")!=std::wstring::npos,"Native auto backend is missing from the system menu");
    config.backend=1; check(plans(config).empty(),"SDK backend included native driver outputs");
    config.backend=2; check(plans(config).empty(),"Hybrid backend can double-own a driver controller");
    worker.prepare(targets[0]); worker.prepare(targets[1]);
    worker.frame(found[0],{10,20,30,10,20,30});
    worker.frame(found[1],{40,50,60,40,50,60});
    auto cached=worker.testRequest({{"op","cached-colors"},{"id",found[0].driver.at("Id")}});
    check(cached["colors"]==Json::array({{10,20,30},{10,20,30},{40,50,60},{40,50,60}}),"Native zone frame overwrote another zone");
    worker.frame(found[0],{70,80,90,70,80,90});
    cached=worker.testRequest({{"op","cached-colors"},{"id",found[0].driver.at("Id")}});
    check(cached["colors"][2]==Json::array({40,50,60}),"Animated native output changed a static zone");
    bool rejected=false;
    try {worker.frame(found[0],{1,2,3});}catch(const std::exception&){rejected=true;}
    check(rejected,"Driver accepted a truncated zone frame");
    worker.frame(found[0],{1,2,3,4,5,6});
    worker.check();
    cached=worker.testRequest({{"op","cached-colors"},{"id",found[0].driver.at("Id")}});
    check(cached["colors"][0]==Json::array({1,2,3}),"Driver JSONL session failed after a rejected command");
    std::cout<<"Native child-worker integration: "<<count<<" checks; memory controller only.\n";
    return count;
}
}
