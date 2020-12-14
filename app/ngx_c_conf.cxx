#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "ngx_func.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"

//静态成员赋值
CConfig *CConfig::m_instance = NULL;


//构造和析构函数
CConfig::CConfig(){

}

CConfig::~CConfig(){
    std::vector<LPCConfItem>::iterator pos;
    for(pos=m_ConfigItemList.begin();pos!=m_ConfigItemList.end();++pos){
        delete (*pos);
    }
    m_ConfigItemList.clear();
}

//装在配置文件
bool CConfig::Load(const char *pconfName){
    FILE *fp;
    fp = fopen(pconfName, "r");
    if(fp == NULL)
        return false;
    
    //每一行配置文件读出来在这里
    char linebuf[500];

    //文件打开成功
    while(!feof(fp)){
        //
        if(fgets(linebuf, 500, fp)==NULL) //每次读一行，一行不超过500字符
            continue;
        
        if(linebuf[0] == 0)
            continue;
        
        //处理注释行
        if(*linebuf==';' || *linebuf=='#' || *linebuf=='\n')
            continue;

        //后面的换行、空格、回车都处理掉
        while(strlen(linebuf)>0){
            if(linebuf[strlen(linebuf)-1]==10 || linebuf[strlen(linebuf)-1] == 13 || linebuf[strlen(linebuf)-1] == 32){
                linebuf[strlen(linebuf)-1] = 0;
            }else{
                break;
            }
        }

        if(linebuf[0] == 0)
            continue;
        if(*linebuf=='[')
            continue;

        // 这种类似于 "ListenPort = 5678"才能运行下来
        char *ptmp = strchr(linebuf, '=');  //在字符串中找第一个匹配'='之处
        if(ptmp != NULL){
            LPCConfItem p_confitem = new CConfItem;  
            memset(p_confitem, 0, sizeof(CConfItem));
            strncpy(p_confitem->ItemName, linebuf, (int)(ptmp-linebuf));
            strcpy(p_confitem->ItemContent,ptmp+1);

            Rtrim(p_confitem->ItemName);
            Ltrim(p_confitem->ItemName);
            Rtrim(p_confitem->ItemContent);
            Ltrim(p_confitem->ItemContent);

            // printf("itemname=%s | itemcontent=%s\n",p_confitem->ItemName,p_confitem->ItemContent);            
            m_ConfigItemList.push_back(p_confitem);  //内存要释放，因为这里是new出来的
        }
    }

    fclose(fp);
    return true;
}

int CConfig::GetIntDefault(const char *p_itemname, const int def){
    std::vector<LPCConfItem>::iterator pos;
    for(pos = m_ConfigItemList.begin();pos!=m_ConfigItemList.end();++pos){
        if(strcasecmp((*pos)->ItemName,p_itemname) ==0){
            return atoi((*pos)->ItemContent);
        }
    }
    return def;
}

const char *CConfig::GetString(const char *p_itemname){
    for(auto pitem:m_ConfigItemList){
        if(strcasecmp(pitem->ItemName, p_itemname) == 0){
            return pitem->ItemContent;
        }
    }
    return NULL;
}