#ifndef __M_UTIL_H__
#define __M_UTIL_H__

#include <iostream>
#include <string>
#include <mysql/mysql.h>
#include "Log.hpp"

using namespace LogModule;

class mysql_util
{
public:
    static MYSQL *mysql_create(const std::string &host,
                               const std::string &username,
                               const std::string &passwd,
                               const std::string &dbname,
                               unsigned int port = 3306)
    {
        // 1.初始化mysql句柄
        MYSQL *mysql = mysql_init(nullptr);
        if (mysql == nullptr)
        {
            LOG(LogLevel::FATAL) << "mysql_init failed";
            return nullptr;
        }
        // 2.连接服务器
        if (mysql_real_connect(mysql, host.c_str(), username.c_str(), passwd.c_str(), dbname.c_str(), port, nullptr, 0) == nullptr)
        {
            LOG(LogLevel::FATAL) << "connect mysql server failed: " << mysql_error(mysql);
            mysql_close(mysql);
            return nullptr;
        }
        // 3.设置客户端字符集为utf8mb4（支持emoji）
        if (mysql_set_character_set(mysql, "utf8mb4") != 0)
        {
            LOG(LogLevel::FATAL) << "set client character failed: " << mysql_error(mysql);
            mysql_close(mysql);
            return nullptr;
        }
        return mysql;
    }

    static bool mysql_exec(MYSQL *mysql, const std::string &sql)
    {
        int ret = mysql_query(mysql, sql.c_str());
        if (ret != 0)
        {
            LOG(LogLevel::DEBUG) << sql;
            LOG(LogLevel::ERROR) << "mysql query failed: " << mysql_error(mysql);
            return false;
        }
        return true;
    }

    static void mysql_destroy(MYSQL *mysql)
    {
        if(mysql!=nullptr)
        {
            mysql_close(mysql);
        }
    }
};

#endif