#include "Common.hpp"
#include "Http.hpp"
#include "mysql_util.hpp"
#include "Session.hpp"
#include <curl/curl.h>
#include <jsoncpp/json/json.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include <iomanip>
#include <unordered_map>
#include <algorithm>

// ===== .env 文件加载 =====
// 解析 KEY=VALUE 格式的行，调用 setenv 注入到进程环境
static void LoadEnvFile(const std::string& env_path)
{
    std::ifstream file(env_path);
    if (!file.is_open())
    {
        std::cerr << "[LoadEnv] 无法打开 .env 文件: " << env_path << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        // 去掉行首尾空白
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r");
        line = line.substr(start, end - start + 1);

        // 跳过空行和注释行
        if (line.empty() || line[0] == '#') continue;

        // 查找 '='
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // 去掉 key 尾部空白
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();

        // 去掉 value 首部空白
        size_t vstart = value.find_first_not_of(" \t");
        if (vstart == std::string::npos)
            value.clear();
        else
            value = value.substr(vstart);

        // 去掉引号包裹
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\'')))
        {
            value = value.substr(1, value.size() - 2);
        }

        if (key.empty()) continue;

        // 只覆盖尚未设置的环境变量 (override=false)
        setenv(key.c_str(), value.c_str(), 0);
    }
    file.close();
    std::cout << "[LoadEnv] 已加载环境变量文件: " << env_path << std::endl;
}

// 数据库配置
#define HOST "115.190.2.155"
#define USER "czj"
#define PASS "123456"
#define DBNAME "http_service"
#define PORT 3306

// SHA256加密函数
std::string sha256(const std::string& str)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, str.c_str(), str.size());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// 解析参数函数 (username=xxx&password=yyy&email=zzz)
std::unordered_map<std::string, std::string> ParseArgs(const std::string& args)
{
    std::unordered_map<std::string, std::string> params;
    std::string temp = args;
    
    size_t pos = 0;
    while ((pos = temp.find('&')) != std::string::npos)
    {
        std::string pair = temp.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos)
        {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            params[key] = Util::UrlDecode(value);
        }
        temp.erase(0, pos + 1);
    }
    
    // 处理最后一个参数
    size_t eq_pos = temp.find('=');
    if (eq_pos != std::string::npos)
    {
        std::string key = temp.substr(0, eq_pos);
        std::string value = temp.substr(eq_pos + 1);
        params[key] = Util::UrlDecode(value);
    }
    
    return params;
}

