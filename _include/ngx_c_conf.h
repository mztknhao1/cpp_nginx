#ifndef __NGX_CONF_H__
#define __NGX_CONF_H__

#include <vector>

#include "ngx_global.h"

// 类遵循一定的命名规范，例如第一个字母是C后续的单词首字母大写
class CConfig{
    //-------------------下面是单例设计模式---------------------------
private:
    CConfig();
public:
    ~CConfig();

private:
    static CConfig *m_instance;

public:
    static CConfig* GetInstance(){
        if(m_instance == NULL){
            //锁
            if(m_instance == NULL){
                m_instance == new CConfig();
                static CGarhuishou cl;  //析构函数中会释放m_instance
            }
            //放锁
        }
        return m_instance;
    }
    class CGarhuishou{   //类中嵌套类，用于释放对象
    public:
        ~CGarhuishou(){
            if(CConfig::m_instance){
                delete CConfig::m_instance;
                CConfig::m_instance = NULL;
            }
        }

    };

public:
    bool Load(const char *pconfName);  //装载配置文件
    const char *GetString(const char *p_itemname);
    int GetIntDefault(const char *p_itemname, const int def);

public:
    std::vector<LPCConfItem> m_ConfigItemList;  //存储配置信息的列表
};


#endif
