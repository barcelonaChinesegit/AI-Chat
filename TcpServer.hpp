#include "Socket.hpp"
#include "Log.hpp"
#include <iostream>
#include <memory>
#include <sys/wait.h>
#include <sys/types.h>
#include <functional>

using namespace SocketModule;
using namespace LogModule;

using ioservice_t = std::function<void(std::shared_ptr<Socket> &sock, InetAddr &client)>;

class TcpServer
{
public:
    TcpServer(uint16_t port) 
        :_port(port)
        ,_listensockptr(std::make_unique<TcpSocket>())
        ,_isrunning(false)
    {
        _listensockptr->BuildTcpSocketMethod(_port);
    }
    
    void Start(ioservice_t callback)
    {
        _isrunning = true;
        while(_isrunning)
        {
            InetAddr client;
            auto sock = _listensockptr->Accept(&client); // 获得1.和client通信的sockfd 2.client网络地址
            if(sock == nullptr)
            {
                continue;                
            }
            LOG(LogLevel::DEBUG) << "accept success ..." << client.StringAddr();

            // 获得了:1.与客户端通信socket;2.客户端地址和端口号
            pid_t id = fork();
            if(id < 0)
            {
                LOG(LogLevel::FATAL) << "fork error ...";
                exit(FORK_ERROR);
            }
            else if(id == 0)
            {
                //子进程 ->关闭listen socket
                _listensockptr->Close();
                if(fork() > 0)
                    exit(0);
                //孙子进程在执行任务,已经是孤儿进程了
                callback(sock,client);
                sock->Close();
                exit(OK);
            }
            else
            {
                //父进程 ->关闭clinet socket(即:auto sock)
                sock->Close();
                pid_t rid = ::waitpid(-1, nullptr, 0);
                (void)rid;
            }
        }
        _isrunning = false;
    }
    
    ~TcpServer() {}
private:
    uint16_t _port;
    std::unique_ptr<Socket> _listensockptr;
    bool _isrunning;
};