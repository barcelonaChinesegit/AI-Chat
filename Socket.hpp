#pragma once
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include "Log.hpp"
#include "Common.hpp"
#include "Inet_Addr.hpp"

namespace SocketModule
{
    using namespace LogModule;
    const static int gbacklog = 16;

    // 模板方法模式(固定套路代码常用)
    // 基类socket,大部分方法,都是纯虚方法
    class Socket
    {
    public:
        virtual ~Socket() {}
        virtual void SocketOrDie() = 0;
        virtual void BindOrDie(uint16_t port) = 0;
        virtual void ListenOrDie(int blacklog) = 0;
        virtual std::shared_ptr<Socket> Accept(InetAddr* client) = 0;
        virtual void Close() = 0;
        virtual int Recv(std::string *out) = 0;
        virtual int Send(const std::string& message) = 0;
        virtual int Connect(const std::string &server_ip, uint16_t server_port) = 0;

    public:
        void BuildTcpSocketMethod(uint16_t port, int blacklog = gbacklog)
        {
            SocketOrDie();
            BindOrDie(port);
            ListenOrDie(blacklog);
        }
        
        void BuildTcpClientSocketMethod()
        {
            SocketOrDie();
        }
    };

    const static int defaultfd = -1;
    class TcpSocket : public Socket
    {
    public:
        TcpSocket():_sockfd(defaultfd)
        {}
        TcpSocket(int fd):_sockfd(fd)
        {}
        ~TcpSocket() {}
        void SocketOrDie() override
        {
            _sockfd = ::socket(AF_INET, SOCK_STREAM, 0); // ::表示默认使用更外部(全局)的socket函数
            if(_sockfd < 0)
            {
                LOG(LogLevel::FATAL) << "socket error";
                exit(SOCKET_ERR);
            }
            int opt = 1;
            ::setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            LOG(LogLevel::INFO) << "socket success";
        }
        
        void BindOrDie(uint16_t port) override
        {
            const char* bind_host_env = std::getenv("HTTP_BIND_HOST");
            std::string bind_host = bind_host_env && bind_host_env[0] ? bind_host_env : "127.0.0.1";
            InetAddr localaddr = (bind_host == "0.0.0.0" || bind_host == "*")
                ? InetAddr(port)
                : InetAddr(bind_host, port);
            int n = ::bind(_sockfd, localaddr.NetAddrPtr(), localaddr.NetAddrLen());
            if(n < 0)
            {
                LOG(LogLevel::FATAL) << "bind error: " << bind_host << ":" << port
                                     << " errno=" << errno << " " << std::strerror(errno);
                exit(BIND_ERR);
            }
            LOG(LogLevel::INFO) << "bind success: " << bind_host << ":" << port;
        }

        void ListenOrDie(int blacklog) override
        {
            int n = ::listen(_sockfd, blacklog);
            if(n < 0)
            {
                LOG(LogLevel::FATAL) << "listen error";
                exit(LISTEN_ERR);
            }
            LOG(LogLevel::INFO) << "listen success";
        }

        std::shared_ptr<Socket> Accept(InetAddr* client) override
        {
            struct sockaddr_in peer;
            socklen_t len = sizeof(peer);
            int fd = ::accept(_sockfd, CONV(peer), &len);
            if(fd < 0)
            {
                LOG(LogLevel::WARNING) << "accept warning ...";
                return nullptr; //TODO
            }
            client->SetAddr(peer);
            return std::make_shared<TcpSocket>(fd);
        }

        int Recv(std::string *out) override //返回值等同read的返回值
        {
            // 流式读取,并不关心读到的是什么
            char buffer[4096*4];
            ssize_t n = ::recv(_sockfd,&buffer,sizeof(buffer)-1, 0);
            if(n > 0)
            {
                out->append(buffer, n);
            }
            return n;
        }

        int Send(const std::string& message) override
        {
            return ::send(_sockfd, message.c_str(), message.size(), 0);
        }

        void Close() override
        {
            if(_sockfd > 0)
                ::close(_sockfd);
        }

        int Connect(const std::string &server_ip, uint16_t server_port) override
        {
            InetAddr server(server_ip, server_port);
            return ::connect(_sockfd, server.NetAddrPtr(), server.NetAddrLen());
        }

    private:
        int _sockfd; // _sockfd,listensockfd,sockfd
    };
}
