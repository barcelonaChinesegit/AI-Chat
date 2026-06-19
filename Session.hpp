#pragma once
#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

// Session管理工具类
class SessionUtil
{
public:
    // 生成UUID格式的session_id
    static std::string GenerateSessionId()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        std::uniform_int_distribution<> dis2(8, 11);
        
        std::stringstream ss;
        ss << std::hex;
        
        for (int i = 0; i < 8; i++) {
            ss << dis(gen);
        }
        ss << "-";
        
        for (int i = 0; i < 4; i++) {
            ss << dis(gen);
        }
        ss << "-4";  // UUID version 4
        
        for (int i = 0; i < 3; i++) {
            ss << dis(gen);
        }
        ss << "-";
        
        ss << dis2(gen);
        for (int i = 0; i < 3; i++) {
            ss << dis(gen);
        }
        ss << "-";
        
        for (int i = 0; i < 12; i++) {
            ss << dis(gen);
        }
        
        return ss.str();
    }
    
    // 从Cookie中提取登录用户名
    static std::string GetUsernameFromCookie(const std::string& cookie_header)
    {
        // Cookie格式: username=xxx; other=yyy
        size_t pos = cookie_header.find("username=");
        if (pos == std::string::npos) {
            return "";
        }
        
        size_t start = pos + 9; // strlen("username=")
        size_t end = cookie_header.find(";", start);
        
        if (end == std::string::npos) {
            return cookie_header.substr(start);
        } else {
            return cookie_header.substr(start, end - start);
        }
    }
    
    // 生成登录Cookie（存储用户名）
    static std::string GenerateLoginCookie(const std::string& username, int max_age = 86400 * 7)
    {
        // 7天过期
        std::stringstream ss;
        ss << "username=" << username 
           << "; Max-Age=" << max_age
           << "; Path=/"
           << "; HttpOnly"
           << "; SameSite=Lax";
        return ss.str();
    }
    
    // 生成登出Cookie（清除用户名）
    static std::string GenerateLogoutCookie()
    {
        std::stringstream ss;
        ss << "username=; Max-Age=0; Path=/";
        return ss.str();
    }
    
    // 验证session_id格式
    static bool IsValidSessionId(const std::string& session_id)
    {
        if (session_id.empty() || session_id.length() < 10) {
            return false;
        }
        // 简单验证，可以根据需要加强
        return true;
    }
    
    // === 以下是旧的session管理函数，保留用于兼容性 ===
    // 注意：新设计中应该使用username而不是session_id
    
    // 从Cookie中提取session_id（旧版本，保留兼容）
    static std::string GetSessionIdFromCookie(const std::string& cookie_header)
    {
        // Cookie格式: session_id=xxx; other=yyy
        size_t pos = cookie_header.find("session_id=");
        if (pos == std::string::npos) {
            return "";
        }
        
        size_t start = pos + 11; // strlen("session_id=")
        size_t end = cookie_header.find(";", start);
        
        if (end == std::string::npos) {
            return cookie_header.substr(start);
        } else {
            return cookie_header.substr(start, end - start);
        }
    }
    
    // 生成Set-Cookie响应头（旧版本，保留兼容）
    static std::string GenerateSetCookieHeader(const std::string& session_id, int max_age = 86400 * 30)
    {
        // 30天过期
        std::stringstream ss;
        ss << "session_id=" << session_id 
           << "; Max-Age=" << max_age
           << "; Path=/"
           << "; HttpOnly"
           << "; SameSite=Lax";
        return ss.str();
    }
};

