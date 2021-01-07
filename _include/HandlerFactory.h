#ifndef __HANDLERFACTORY_H__
#define __HANDLERFACTORY_H__

#include "IRequestHandler.h"
#include <unordered_map>
#include <string>

//处理工厂

class HandlerFactory{
public:
    HandlerFactory(){}

public:    
    bool setHandler(std::string key, IRequestHandler* value);
    IRequestHandler* getHandler(std::string key);

private:
    std::unordered_map<std::string, IRequestHandler*> stratagyMap;
};











#endif