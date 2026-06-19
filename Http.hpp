#pragma once
#include "TcpServer.hpp"
#include "Util.hpp"
#include <sstream>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cctype>

const std::string gspace = " ";
const std::string glinespace = "\r\n";
const std::string glinesep = ": ";

const std::string webroot = "./wwwroot";
const std::string homepage = "index.html";
const std::string page_404 = "/404.html";

class HttpRequest
{
public:
    HttpRequest()
        :_is_interact(false)
        ,_has_header(false)
        ,_has_body(false)
    {}
    
    // 服务端浏览器写好了
    std::string Serialize()
    {
        return std::string();
    }

    // 获取请求行
    void ParseReqLine(std::string& reqline)
    {
        // GET / HTTP/1.1
        std::stringstream ss(reqline);
        ss >> _method >> _uri >> _version;
    }
    
    // 获取请求报头与正文
    bool ParseReqHeadersAndBody(std::string& reqline)
    {
        std::string line;
        int content_len = 0;

        // 读取并解析 Header，直到空行
        while (true)
        {
            bool ret = Util::ReadOneLine(reqline, &line, glinespace);
            if (!ret)
            {
                LOG(LogLevel::DEBUG) << "请求报头为空";
                return true;
            }
            if (line.empty()) break; // 空行:头结束(因为ReadOneLine已去掉\r\n)

            auto sep = line.find(glinesep);
            if (sep != std::string::npos)
            {
                std::string key = line.substr(0, sep);
                std::string value = line.substr(sep + glinesep.size());
                _headers[key] = value;
                if(!_has_header) _has_header = true;
                if (key == "Content-Length" || key == "content-length")
                {
                    content_len = std::stoi(value);
                }
            }
        }

        // 按 Content-Length 读取正文到 _text
        _text.clear();
        if (content_len > 0)
        {
            if ((int)reqline.size() >= content_len)
            {
                _has_body = true;
                _text = reqline.substr(0, content_len);
                reqline.erase(0, content_len);
            }
            else
            {
                LOG(LogLevel::FATAL) << "报文异常";
                return false;
            }
        }
        
        return true;
    }
    
    // 实现(我们今天认为,reqstr是一个完整的http,没有写decode)
    bool Deserialize(std::string& reqstr)
    {
        // 1.提取请求中的请求行
        std::string reqline;
        bool res = Util::ReadOneLine(reqstr, &reqline,glinespace);
        LOG(LogLevel::DEBUG) << reqline;
        
        // 2.对请求行进行反序列化
        // 获得请求行
        ParseReqLine(reqline);
        if(_uri=="/")
            _uri = webroot + _uri + homepage;
        else
            _uri = webroot + _uri;
        // 获得请求报头与正文
        ParseReqHeadersAndBody(reqstr);
        
        /*日志打印请求信息*/
        LOG(LogLevel::DEBUG) << "_method: " << _method;
        LOG(LogLevel::DEBUG) << "_uri: " << _uri;
        LOG(LogLevel::DEBUG) << "_version: " << _version;
        if(_has_header)
        {
            for(const auto &header : _headers)
            {
                LOG(LogLevel::DEBUG) << "_header: " << header.first << glinesep << header.second;
            }
        }
        if(_has_body)
            LOG(LogLevel::DEBUG) << "_text: " << _text;
        /*日志打印请求信息*/
        
        LOG(LogLevel::DEBUG) << "开始处理请求类型判断, _method=" << _method;
            
        // (1).POST特殊处理:
        if (_method == "POST" || _method == "post")
        {
            // 处理URL中的查询参数 (例如: /ai_chat?message=xxx)
            const std::string temp = "?";
            auto pos = _uri.find(temp);
            if(pos != std::string::npos)
            {
                // 分离URI和参数
                _args = _uri.substr(pos + temp.size());
                _uri = _uri.substr(0, pos);
            }
            else
            {
                // 如果URL中没有参数，则从请求正文获取
                _args = _text;
            }
            _is_interact = true;
            LOG(LogLevel::DEBUG) << "POST请求设置为交互式, _is_interact=" << _is_interact;
            return true;
        }

        // (2).GET特殊处理:
        // 注:可能有这种_uri: ./wwwroot/login?username=zhangsan&password=123456
        if (_method == "GET" || _method == "get")
        {
            const std::string temp = "?";
            auto pos = _uri.find(temp);
            if(pos == std::string::npos)
            {
                // 检查是否是静态资源(有文件扩展名)
                auto dot_pos = _uri.rfind(".");
                auto slash_pos = _uri.rfind("/");
                // 如果有扩展名且在最后一个/之后，认为是静态资源
                if (dot_pos != std::string::npos && (slash_pos == std::string::npos || dot_pos > slash_pos))
                {
                    return true; // 静态资源
                }
                else
                {
                    // 没有扩展名，可能是API接口
                    _is_interact = true;
                    _args = ""; // 没有参数
                    return true;
                }
            }
            // _uri解析:
            // _args: username=zhangsan&password=123456
            // _uri: ./wwwroot/login
            _args = _uri.substr(pos + temp.size());
            _uri = _uri.substr(0, pos);
            _is_interact = true;
            return true;
        }
        
        // 其他请求方法（PUT、DELETE、HEAD等）
        
        return true;
    }

