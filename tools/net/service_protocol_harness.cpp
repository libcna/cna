// SPDX-License-Identifier: MS-PL
#include "CnaService/Protocol.hpp"
#include <fstream>
#include <iostream>
#include <random>
int main(int argc,char** argv) {
    try {
        if(argc!=2)return 2;
        int checks=0;
        auto require=[&](bool value){++checks;if(!value)throw std::runtime_error("Protocol property failed.");};
        std::ifstream input(argv[1]);if(!input)return 2;
        for(const auto& vector:CnaService::Json::parse(input)) {
            std::string error="OK";
            try{CnaService::validateRequest(CnaService::parse(vector["input"].get<std::string>()));}
            catch(const CnaService::Error& exception){error=exception.code();}
            require(error==vector["error"]);
        }
        const std::string valid=R"({"v":1,"id":"check","game":"one","op":"hello","args":{}})";
        for(std::size_t length=0;length<valid.size();++length) {
            bool rejected=false;try{CnaService::validateRequest(CnaService::parse(valid.substr(0,length)));}catch(const CnaService::Error&){rejected=true;}
            require(rejected);
        }
        for(bool composite:{false,true}) {
            auto array=CnaService::Json::array();for(int i=0;i<257;++i)array.push_back(composite?CnaService::Json::object():CnaService::Json(i));
            bool rejected=false;try{(void)CnaService::parse(array.dump());}catch(const CnaService::Error& error){rejected=error.code()=="LIMIT_EXCEEDED";}
            require(rejected);
        }
        std::mt19937 random(0x434e41);
        for(int trial=0;trial<5000;++trial) {
            auto mutated=valid;for(int i=0;i<1+trial%4;++i)mutated[random()%mutated.size()]=static_cast<char>(random()%256);
            auto outcome=[&]{try{CnaService::validateRequest(CnaService::parse(mutated));return std::string("OK");}catch(const CnaService::Error& error){return error.code();}};
            require(outcome()==outcome());
        }
        std::cout<<checks<<" canonical protocol checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
