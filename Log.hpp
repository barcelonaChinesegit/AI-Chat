#ifndef __LOG_HPP__
#define __LOG_HPP__

#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <unistd.h>
#include <ctime>
#include "Mutex.hpp"

namespace LogModule
{
    using namespace MutexModule;

    const std::string gsep = "\r\n";

    
    // 策略模式(利用的C++多态特性)
    //1.刷新策略 a:显示器打印 b:向指定文件写入
    // 刷新策略基类
    class LogStrategy
    {
    public:
        ~LogStrategy() = default;

        virtual void SyncLog(const std::string& message) = 0; // =0表示纯虚函数
    };
    
    // (1).显示器打印日志的策略(子类)
    class ConsoleLogStrategy : public LogStrategy
    {
    public:
        ConsoleLogStrategy()
        {}
        
        ~ConsoleLogStrategy()
        {}
        
        void SyncLog(const std::string& message) override
        {
            LockGuard lockguard(_mutex);
            std::cout << message << gsep;
        }

    private:
        Mutex _mutex;
    };
    
    // (2).文件打印日志的策略(子类)
    const std::string defaultpath = "./logs";
    const std::string defaultfile = "myhttp_ai.log";
    class FileLogStrategy : public LogStrategy
    {
        public:
        FileLogStrategy(const std::string& path = defaultpath, const std::string& file = defaultfile)
        :_path(path)
        ,_file(file)
        {
            LockGuard lockguard(_mutex);
            if (std::filesystem::exists(_path)) // 判断目录是否存在
            {
                return;
            }
            
            //防止由权限不够等情况，创建路径失败
            try
            {
                std::filesystem::create_directories(_path); //创建路径
            }
            catch(const std::filesystem::filesystem_error& e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        
        ~FileLogStrategy()
        {}
        
        void SyncLog(const std::string& message) override
        {
            LockGuard lockguard(_mutex);
            std::string filename = _path + ((_path.back()=='/')?"":"/") + _file;
            std::ofstream out(filename,std::ios::app); // 追加写入方式打开
            if(!out.is_open())
            {
                return;
            }
            out << message<<gsep;
            out.close();
        }
        
    private:
        std::string _path; //日志文件所在路径
        std::string _file; //日志文件本身
        
        Mutex _mutex;
    };
    
    //形成一条完整的日志&&根据上面策略,选择不同的刷新方式
    
    //1.形成日志等级
    enum class LogLevel
    {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        FATAL
    };
    
    std::string Level2Str(LogLevel level)
    {
        switch(level)
        {
            case LogLevel::DEBUG:
                return "DEBUG";
            case LogLevel::INFO:
                return "INFO";
            case LogLevel::WARNING:
                return "WARNING";
            case LogLevel::ERROR:
                return "ERROR";
            case LogLevel::FATAL:
                return "FATAL";
            default:
                return "UNKNOWN";
        }
    }
    
    std::string GetTimeStamp()
    {
        time_t curr = time(nullptr);
        struct tm curr_tm;
        localtime_r(&curr, &curr_tm);
        // 法一:stringstream
        std::stringstream ss;
        ss << std::setfill('0')
           << std::setw(4) << curr_tm.tm_year + 1900 << "-"
           << std::setw(2) << curr_tm.tm_mon + 1 << "-"
           << std::setw(2) << curr_tm.tm_mday << " "
           << std::setw(2) << curr_tm.tm_hour << ":"
           << std::setw(2) << curr_tm.tm_min << ":"
           << std::setw(2) << curr_tm.tm_sec;

        // ss << curr_tm.tm_year + 1900 << "-" << curr_tm.tm_mon + 1 << "-" << curr_tm.tm_mday << " "
        //    << curr_tm.tm_hour << ":" << curr_tm.tm_min << ":" << curr_tm.tm_sec;
        return ss.str();
        
        //法二:
        // char timebuffer[128];
        // snprintf(timebuffer, sizeof(timebuffer),"%4d-%02d-%02d %02d:%02d:%02d",
        //     curr_tm.tm_year+1900,
        //     curr_tm.tm_mon+1,
        //     curr_tm.tm_mday,
        //     curr_tm.tm_hour,
        //     curr_tm.tm_min,
        //     curr_tm.tm_sec
        // );
        // return timebuffer;
    }

    //class Logger作用:1.形成日志 2.根据不同的策略,完成刷新
    class Logger
    {
    public:
        Logger()
        {
            EnableConsoleLogStrategy();
        }

        ~Logger()
        {}
        
        void EnableFileLogStrategy(const std::string& path = defaultpath, const std::string& file = defaultfile)
        {
            _fflush_strategy = std::make_unique<FileLogStrategy>(path, file);
        }

        void EnableConsoleLogStrategy()
        {
            _fflush_strategy = std::make_unique<ConsoleLogStrategy>();
            
        }
        
        //内部类:表示的是未来的一条日志
        class LogMessage
        {
        public:
            LogMessage(LogLevel& level, std::string& src_name,int line_number,Logger& logger)
                :_curr_time(GetTimeStamp())
                ,_leve(level)
                ,_pid(getpid())
                ,_src_name(src_name)
                ,_line_number(line_number)
                ,_logger(logger)
            {
                //先把日志的左半部分合并起来
                std::stringstream ss;
                ss << "[" << _curr_time << "] "
                   << "[" << Level2Str(_leve) << "] "
                   << "[" << _pid << "] "
                   << "[" << _src_name << "] "
                   << "[" << _line_number << "] "
                   << "- ";
                _loginfo = ss.str();
            }
            
            // LogMessage()<<"hello word"<<"XXXX"<<3.14<<12345
            template<typename T>
            LogMessage& operator <<(const T& info)
            {
                //再把日志的右半部分合并起来
                std::stringstream ss;
                ss << info;
                //左半部分和右半部分合并起来
                _loginfo += ss.str();
                return *this;
            }

            ~LogMessage()
            {
                if(_logger._fflush_strategy)
                {
                    _logger._fflush_strategy->SyncLog(_loginfo);
                }
            }
        private:
            std::string _curr_time;
            LogLevel _leve;
            pid_t _pid;
            std::string _src_name;
            int _line_number;
            std::string _loginfo; //合并之后,一条完整的信息
            Logger &_logger;
        };

    //这里故意写成返回临时对象
    LogMessage operator()(LogLevel level, std::string name,int line)
    {
        return LogMessage(level, name, line, *this);
    }

    private:
        std::unique_ptr<LogStrategy> _fflush_strategy;
    };
    
    //全局日志对象
    Logger logger;
    
    //使用宏,简化用户操作,获取文件名和行号
    #define LOG(level) logger(level, __FILE__, __LINE__)
    #define Enable_Console_Log_Strategy() logger.EnableConsoleLogStrategy()
    #define Enable_File_Log_Strategy(...) logger.EnableFileLogStrategy(__VA_ARGS__) //可变参数宏
}

#endif