    std::string Uri() { return _uri; }

    bool isInteract() { return _is_interact; }

    std::string Args() { return _args; }
    
    // 获取请求头
    std::string GetHeader(const std::string& key)
    {
        auto iter = _headers.find(key);
        if (iter != _headers.end()) {
            return iter->second;
        }
        return "";
    }

    // 检查是否支持长连接
    bool KeepAlive()
    {
        auto iter = _headers.find("Connection");
        if(iter != _headers.end())
        {
            // Connection字段:conn_val
            std::string conn_val = iter->second;
            // 转换为小写进行比较
            for(char& c : conn_val)
            {
                c = std::tolower(c);
            }
            if(conn_val == "keep-alive")
                return true;
        }
        // 检查 HTTP 版本，HTTP/1.1 默认支持长连接
        if(_version == "HTTP/1.1" || _version == "http/1.1")
        {
            // HTTP/1.1 如果没有明确指定 Connection: close，则默认支持 keep-alive
            auto iter2 = _headers.find("Connection");
            if(iter2 != _headers.end())
            {
                std::string conn_val = iter2->second;
                for(char& c : conn_val)
                {
                    c = std::tolower(c);
                }
                if(conn_val == "close")
                    return false;
            }
            return true;
        }
        return false;
    }

    ~HttpRequest()
    {}
private:
    std::string _method;
    std::string _uri;
    std::string _version;
    std::unordered_map<std::string, std::string> _headers; //请求报头
    std::string _blankline; //空行
    std::string _text; //正文

    bool _has_header;
    bool _has_body;

    std::string _args; //uri后面跟的参数
    bool _is_interact; //是否需要交互
};

class HttpResponse
{
public:
    HttpResponse()
        :_blankline(glinespace)
        ,_version("HTTP/1.0")
        ,_keep_alive(false)
    {}
    
    // 实现:成熟的http,应答做序列化,不需要依赖任何第三方库!
    std::string Serialize()
    {
        std::string status_line = _version + gspace + std::to_string(_code) + gspace + _desc + glinespace;
        std::string resp_header;
        for(auto& header : _headers)
        {
            std::string line = header.first + glinesep + header.second + glinespace;
            resp_header += line;
        }
        return status_line + resp_header + _blankline + _text;
    }

    // 服务端浏览器写好了
    bool Deserialize()
    {
        return true;
    }
    
    void SetTargetFile(const std::string& target)
    {
        _targetfile = target;
    }
    
    void SetCode(int code)
    {
        _code = code;
        switch(_code)
        {
            case 200:
                _desc = "OK";
                break;
            case 404:
                _desc = "Not Found";
                break;
            case 301:
                _desc = "Moved Permanently";
                break;
            case 302:
                _desc = "See Other";
                break;
            default:
                break;
        }
    }
    
    void SetHeader(const std::string& key, const std::string& value)
    {
        auto iter = _headers.find(key);
        if(iter != _headers.end())
            return;
        _headers.emplace(key, value);
    }

    void SetText(const std::string & t)
    {
        _text = t;
    }

    // 设置是否保持连接
    void SetKeepAlive(bool keep_alive)
    {
        _keep_alive = keep_alive;
        if(_keep_alive)
        {
            SetHeader("Connection", "keep-alive");
            // HTTP/1.1 版本以支持长连接
            _version = "HTTP/1.1";
        }
        else
        {
            SetHeader("Connection", "close");
        }
    }

