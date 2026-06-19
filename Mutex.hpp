#pragma once
#include <pthread.h>
#include <iostream>
#include <cstring>

namespace MutexModule
{
    class Mutex
    {
    public:
    Mutex()
    {
        pthread_mutex_init(&_mutex, nullptr);
    }

    ~Mutex()
    {
        pthread_mutex_destroy(&_mutex);
    }

    void Lock()
    {
        int n = pthread_mutex_lock(&_mutex);
        if(n != 0)
        {
            std::cerr << "lock error:" << strerror(n) << std::endl;
        }
    }

    void Unlock()
    {
        int n = pthread_mutex_unlock(&_mutex);
        if(n != 0)
        {
            std::cerr << "unlock error:" << strerror(n) << std::endl;
        }
    }
    
    pthread_mutex_t* get()
    {
        return &_mutex;
    }

    private:
        pthread_mutex_t _mutex;
    };
    
    class LockGuard
    {
    public:
        LockGuard(Mutex& mutex)
            :_mutex(mutex)
        {
            _mutex.Lock();
        }

        ~LockGuard()
        {
            _mutex.Unlock();
        }

    private:
        Mutex& _mutex;
    };
}