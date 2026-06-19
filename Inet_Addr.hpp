#pragma once
#include "Common.hpp"

// 网络地址和主机地址之间进行转化的类

class InetAddr
{
public:
    InetAddr(){}
    // 网络转主机
    InetAddr(struct sockaddr_in& addr)
    {
        SetAddr(addr);
    }

    // 主机转网络
    InetAddr(const std::string& ip ,uint16_t port)
        :_ip(ip)
        ,_port(port)
    {
        memset(&_addr, 0, sizeof(_addr));
        _addr.sin_family = AF_INET;
        
        //法一(线程不安全)
        //_addr.sin_addr.s_addr = inet_addr(_ip.c_str());
        //法二(线程安全)
        inet_pton(AF_INET, _ip.c_str(), &_addr.sin_addr);
        _addr.sin_port = htons(_port);
    }
    
    InetAddr(uint16_t port)
        :_ip("0")
        ,_port(port)
    {
        memset(&_addr, 0, sizeof(_addr));
        _addr.sin_family = AF_INET;
        _addr.sin_addr.s_addr = INADDR_ANY;
        _addr.sin_port = htons(_port);
    }

    void SetAddr(struct sockaddr_in& addr)
    {
        _addr = addr; //浅拷贝不会有影响
        _port = ntohs(_addr.sin_port); // 从网络中拿到的!网络序列
        // 4字节网络风格的IP -> 点分十进制的字符串风格的IP
        //法一(线程不安全)
        // _ip = inet_ntoa(_addr.sin_addr); 
        
        //法二(线程安全)
        char ipbuffer[64];
        inet_ntop(AF_INET,&_addr.sin_addr,ipbuffer,sizeof(ipbuffer));
        _ip = ipbuffer;
    }
    
    uint16_t Port() const { return _port; }
    std::string Ip() const { return _ip; }
    // NetAddr需要引用,是因为Route.hpp的MessageRoute函数中
    // sendto(sockfd, send_message.c_str(), send_message.size(), 0, (const struct sockaddr *)&user.NetAddr(), sizeof(user.NetAddr()));
    // 的第五个参数需要可以修改,不能传右值(临时变量)
    const struct sockaddr_in& NetAddr() { return _addr; }
    const struct sockaddr* NetAddrPtr() { return CONV(_addr); }
    socklen_t NetAddrLen()
    {
        return sizeof(_addr);
    }
    bool operator==(const InetAddr& addr)
    {
        return _ip == addr._ip && _port == addr._port;
    }

    std::string StringAddr()
    {
        return _ip + ":" + std::to_string(_port);
    }
    
    ~InetAddr()
    {}
private:
    struct sockaddr_in _addr;
    std::string _ip;
    uint16_t _port;
};