    std::string Uri2Suffix(const std::string& targetfile)
    {
        // targetfile: ./wwwroot/a/b/c.html
        auto pos = targetfile.rfind(".");
        if(pos == std::string::npos)
        {
            return "text/html"; //应该报错的,简写默认是网页了
        }
        std::string suffix = targetfile.substr(pos);
        if(suffix == ".html" || suffix == ".htm")
            return "text/html";
        else if (suffix == ".jpg")
            return "image/jpeg";
        else if (suffix == ".png")
            return "image/png";
        else if (suffix == ".mp4")
            return "video/mpeg4";
        else
            return "text/html";//应该填完Content-Type整张表的,简写默认是网页了
    }

    bool MakeResponse()
    {
        if(_targetfile == "./wwwroot/favicon.ico")
        {
            LOG(LogLevel::DEBUG) << "用户请求: " << _targetfile << "忽略它";
            return false;
        }
        
        // 临时重定向
        if(_targetfile == "./wwwroot/redir_test")
        {
            SetCode(302);
            SetHeader("Location", "https://www.qq.com/");
            return true;
        }

        int filesize = 0;
        bool res = Util::ReadFileContent(_targetfile, &_text); //ReadFileContent给_targetfile加好了./wwwroot
        if(!res)
        {
            // 法一:
            _text = "";
            LOG(LogLevel::WARNING) << "client want get : " << _targetfile << " but not found";
            SetCode(404);
            _targetfile = webroot + page_404;
            Util::ReadFileContent(_targetfile, &_text);
            std::string suffix = Uri2Suffix(_targetfile);
            SetHeader("Content-Type", suffix);
            
            // 法二:
            // SetCode(302);
            // SetHeader("Location", "http://115.190.2.155:8080/404.html"); //注意:这里没有域名,端口写死的,要注意!!!
            // return true;
        }
        else
        {
            LOG(LogLevel::DEBUG) << "读取文件: " << _targetfile;
            SetCode(200);
            std::string suffix = Uri2Suffix(_targetfile);
            SetHeader("Content-Type", suffix);
        }
        filesize = Util::FileSize(_targetfile);
        SetHeader("Content-Length", std::to_string(filesize));
        return true;
    }

    ~HttpResponse(){}
// private:
public:
    std::string _version;
    int _code; //404
    std::string _desc; //"Not Found"
    std::unordered_map<std::string, std::string> _headers; //请求报头
    std::string _blankline; //空行
    std::string _text; //正文
    // 其他属性
    std::string _targetfile; //要获取资源的地址
    bool _keep_alive; //是否保持连接
};

// Http要做到:
// 1.返回静态资源
// 2.提供动态交互的能力

using http_func_t = std::function<void(HttpRequest &req, HttpResponse &resp)>;

class Http
{
public:
    Http(uint16_t port)
        :tsvrp(std::make_unique<TcpServer>(port))
    {
        
    }
    
    // 从缓冲区中提取一个完整的 HTTP 请求
    // 返回值: true 表示提取到完整请求, false 表示数据不完整
    bool ExtractOneRequest(std::string& buffer, std::string& request)
    {
        // 先检查是否至少有一个完整的请求行
        size_t first_line_end = buffer.find(glinespace);
        if(first_line_end == std::string::npos)
        {
            // 还没有收到完整的请求行
            return false;
        }

        // 查找请求头结束标志 \r\n\r\n
        std::string header_end = glinespace + glinespace; // "\r\n\r\n"
        size_t header_end_pos = buffer.find(header_end);
        if(header_end_pos == std::string::npos)
        {
            // 还没有收到完整的请求头
            return false;
        }

        // 提取请求头部分（用于解析 Content-Length）
        std::string header_part = buffer.substr(0, header_end_pos);
        
        // 检查是否有 Content-Length
        int content_len = 0;
        size_t content_pos = header_part.find("Content-Length:");
        if(content_pos == std::string::npos)
        {
            content_pos = header_part.find("content-length:");
        }
        
        if(content_pos != std::string::npos && content_pos < header_end_pos)
        {
            // 找到 Content-Length 头，解析其值
            size_t len_start = header_part.find(":", content_pos) + 1;
            // 跳过空格
            while(len_start < header_part.size() && (header_part[len_start] == ' ' || header_part[len_start] == '\t'))
                len_start++;
            size_t len_end = header_part.find(glinespace, len_start);
            if(len_end == std::string::npos)
                len_end = header_part.size();
            
            if(len_start < len_end)
            {
                std::string len_str = header_part.substr(len_start, len_end - len_start);
                try {
                    content_len = std::stoi(len_str);
                } catch(...) {
                    content_len = 0;
                }
            }
        }

        // 计算完整请求的结束位置
        // header_end_pos 是 \r\n 的位置，header_end.size() 是 \r\n 的长度
        // 所以请求头结束后的位置是 header_end_pos + header_end.size()
        size_t header_end_offset = header_end_pos + header_end.size();
        size_t request_end = header_end_offset + content_len;
        
        if(buffer.size() < request_end)
        {
            // 数据不完整，还需要继续接收
            return false;
        }

        // 提取完整的请求
        request = buffer.substr(0, request_end);
        buffer.erase(0, request_end);
        return true;
    }

