#pragma once

#include <iostream>
#include <fstream>
#include <string>

// 工具类
class Util
{
public:
    Util() {}
    ~Util() {}
    
    static bool ReadFileContent(const std::string &filename, std::string* out/*实际std::vector<char>常用*/)
    {
        // version 1:默认以文本方式读取文件的.图片是二进制的不能用这种方式读.
        // std::ifstream in(filename, std::ios::out | std::ios::app);
        // if (!in.is_open())
        // {
        //     return false;
        // }
        // std::string line;
        // while (std::getline(in,line))
        // {
        //     *out += line;
        // }
        // in.close();
        // return true;
        
        // version 2:以二进制方式进行读取
        int filesize = FileSize(filename);
        if(filesize > 0)
        {
            std::ifstream in(filename, std::ios::binary);
            if(!in.is_open())
                return false;
            out->resize(filesize);
            in.read(&(*out)[0], filesize); //或in.read((char *)out->c_str(), filesize);
            in.close();
            return true;
        }
        else
        {
            return false;
        }
    }

    static bool ReadOneLine(std::string &bigstr, std::string *out, const std::string &sep/*\r\n*/)
    {
        auto pos = bigstr.find(sep);
        if(pos == std::string::npos)
            return false;
        *out = bigstr.substr(0, pos);
        bigstr.erase(0, pos+sep.size());
        return true;
    }

    static int FileSize(const std::string& filename)
    {
        std::ifstream in(filename,std::ios::binary);
        if(!in.is_open())
            return -1;
        in.seekg(0, in.end);
        int filesize = in.tellg();
        in.seekg(0, in.beg);
        in.close();
        return filesize;
    }

    // URL解码函数
    static std::string UrlDecode(const std::string& str)
    {
        std::string result;
        for (size_t i = 0; i < str.length(); ++i)
        {
            if (str[i] == '+')
            {
                result += ' ';
            }
            else if (str[i] == '%' && i + 2 < str.length())
            {
                // 将十六进制转换为字符
                int value = 0;
                std::string hex = str.substr(i + 1, 2);
                for (char c : hex)
                {
                    value *= 16;
                    if (c >= '0' && c <= '9')
                        value += c - '0';
                    else if (c >= 'A' && c <= 'F')
                        value += c - 'A' + 10;
                    else if (c >= 'a' && c <= 'f')
                        value += c - 'a' + 10;
                }
                result += static_cast<char>(value);
                i += 2;
            }
            else
            {
                result += str[i];
            }
        }
        return result;
    }

private:
        
};