void Login(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "登录请求: " << req.Args();
    
    // 解析参数
    auto params = ParseArgs(req.Args());
    std::string username = params["username"];
    std::string password = params["password"];
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 参数校验
    if (username.empty() || password.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "用户名或密码不能为空";
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 密码加密
    std::string encrypted_password = sha256(password);
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 查询用户
    std::string sql = "SELECT id, username, is_vip FROM users WHERE username='" + 
                      username + "' AND password='" + encrypted_password + "'";
    
    if (!mysql_util::mysql_exec(mysql, sql))
    {
        response_json["success"] = false;
        response_json["message"] = "数据库查询失败";
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 获取查询结果
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "获取查询结果失败";
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row)
    {
        // 登录成功
        response_json["success"] = true;
        response_json["message"] = "登录成功";
        response_json["user_id"] = row[0];
        response_json["username"] = row[1];
        response_json["is_vip"] = (std::string(row[2]) == "1");
        
        // 更新最后登录时间
        std::string update_sql = "UPDATE users SET last_login=NOW() WHERE username='" + username + "'";
        mysql_util::mysql_exec(mysql, update_sql);
        
        // 设置登录Cookie（存储用户名，有效期7天）
        resp.SetHeader("Set-Cookie", SessionUtil::GenerateLoginCookie(username));
        
        LOG(LogLevel::INFO) << "用户 " << username << " 登录成功，已设置Cookie";
    }
    else
    {
        // 登录失败
        response_json["success"] = false;
        response_json["message"] = "用户名或密码错误";
        LOG(LogLevel::WARNING) << "用户 " << username << " 登录失败";
    }
    
    mysql_free_result(result);
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

void Register(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "注册请求: " << req.Args();
    
    // 解析参数
    auto params = ParseArgs(req.Args());
    std::string username = params["username"];
    std::string password = params["password"];
    std::string email = params["email"];
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 参数校验
    if (username.empty() || password.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "用户名或密码不能为空";
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 用户名长度校验
    if (username.length() < 3 || username.length() > 20)
    {
        response_json["success"] = false;
        response_json["message"] = "用户名长度必须在3-20个字符之间";
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 密码长度校验
    if (password.length() < 6)
    {
        response_json["success"] = false;
        response_json["message"] = "密码长度至少6个字符";
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 密码加密
    std::string encrypted_password = sha256(password);
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 检查用户名是否已存在
    std::string check_sql = "SELECT id FROM users WHERE username='" + username + "'";
    if (!mysql_util::mysql_exec(mysql, check_sql))
    {
        response_json["success"] = false;
        response_json["message"] = "数据库查询失败";
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        if (row)
        {
            // 用户名已存在
            response_json["success"] = false;
            response_json["message"] = "用户名已存在";
            mysql_free_result(result);
            mysql_util::mysql_destroy(mysql);
            
            std::string response_text = Json::writeString(writer, response_json);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
        mysql_free_result(result);
    }
    
    // 插入新用户
    std::string insert_sql = "INSERT INTO users (username, password, email) VALUES ('" + 
                            username + "', '" + encrypted_password + "', '" + email + "')";
    
    if (mysql_util::mysql_exec(mysql, insert_sql))
    {
        response_json["success"] = true;
        response_json["message"] = "注册成功";
        response_json["username"] = username;
        LOG(LogLevel::INFO) << "用户 " << username << " 注册成功";
    }
    else
    {
        response_json["success"] = false;
        response_json["message"] = "注册失败，请稍后重试";
        LOG(LogLevel::ERROR) << "用户 " << username << " 注册失败";
    }
    
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

void VipCheck(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "VIP检查请求: " << req.Args();
    
    // 解析参数
    auto params = ParseArgs(req.Args());
    std::string username = params["username"];
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 参数校验
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "用户名不能为空";
        response_json["is_vip"] = false;
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        response_json["is_vip"] = false;
        std::string response_text = Json::writeString(writer, response_json);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 查询用户VIP状态
    std::string sql = "SELECT is_vip, username FROM users WHERE username='" + username + "'";
    
    if (!mysql_util::mysql_exec(mysql, sql))
    {
        response_json["success"] = false;
        response_json["message"] = "数据库查询失败";
        response_json["is_vip"] = false;
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 获取查询结果
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "获取查询结果失败";
        response_json["is_vip"] = false;
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row)
    {
        // 找到用户
        bool is_vip = (std::string(row[0]) == "1");
        response_json["success"] = true;
        response_json["username"] = row[1];
        response_json["is_vip"] = is_vip;
        response_json["message"] = is_vip ? "该用户是VIP会员" : "该用户不是VIP会员";
        LOG(LogLevel::INFO) << "用户 " << username << " VIP状态: " << (is_vip ? "是" : "否");
    }
    else
    {
        // 用户不存在
        response_json["success"] = false;
        response_json["message"] = "用户不存在";
        response_json["is_vip"] = false;
        LOG(LogLevel::WARNING) << "VIP检查: 用户 " << username << " 不存在";
    }
    
    mysql_free_result(result);
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// 登出接口
void Logout(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "登出请求";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 清除Cookie
    resp.SetHeader("Set-Cookie", SessionUtil::GenerateLogoutCookie());
    
    response_json["success"] = true;
    response_json["message"] = "登出成功";
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
    
    LOG(LogLevel::INFO) << "用户登出成功";
}

// 获取当前登录用户信息
void GetCurrentUser(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "获取当前登录用户信息";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 从Cookie获取用户名
    std::string cookie_header = req.GetHeader("Cookie");
    std::string username = SessionUtil::GetUsernameFromCookie(cookie_header);
    
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["logged_in"] = false;
        response_json["message"] = "未登录";
    }
    else
    {
        // 查询用户信息
        MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
        if (mysql != nullptr)
        {
            std::string sql = "SELECT id, username, email, avatar, is_vip, created_at, last_login FROM users WHERE username='" + username + "'";
            if (mysql_util::mysql_exec(mysql, sql))
            {
                MYSQL_RES* result = mysql_store_result(mysql);
                if (result != nullptr)
                {
                    MYSQL_ROW row = mysql_fetch_row(result);
                    if (row)
                    {
                        response_json["success"] = true;
                        response_json["logged_in"] = true;
                        response_json["user_id"] = row[0];
                        response_json["username"] = row[1];
                        response_json["email"] = row[2] ? row[2] : "";
                        response_json["avatar"] = row[3] ? row[3] : "";  // 头像字段
                        response_json["is_vip"] = (std::string(row[4]) == "1");
                        response_json["created_at"] = row[5] ? row[5] : "";
                        response_json["last_login"] = row[6] ? row[6] : "";
                    }
                    else
                    {
                        response_json["success"] = false;
                        response_json["logged_in"] = false;
                        response_json["message"] = "用户不存在";
                    }
                    mysql_free_result(result);
                }
            }
            mysql_util::mysql_destroy(mysql);
        }
    }
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

void Search(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << req.Args() << ",我们成功进入到了处理数据的逻辑";
    std::string text = "hello: " + req.Args();
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "text/plain"); //文字类型
    resp.SetHeader("Content-Length", std::to_string(text.size()));
    resp.SetText(text);
}

// 查询聊天历史记录
void ChatHistory(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "查询聊天历史记录";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 从Cookie获取登录用户名
    std::string cookie_header = req.GetHeader("Cookie");
    std::string username = SessionUtil::GetUsernameFromCookie(cookie_header);
    
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["logged_in"] = false;
        response_json["message"] = "请先登录";
        response_json["history"] = Json::Value(Json::arrayValue);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 解析参数
    auto params = ParseArgs(req.Args());
    int limit = 100;
    if (params.find("limit") != params.end())
    {
        try {
            limit = std::stoi(params["limit"]);
            if (limit < 1) limit = 100;
            if (limit > 500) limit = 500;
        } catch(...) {
            limit = 100;
        }
    }
    
    // 获取session_id参数（如果有）
    std::string session_id = params.find("session_id") != params.end() ? params["session_id"] : "";
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        response_json["history"] = Json::Value(Json::arrayValue);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 查询该用户的聊天记录
    std::string sql;
    if (!session_id.empty()) {
        // 指定了session_id，验证该session属于当前用户
        sql = "SELECT ch.id, ch.user_message, ch.ai_response, ch.model, ch.created_at "
              "FROM chat_history ch "
              "JOIN sessions s ON ch.session_id = s.session_id "
              "WHERE s.username='" + username + "' AND ch.session_id='" + session_id + "' "
              "ORDER BY ch.created_at ASC LIMIT " + std::to_string(limit);
    } else {
        // 没有指定session_id，查询用户最近的session的聊天记录
        sql = "SELECT ch.id, ch.user_message, ch.ai_response, ch.model, ch.created_at "
              "FROM chat_history ch "
              "JOIN sessions s ON ch.session_id = s.session_id "
              "WHERE s.username='" + username + "' "
              "ORDER BY ch.created_at ASC LIMIT " + std::to_string(limit);
    }
    
    if (!mysql_util::mysql_exec(mysql, sql))
    {
        response_json["success"] = false;
        response_json["message"] = "数据库查询失败";
        response_json["history"] = Json::Value(Json::arrayValue);
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 获取查询结果
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "获取查询结果失败";
        response_json["history"] = Json::Value(Json::arrayValue);
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 构建历史记录数组
    Json::Value history_array(Json::arrayValue);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)))
    {
        Json::Value record;
        record["id"] = row[0];
        record["user_message"] = row[1] ? row[1] : "";
        record["ai_response"] = row[2] ? row[2] : "";
        record["model"] = row[3] ? row[3] : "";
        record["created_at"] = row[4] ? row[4] : "";
        history_array.append(record);
    }
    
    mysql_free_result(result);
    mysql_util::mysql_destroy(mysql);
    
    response_json["success"] = true;
    response_json["message"] = "查询成功";
    response_json["count"] = (int)history_array.size();
    response_json["history"] = history_array;
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
    
    LOG(LogLevel::INFO) << "返回" << history_array.size() << "条聊天记录";
}

// 获取会话列表
void GetSessions(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "获取会话列表";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 从Cookie获取登录用户名
    std::string cookie_header = req.GetHeader("Cookie");
    std::string username = SessionUtil::GetUsernameFromCookie(cookie_header);
    
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["logged_in"] = false;
        response_json["message"] = "请先登录";
        response_json["sessions"] = Json::Value(Json::arrayValue);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 解析参数，获取当前选中的session_id（可选）
    auto params = ParseArgs(req.Args());
    std::string current_session_id = params.find("current_session_id") != params.end() ? params["current_session_id"] : "";
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        response_json["sessions"] = Json::Value(Json::arrayValue);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 查询该用户的所有session，按更新时间倒序
    std::string sql = "SELECT session_id, session_name, created_at, updated_at, message_count FROM sessions WHERE username='" + username + "' ORDER BY updated_at DESC LIMIT 50";
    
    if (!mysql_util::mysql_exec(mysql, sql))
    {
        response_json["success"] = false;
        response_json["message"] = "数据库查询失败";
        response_json["sessions"] = Json::Value(Json::arrayValue);
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "获取查询结果失败";
        response_json["sessions"] = Json::Value(Json::arrayValue);
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    Json::Value sessions_array(Json::arrayValue);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)))
    {
        Json::Value session;
        session["session_id"] = row[0] ? row[0] : "";
        session["session_name"] = row[1] ? row[1] : "新对话";
        session["created_at"] = row[2] ? row[2] : "";
        session["updated_at"] = row[3] ? row[3] : "";
        session["message_count"] = row[4] ? std::stoi(row[4]) : 0;
        session["is_current"] = (std::string(row[0]) == current_session_id);
        sessions_array.append(session);
    }
    
    mysql_free_result(result);
    mysql_util::mysql_destroy(mysql);
    
    response_json["success"] = true;
    response_json["message"] = "查询成功";
    response_json["count"] = (int)sessions_array.size();
    response_json["current_session_id"] = current_session_id;
    response_json["sessions"] = sessions_array;
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
    
    LOG(LogLevel::INFO) << "返回" << sessions_array.size() << "个会话";
}

// 创建新会话
void CreateSession(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "创建新会话";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 从Cookie获取登录用户名
    std::string cookie_header = req.GetHeader("Cookie");
    std::string username = SessionUtil::GetUsernameFromCookie(cookie_header);
    
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["logged_in"] = false;
        response_json["message"] = "请先登录";
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    std::string new_session_id = SessionUtil::GenerateSessionId();
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 创建会话时关联当前登录用户
    std::string insert_sql = "INSERT INTO sessions (session_id, username, session_name, created_at) VALUES ('" + 
                            new_session_id + "', '" + username + "', '新对话', NOW())";
    
    if (mysql_util::mysql_exec(mysql, insert_sql))
    {
        response_json["success"] = true;
        response_json["message"] = "会话创建成功";
        response_json["session_id"] = new_session_id;
        
        LOG(LogLevel::INFO) << "用户 " << username << " 创建新会话: " << new_session_id;
    }
    else
    {
        response_json["success"] = false;
        response_json["message"] = "会话创建失败";
    }
    
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// 切换会话（验证会话归属）
void SwitchSession(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "切换会话";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 从Cookie获取登录用户名
    std::string cookie_header = req.GetHeader("Cookie");
    std::string username = SessionUtil::GetUsernameFromCookie(cookie_header);
    
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["logged_in"] = false;
        response_json["message"] = "请先登录";
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    auto params = ParseArgs(req.Args());
    std::string target_session_id = params["session_id"];
    
    if (target_session_id.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "缺少会话ID参数";
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 验证该会话属于当前用户
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql != nullptr)
    {
        std::string check_sql = "SELECT session_id FROM sessions WHERE session_id='" + target_session_id + "' AND username='" + username + "'";
        if (mysql_util::mysql_exec(mysql, check_sql))
        {
            MYSQL_RES* result = mysql_store_result(mysql);
            if (result != nullptr)
            {
                MYSQL_ROW row = mysql_fetch_row(result);
                if (row)
                {
                    response_json["success"] = true;
                    response_json["message"] = "会话切换成功";
                    response_json["session_id"] = target_session_id;
                    LOG(LogLevel::INFO) << "用户 " << username << " 切换到会话: " << target_session_id;
                }
                else
                {
                    response_json["success"] = false;
                    response_json["message"] = "会话不存在或无权访问";
                }
                mysql_free_result(result);
            }
        }
        mysql_util::mysql_destroy(mysql);
    }
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// 重命名会话
void RenameSession(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "重命名会话";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 验证登录
    std::string username = SessionUtil::GetUsernameFromCookie(req.GetHeader("Cookie"));
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "请先登录";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    auto params = ParseArgs(req.Args());
    std::string session_id = params["session_id"];
    std::string new_name = params["new_name"];
    
    if (session_id.empty() || new_name.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "参数不完整";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 转义新名称
    char* escaped_name = new char[new_name.length() * 2 + 1];
    mysql_real_escape_string(mysql, escaped_name, new_name.c_str(), new_name.length());
    
    // 只能修改自己的会话
    std::string update_sql = "UPDATE sessions SET session_name='" + std::string(escaped_name) + 
                            "' WHERE session_id='" + session_id + "' AND username='" + username + "'";
    
    delete[] escaped_name;
    
    if (mysql_util::mysql_exec(mysql, update_sql))
    {
        if (mysql_affected_rows(mysql) > 0)
        {
            response_json["success"] = true;
            response_json["message"] = "会话重命名成功";
            LOG(LogLevel::INFO) << "用户 " << username << " 重命名会话 " << session_id;
        }
        else
        {
            response_json["success"] = false;
            response_json["message"] = "会话不存在或无权限";
        }
    }
    else
    {
        response_json["success"] = false;
        response_json["message"] = "重命名失败";
    }
    
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// 删除会话
void DeleteSession(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "删除会话";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 验证登录
    std::string username = SessionUtil::GetUsernameFromCookie(req.GetHeader("Cookie"));
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "请先登录";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    auto params = ParseArgs(req.Args());
    std::string session_id = params["session_id"];
    
    if (session_id.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "会话ID不能为空";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 先删除该会话的所有聊天记录
    std::string delete_chat_sql = "DELETE FROM chat_history WHERE session_id='" + session_id + "'";
    mysql_util::mysql_exec(mysql, delete_chat_sql);
    
    // 再删除会话（只能删除自己的）
    std::string delete_session_sql = "DELETE FROM sessions WHERE session_id='" + session_id + 
                                     "' AND username='" + username + "'";
    
    if (mysql_util::mysql_exec(mysql, delete_session_sql))
    {
        if (mysql_affected_rows(mysql) > 0)
        {
            response_json["success"] = true;
            response_json["message"] = "会话删除成功";
            LOG(LogLevel::INFO) << "用户 " << username << " 删除会话 " << session_id;
        }
        else
        {
            response_json["success"] = false;
            response_json["message"] = "会话不存在或无权限";
        }
    }
    else
    {
        response_json["success"] = false;
        response_json["message"] = "删除失败";
    }
    
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// 修改邮箱（需验证旧密码）
void UpdateEmail(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "修改邮箱";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 验证登录
    std::string username = SessionUtil::GetUsernameFromCookie(req.GetHeader("Cookie"));
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "请先登录";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    auto params = ParseArgs(req.Args());
    std::string old_password = params["old_password"];
    std::string new_email = params["new_email"];
    
    if (old_password.empty() || new_email.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "参数不完整";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 加密旧密码
    std::string encrypted_old_password = sha256(old_password);
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 验证旧密码
    std::string check_sql = "SELECT id FROM users WHERE username='" + username + 
                           "' AND password='" + encrypted_old_password + "'";
    
    if (!mysql_util::mysql_exec(mysql, check_sql))
    {
        response_json["success"] = false;
        response_json["message"] = "密码验证失败";
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        if (!row)
        {
            // 密码错误
            response_json["success"] = false;
            response_json["message"] = "密码错误";
            mysql_free_result(result);
            mysql_util::mysql_destroy(mysql);
            
            std::string response_text = Json::writeString(writer, response_json);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
        mysql_free_result(result);
    }
    
    // 转义新邮箱
    char* escaped_email = new char[new_email.length() * 2 + 1];
    mysql_real_escape_string(mysql, escaped_email, new_email.c_str(), new_email.length());
    
    // 更新邮箱
    std::string update_sql = "UPDATE users SET email='" + std::string(escaped_email) + 
                            "' WHERE username='" + username + "'";
    
    delete[] escaped_email;
    
    if (mysql_util::mysql_exec(mysql, update_sql))
    {
        response_json["success"] = true;
        response_json["message"] = "邮箱修改成功";
        response_json["new_email"] = new_email;
        LOG(LogLevel::INFO) << "用户 " << username << " 修改邮箱";
    }
    else
    {
        response_json["success"] = false;
        response_json["message"] = "邮箱修改失败";
    }
    
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// 上传头像
void UploadAvatar(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "上传头像";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 验证登录
    std::string username = SessionUtil::GetUsernameFromCookie(req.GetHeader("Cookie"));
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "请先登录";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    auto params = ParseArgs(req.Args());
    std::string avatar_data = params["image_data"];
    
    if (avatar_data.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "头像数据为空";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 直接存储emoji头像（简化实现）
    // avatar_data就是emoji字符，如：👤、👨、🤖等
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 转义头像数据
    char* escaped_avatar = new char[avatar_data.length() * 2 + 1];
    mysql_real_escape_string(mysql, escaped_avatar, avatar_data.c_str(), avatar_data.length());
    
    // 更新用户头像
    std::string update_sql = "UPDATE users SET avatar='" + std::string(escaped_avatar) + 
                            "' WHERE username='" + username + "'";
    
    delete[] escaped_avatar;
    
    if (mysql_util::mysql_exec(mysql, update_sql))
    {
        response_json["success"] = true;
        response_json["message"] = "头像更换成功";
        response_json["avatar"] = avatar_data;
        LOG(LogLevel::INFO) << "用户 " << username << " 更换头像: " << avatar_data;
    }
    else
    {
        response_json["success"] = false;
        response_json["message"] = "头像上传失败";
    }
    
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// 修改密码（需验证旧密码）
void UpdatePassword(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "修改密码";
    
    Json::Value response_json;
    Json::StreamWriterBuilder writer;
    
    // 验证登录
    std::string username = SessionUtil::GetUsernameFromCookie(req.GetHeader("Cookie"));
    if (username.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "请先登录";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    auto params = ParseArgs(req.Args());
    std::string old_password = params["old_password"];
    std::string new_password = params["new_password"];
    
    if (old_password.empty() || new_password.empty())
    {
        response_json["success"] = false;
        response_json["message"] = "参数不完整";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 验证新密码长度
    if (new_password.length() < 6)
    {
        response_json["success"] = false;
        response_json["message"] = "新密码长度至少6个字符";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 加密旧密码和新密码
    std::string encrypted_old_password = sha256(old_password);
    std::string encrypted_new_password = sha256(new_password);
    
    // 连接数据库
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql == nullptr)
    {
        response_json["success"] = false;
        response_json["message"] = "数据库连接失败";
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 验证旧密码
    std::string check_sql = "SELECT id FROM users WHERE username='" + username + 
                           "' AND password='" + encrypted_old_password + "'";
    
    if (!mysql_util::mysql_exec(mysql, check_sql))
    {
        response_json["success"] = false;
        response_json["message"] = "密码验证失败";
        mysql_util::mysql_destroy(mysql);
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        if (!row)
        {
            // 旧密码错误
            response_json["success"] = false;
            response_json["message"] = "旧密码错误";
            mysql_free_result(result);
            mysql_util::mysql_destroy(mysql);
            
            std::string response_text = Json::writeString(writer, response_json);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
        mysql_free_result(result);
    }
    
    // 更新密码
    std::string update_sql = "UPDATE users SET password='" + encrypted_new_password + 
                            "' WHERE username='" + username + "'";
    
    if (mysql_util::mysql_exec(mysql, update_sql))
    {
        response_json["success"] = true;
        response_json["message"] = "密码修改成功";
        LOG(LogLevel::INFO) << "用户 " << username << " 修改密码";
    }
    else
    {
        response_json["success"] = false;
        response_json["message"] = "密码修改失败";
    }
    
    mysql_util::mysql_destroy(mysql);
    
    std::string response_text = Json::writeString(writer, response_json);
    resp.SetCode(200);
    resp.SetHeader("Content-Type", "application/json; charset=utf-8");
    resp.SetHeader("Content-Length", std::to_string(response_text.size()));
    resp.SetText(response_text);
}

// libcurl回调函数,用于接收响应数据
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string GetEnvOrDefault(const char* key, const std::string& default_value)
{
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return default_value;
    }
    return value;
}

static long GetEnvLongOrDefault(const char* key, long default_value)
{
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0')
    {
        return default_value;
    }
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end == value || parsed <= 0)
    {
        return default_value;
    }
    return parsed;
}

static const std::string& LangChainApiUrl()
{
    static std::string url = GetEnvOrDefault("LANGCHAIN_API_URL", "http://127.0.0.1:8000/llm/chat");
    return url;
}

static const std::string& LangChainApiKey()
{
    static std::string key = GetEnvOrDefault("LANGCHAIN_API_KEY", "");
    return key;
}

static long LangChainConnectTimeoutMs()
{
    static long timeout_ms = GetEnvLongOrDefault("LANGCHAIN_CONNECT_TIMEOUT_MS", 5000);
    return timeout_ms;
}

static long LangChainTimeoutMs()
{
    static long timeout_ms = GetEnvLongOrDefault("LANGCHAIN_TIMEOUT_MS", 60000);
    return timeout_ms;
}

static std::string GetContentTypeMime(const std::string& content_type)
{
    size_t sep = content_type.find(';');
    std::string mime = sep == std::string::npos ? content_type : content_type.substr(0, sep);
    std::transform(mime.begin(), mime.end(), mime.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    while (!mime.empty() && (mime.back() == ' ' || mime.back() == '\t'))
    {
        mime.pop_back();
    }
    return mime;
}

static bool IsJsonContentType(const std::string& content_type)
{
    return GetContentTypeMime(content_type) == "application/json";
}

void AIChat(HttpRequest &req, HttpResponse &resp)
{
    LOG(LogLevel::DEBUG) << "AI聊天请求: " << req.Args();
    
    Json::StreamWriterBuilder writer;
    Json::Value response_json;
    std::string request_id = SessionUtil::GenerateSessionId();
    resp.SetHeader("X-Request-ID", request_id);
    
    // 1. 验证登录状态（从Cookie获取用户名）
    std::string cookie_header = req.GetHeader("Cookie");
    std::string username = SessionUtil::GetUsernameFromCookie(cookie_header);
    
    if (username.empty()) {
        // 未登录，返回错误
        response_json["success"] = false;
        response_json["error"] = "请先登录";
        response_json["reply"] = "请先登录后使用AI聊天功能";
        response_json["trace_id"] = request_id;
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        LOG(LogLevel::WARNING) << "未登录用户尝试使用AI聊天";
        return;
    }
    
    // 2. 获取或创建该用户的当前活跃会话
    std::string session_id;
    MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    
    if (mysql != nullptr) {
        // 查询该用户最近使用的会话
        std::string query_sql = "SELECT session_id FROM sessions WHERE username='" + 
                               username + "' ORDER BY updated_at DESC LIMIT 1";
        
        if (mysql_util::mysql_exec(mysql, query_sql)) {
            MYSQL_RES* result = mysql_store_result(mysql);
            if (result != nullptr) {
                MYSQL_ROW row = mysql_fetch_row(result);
                if (row && row[0]) {
                    session_id = row[0];
                    LOG(LogLevel::INFO) << "用户 " << username << " 使用现有会话: " << session_id;
                }
                mysql_free_result(result);
            }
        }
        
        // 如果该用户没有会话，创建新会话
        if (session_id.empty()) {
            session_id = SessionUtil::GenerateSessionId();
            std::string create_session_sql = "INSERT INTO sessions (session_id, username, session_name, created_at) VALUES ('" + 
                                            session_id + "', '" + username + "', '新对话', NOW())";
            mysql_util::mysql_exec(mysql, create_session_sql);
            LOG(LogLevel::INFO) << "为用户 " << username << " 创建新会话: " << session_id;
        }
        
        mysql_util::mysql_destroy(mysql);
    }
    
    if (session_id.empty()) {
        // 无法创建会话
        response_json["success"] = false;
        response_json["error"] = "会话创建失败";
        response_json["reply"] = "抱歉，系统错误，请稍后重试";
        response_json["trace_id"] = request_id;
        
        std::string response_text = Json::writeString(writer, response_json);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }
    
    // 解析用户消息、模型选择和附件。旧版表单请求继续兼容，新版附件请求使用JSON。
    std::string args = req.Args();
    std::string user_message;
    std::string selected_model = "deepseek/deepseek-v4-pro"; // 默认模型
    Json::Value attachments(Json::arrayValue);
    std::string content_type = req.GetHeader("Content-Type");
    bool is_json_request = IsJsonContentType(content_type);
    
    if (is_json_request)
    {
        Json::Value request_json;
        Json::CharReaderBuilder reader;
        std::string parse_errs;
        std::istringstream request_stream(args);
        if (!Json::parseFromStream(reader, request_stream, &request_json, &parse_errs))
        {
            Json::Value error_resp;
            error_resp["success"] = false;
            error_resp["error"] = "请求JSON解析失败";
            error_resp["reply"] = "请求格式不正确，请重新发送";
            error_resp["trace_id"] = request_id;
            std::string response_text = Json::writeString(writer, error_resp);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
        if (request_json.isMember("message") && request_json["message"].isString())
        {
            user_message = request_json["message"].asString();
        }
        if (request_json.isMember("model") && request_json["model"].isString())
        {
            selected_model = request_json["model"].asString();
        }
        if (request_json.isMember("attachments") && request_json["attachments"].isArray())
        {
            attachments = request_json["attachments"];
        }
    }
    else
    {
        // 解析参数 (格式: message=xxx&model=yyy)
        // 提取message字段
        size_t msg_pos = args.find("message=");
        if (msg_pos != std::string::npos)
        {
            size_t msg_start = msg_pos + 8; // 8 = strlen("message=")
            size_t msg_end = args.find("&", msg_start);
            
            if (msg_end != std::string::npos)
            {
                user_message = args.substr(msg_start, msg_end - msg_start);
            }
            else
            {
                user_message = args.substr(msg_start);
            }
            
            // URL解码 (处理中文等特殊字符)
            user_message = Util::UrlDecode(user_message);
        }
        
        // 提取model字段
        size_t model_pos = args.find("model=");
        if (model_pos != std::string::npos)
        {
            size_t model_start = model_pos + 6; // 6 = strlen("model=")
            size_t model_end = args.find("&", model_start);
            
            if (model_end != std::string::npos)
            {
                selected_model = args.substr(model_start, model_end - model_start);
            }
            else
            {
                selected_model = args.substr(model_start);
            }
            
            // URL解码
            selected_model = Util::UrlDecode(selected_model);
        }
    }
    
    LOG(LogLevel::DEBUG) << "选择的模型: " << selected_model;
    LOG(LogLevel::INFO) << "附件数量: " << attachments.size();

    std::string saved_user_message = user_message;
    if (!attachments.empty())
    {
        saved_user_message += saved_user_message.empty() ? "" : "\n\n";
        saved_user_message += "[上传附件]";
        for (Json::ArrayIndex i = 0; i < attachments.size(); ++i)
        {
            std::string name = "attachment";
            if (attachments[i].isMember("name") && attachments[i]["name"].isString())
            {
                name = attachments[i]["name"].asString();
            }
            saved_user_message += "\n- " + name;
        }
    }
    
    if (user_message.empty() && attachments.empty())
    {
        Json::Value error_resp;
        error_resp["success"] = false;
        error_resp["error"] = "没有收到消息内容";
        error_resp["reply"] = "请先输入消息内容或上传附件";
        error_resp["trace_id"] = request_id;
        Json::StreamWriterBuilder writer;
        std::string response_text = Json::writeString(writer, error_resp);
        
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
        return;
    }

    if (!attachments.empty())
    {
        const Json::ArrayIndex max_attachments = 4;
        const Json::UInt64 max_total_data_chars = 10 * 1024 * 1024;
        Json::UInt64 total_data_chars = 0;
        if (attachments.size() > max_attachments)
        {
            Json::Value error_resp;
            error_resp["success"] = false;
            error_resp["error"] = "附件数量过多";
            error_resp["reply"] = "一次最多上传4个附件";
            error_resp["trace_id"] = request_id;
            std::string response_text = Json::writeString(writer, error_resp);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
        for (Json::ArrayIndex i = 0; i < attachments.size(); ++i)
        {
            if (!attachments[i].isMember("data_url") || !attachments[i]["data_url"].isString())
            {
                continue;
            }
            total_data_chars += attachments[i]["data_url"].asString().size();
        }
        if (total_data_chars > max_total_data_chars)
        {
            Json::Value error_resp;
            error_resp["success"] = false;
            error_resp["error"] = "附件过大";
            error_resp["reply"] = "附件总大小过大，请压缩后再上传";
            error_resp["trace_id"] = request_id;
            std::string response_text = Json::writeString(writer, error_resp);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
    }
    
    const std::string& langchain_url = LangChainApiUrl();
    const std::string& langchain_key = LangChainApiKey();
    
    // 构建请求JSON - 交给 LangChain 服务编排与调用
    Json::Value body;
    body["request_id"] = request_id;
    body["session_id"] = session_id;
    body["username"] = username;
    body["model"] = selected_model;
    body["attachments"] = attachments;
    
    // 构建messages数组
    Json::Value messages(Json::arrayValue);
    
    // 1. 添加系统消息
    Json::Value system_msg;
    system_msg["role"] = "system";
    system_msg["content"] = "你是一个友好、有帮助的AI助手。";
    messages.append(system_msg);
    
    // 2. 从数据库获取该会话的最近历史记录（限制条数避免token过多）
    MYSQL* mysql_history = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
    if (mysql_history != nullptr) {
        std::string history_sql = "SELECT user_message, ai_response FROM chat_history "
                                 "WHERE session_id='" + session_id + "' "
                                 "ORDER BY created_at DESC LIMIT 10"; // 只取最近10轮对话
        
        if (mysql_util::mysql_exec(mysql_history, history_sql)) {
            MYSQL_RES* history_result = mysql_store_result(mysql_history);
            if (history_result != nullptr) {
                // 将结果存入vector（因为查询是倒序的，需要反转）
                std::vector<std::pair<std::string, std::string>> history_pairs;
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(history_result))) {
                    if (row[0] && row[1]) {
                        history_pairs.push_back({row[0], row[1]});
                    }
                }
                mysql_free_result(history_result);
                
                // 反转后添加到messages（保持时间顺序）
                for (int i = history_pairs.size() - 1; i >= 0; i--) {
                    Json::Value user_msg;
                    user_msg["role"] = "user";
                    user_msg["content"] = history_pairs[i].first;
                    messages.append(user_msg);
                    
                    Json::Value assistant_msg;
                    assistant_msg["role"] = "assistant";
                    assistant_msg["content"] = history_pairs[i].second;
                    messages.append(assistant_msg);
                }
                
                LOG(LogLevel::INFO) << "加载了 " << history_pairs.size() << " 轮历史对话";
            }
        }
        mysql_util::mysql_destroy(mysql_history);
    }
    
    // 3. 添加当前用户消息
    Json::Value current_user_msg;
    current_user_msg["role"] = "user";
    current_user_msg["content"] = user_message;
    messages.append(current_user_msg);
    
    body["messages"] = messages;
    
    // 使用已经声明的writer
    writer["indentation"] = "";
    std::string json_data = Json::writeString(writer, body);
    
    LOG(LogLevel::DEBUG) << "发送到LangChain的请求体: " << json_data;
    
    // 使用libcurl发送HTTP请求
    CURL* curl = curl_easy_init();
    std::string response_data;
    
    if (curl)
    {
        auto start_time = std::chrono::steady_clock::now();
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
        std::string request_header = "X-Request-ID: " + request_id;
        headers = curl_slist_append(headers, request_header.c_str());
        std::string session_header = "X-Session-ID: " + session_id;
        headers = curl_slist_append(headers, session_header.c_str());
        if (!langchain_key.empty())
        {
            std::string auth_header = "Authorization: Bearer " + langchain_key;
            headers = curl_slist_append(headers, auth_header.c_str());
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, langchain_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_data.size());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, LangChainTimeoutMs());
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, LangChainConnectTimeoutMs());
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        LOG(LogLevel::INFO) << "正在调用LangChain服务: " << langchain_url
                            << ", 模型: " << selected_model
                            << ", request_id: " << request_id;
        
        CURLcode res = curl_easy_perform(curl);
        
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        auto end_time = std::chrono::steady_clock::now();
        auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        LOG(LogLevel::INFO) << "LangChain响应: HTTP " << http_code
                            << ", CURL=" << res
                            << ", latency_ms=" << latency_ms
                            << ", request_id=" << request_id;
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK)
        {
            std::string curl_error = curl_easy_strerror(res);
            LOG(LogLevel::ERROR) << "LangChain调用失败 [" << res << "]: " << curl_error;
            
            Json::Value error_resp;
            error_resp["success"] = false;
            error_resp["error"] = "LangChain服务不可用: " + curl_error;
            error_resp["reply"] = "❌ AI服务暂时不可用，请稍后重试";
            error_resp["curl_code"] = res;
            error_resp["trace_id"] = request_id;
            
            std::string response_text = Json::writeString(writer, error_resp);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
        
        // 检查HTTP状态码
        if (http_code != 200)
        {
            LOG(LogLevel::ERROR) << "LangChain返回错误状态码: " << http_code;
            Json::Value error_resp;
            error_resp["success"] = false;
            error_resp["error"] = "LangChain服务错误: " + std::to_string(http_code);
            error_resp["reply"] = "抱歉，AI服务暂时不可用";
            error_resp["trace_id"] = request_id;
            error_resp["upstream_status"] = http_code;
            
            std::string response_text = Json::writeString(writer, error_resp);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
            return;
        }
        
        // 解析AI响应
        Json::Value result;
        Json::CharReaderBuilder reader;
        std::string errs;
        std::istringstream ss(response_data);
        
        if (Json::parseFromStream(reader, ss, &result, &errs))
        {
            // 构建返回给前端的JSON
            Json::Value response_json;
            bool upstream_success = true;
            std::string upstream_error;
            std::string upstream_reply;
            std::string upstream_trace_id;
            std::string model_used = selected_model;
            
            if (result.isMember("success"))
            {
                upstream_success = result["success"].asBool();
            }
            if (result.isMember("error") && result["error"].isString())
            {
                upstream_error = result["error"].asString();
            }
            if (result.isMember("reply") && result["reply"].isString())
            {
                upstream_reply = result["reply"].asString();
            }
            if (result.isMember("trace_id") && result["trace_id"].isString())
            {
                upstream_trace_id = result["trace_id"].asString();
            }
            if (result.isMember("model") && result["model"].isString())
            {
                model_used = result["model"].asString();
            }
            
            if (!upstream_success || upstream_reply.empty())
            {
                response_json["success"] = false;
                response_json["error"] = upstream_error.empty() ? "LangChain服务返回空响应" : upstream_error;
                response_json["reply"] = upstream_error.empty()
                    ? "抱歉，AI服务暂时不可用"
                    : "抱歉，当前模型不可用: " + upstream_error;
                response_json["trace_id"] = request_id;
                if (!upstream_trace_id.empty())
                {
                    response_json["upstream_trace_id"] = upstream_trace_id;
                }
                
                LOG(LogLevel::WARNING) << "LangChain返回错误, request_id=" << request_id
                                       << ", upstream_trace_id=" << upstream_trace_id
                                       << ", error=" << upstream_error;
            }
            else
            {
                std::string ai_reply = upstream_reply;
                response_json["reply"] = ai_reply;
                response_json["success"] = true;
                response_json["trace_id"] = request_id;
                response_json["model"] = model_used;
                
                // 保存聊天记录到数据库
                MYSQL* mysql = mysql_util::mysql_create(HOST, USER, PASS, DBNAME, PORT);
                if (mysql != nullptr)
                {
                    // 转义特殊字符防止SQL注入
                    char* escaped_user_msg = new char[saved_user_message.length() * 2 + 1];
                    char* escaped_ai_reply = new char[ai_reply.length() * 2 + 1];
                     mysql_real_escape_string(mysql, escaped_user_msg, saved_user_message.c_str(), saved_user_message.length());
                     mysql_real_escape_string(mysql, escaped_ai_reply, ai_reply.c_str(), ai_reply.length());
                     
                     std::string insert_sql = "INSERT INTO chat_history (session_id, user_message, ai_response, model) VALUES ('" + 
                                             session_id + "', '" +
                                             std::string(escaped_user_msg) + "', '" + 
                                             std::string(escaped_ai_reply) + "', '" + 
                                             model_used + "')";
                     
                     delete[] escaped_user_msg;
                     delete[] escaped_ai_reply;
                     
                     if (mysql_util::mysql_exec(mysql, insert_sql))
                    {
                        LOG(LogLevel::INFO) << "聊天记录已保存到会话: " << session_id;
                        
                        // 更新session的updated_at和message_count
                        std::string update_session_sql = "UPDATE sessions SET updated_at=NOW(), message_count=message_count+1 WHERE session_id='" + session_id + "'";
                        mysql_util::mysql_exec(mysql, update_session_sql);
                    }
                    else
                    {
                        LOG(LogLevel::WARNING) << "聊天记录保存失败";
                    }
                    mysql_util::mysql_destroy(mysql);
                }
            }
            
            std::string response_text = Json::writeString(writer, response_json);
            
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
        }
        else
        {
            LOG(LogLevel::ERROR) << "JSON解析错误: " << errs;
            Json::Value error_resp;
            error_resp["success"] = false;
            error_resp["error"] = "JSON解析失败";
            error_resp["reply"] = "抱歉，AI服务暂时不可用";
            error_resp["trace_id"] = request_id;
            
            std::string response_text = Json::writeString(writer, error_resp);
            resp.SetCode(200);
            resp.SetHeader("Content-Type", "application/json; charset=utf-8");
            resp.SetHeader("Content-Length", std::to_string(response_text.size()));
            resp.SetText(response_text);
        }
    }
    else
    {
        LOG(LogLevel::ERROR) << "无法初始化CURL";
        Json::Value error_resp;
        error_resp["success"] = false;
        error_resp["error"] = "系统错误";
        error_resp["reply"] = "抱歉，AI服务暂时不可用";
        error_resp["trace_id"] = request_id;
        
        std::string response_text = Json::writeString(writer, error_resp);
        resp.SetCode(200);
        resp.SetHeader("Content-Type", "application/json; charset=utf-8");
        resp.SetHeader("Content-Length", std::to_string(response_text.size()));
        resp.SetText(response_text);
    }
}

void Usage(std::string proc)
{
    std::cerr << "Usage: " << proc << " port" << std::endl;
}

// http port
int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        Usage(argv[0]);
        exit(USAGE_ERR);
    }
    // std::cout << "服务器已经启动，已经是一个守护进程了" << std::endl;
    // 加载 .env 文件 (必须在 daemon 之前，因为 daemon 会切换工作目录到 /)
    LoadEnvFile(".env");
    // 守护进程化 (会改变当前工作目录)
    daemon(1, 0);
    Enable_File_Log_Strategy("./logs", "myhttp_ai.log");
    
    uint16_t port = std::stoi(argv[1]);

    
    std::unique_ptr<Http> httpsvr = std::make_unique<Http>(port);
    httpsvr->RegisterService("/login",Login);
    httpsvr->RegisterService("/logout", Logout); // 登出
    httpsvr->RegisterService("/register", Register);
    httpsvr->RegisterService("/get_current_user", GetCurrentUser); // 获取当前登录用户
    httpsvr->RegisterService("/vip_check", VipCheck);
    httpsvr->RegisterService("/s", Search);
    httpsvr->RegisterService("/ai_chat", AIChat); // 注册AI聊天服务
    httpsvr->RegisterService("/chat_history", ChatHistory); // 注册聊天历史查询服务
    httpsvr->RegisterService("/get_sessions", GetSessions); // 获取会话列表
    httpsvr->RegisterService("/create_session", CreateSession); // 创建新会话
    httpsvr->RegisterService("/switch_session", SwitchSession); // 切换会话
    httpsvr->RegisterService("/rename_session", RenameSession); // 重命名会话
    httpsvr->RegisterService("/delete_session", DeleteSession); // 删除会话
    httpsvr->RegisterService("/update_email", UpdateEmail); // 修改邮箱
    httpsvr->RegisterService("/update_password", UpdatePassword); // 修改密码
    httpsvr->RegisterService("/upload_avatar", UploadAvatar); // 上传头像
    
    LOG(LogLevel::INFO) << "已注册" << 17 << "个服务路由";
    
    httpsvr->Start();
    return 0;
}