    void HandlerHttpRequest(std::shared_ptr<Socket> &sock, InetAddr &client)
    {   
        // 接收缓冲区，用于处理粘包
        std::string recv_buffer;
        bool should_close = false;

        // 循环处理多个请求（支持长连接）
        while(!should_close)
        {
            // 尝试从缓冲区提取完整请求
            std::string httpreqstr;
            bool has_complete_request = ExtractOneRequest(recv_buffer, httpreqstr);

            // 如果没有完整请求，尝试接收更多数据
            if(!has_complete_request)
            {
                std::string new_data;
                int n = sock->Recv(&new_data);
                
                if(n <= 0)
                {
                    // 连接已关闭或出错
                    should_close = true;
                    break;
                }
                
                recv_buffer += new_data;
                
                // 再次尝试提取完整请求
                has_complete_request = ExtractOneRequest(recv_buffer, httpreqstr);
            }

            if(!has_complete_request)
            {
                // 如果还是没有完整请求，继续接收
                continue;
            }

            std::cout << std::endl << "##########################" << std::endl;
            std::cout << httpreqstr;
            std::cout << "##########################" << std::endl << std::endl;

            // 对字符串请求反序列化
            HttpRequest req;
            if(!req.Deserialize(httpreqstr))
            {
                LOG(LogLevel::WARNING) << "请求解析失败，跳过该请求，继续处理下一个";
                // 跳过这个有问题的请求，继续处理缓冲区中的下一个请求
                continue;
            }

            // 构建http应答
            HttpResponse resp;
            
            // 根据请求决定是否保持连接
            bool keep_alive = req.KeepAlive();
            resp.SetKeepAlive(keep_alive);

            if(req.isInteract())
            {
                // 1.交互
                // _args: username=zhangsan&password=123456
                // _uri: ./wwwroot/login
                
                LOG(LogLevel::DEBUG) << "交互式请求 URI: " << req.Uri();
                
                if(_route.find(req.Uri()) == _route.end())
                {
                    // (1).无对应方法
                    LOG(LogLevel::WARNING) << "未找到路由: " << req.Uri();
                    resp.SetTargetFile(webroot + page_404);
                    if (resp.MakeResponse())
                    {
                        std::string response_str = resp.Serialize();
                        sock->Send(response_str);
                    }
                }
                else
                {
                    // (2).有对应方法
                    LOG(LogLevel::DEBUG) << "匹配到路由: " << req.Uri();
                    _route[req.Uri()](req, resp);
                    std::string response_str = resp.Serialize();
                    sock->Send(response_str);
                }
            }
            else
            {
                // 2.静态
                resp.SetTargetFile(req.Uri());
                if (resp.MakeResponse())
                {
                    // 所以我们就不在担心,用户访问一个服务器上不存在的资源了(html,css,js,图片,视频这种资源--静态资源!)
                    std::string response_str = resp.Serialize();
                    sock->Send(response_str);
                }
            }

            // 如果不保持连接，处理完这个请求后关闭
            if(!keep_alive)
            {
                should_close = true;
            }
        }
    }
    
    void Start()
    {
        tsvrp->Start([this](std::shared_ptr<Socket> &sock, InetAddr &client){
            this->HandlerHttpRequest(sock, client);
        });
    }

    void RegisterService(const std::string name, http_func_t h)
    {
        std::string key = webroot + name;
        auto iter = _route.find(key);
        if(iter == _route.end())
        {
            _route.emplace(key, h);
        }
    }

    ~Http()
    {
        
    }
private:
    std::unique_ptr<TcpServer> tsvrp;
    std::unordered_map<std::string, http_func_t> _route;
};
