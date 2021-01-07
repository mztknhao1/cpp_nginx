#include "HandlerFactory.h"
#include "IRequestHandler.h"

IRequestHandler* HandlerFactory::getHandler(std::string key){
    if(stratagyMap.find(key)!=stratagyMap.end()){
        return stratagyMap[key];
    }
    return nullptr;
}

bool HandlerFactory::setHandler(std::string key, IRequestHandler* value){
    if(stratagyMap.find(key)!=stratagyMap.end()){
        return false;
    }else{
        stratagyMap[key] = value;
    }
    return true;
}