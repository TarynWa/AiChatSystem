#ifndef TIMESTAMP_HPP
#define TIMESTAMP_HPP
#include<stdint.h>
#include<iostream>
#include<string>
#include<sys/time.h>
using namespace std;
namespace wangt{
    class Timestamp{
        private:
        uint64_t micr_;
        public:
        Timestamp():micr_(0){}
        Timestamp(uint64_t ms):micr_(ms){}
        ~Timestamp(){}
        void swap(Timestamp &other);
        string toString()const;
        //"123.32z"
        string toFormattedString(bool showmicto=true)const;
        //"2025/10/11 15:29:23"
        //"2025/10/11 15:29:23.32z"
        string toFormattedFile()const;
        bool valid()const;//确保微秒有效
        uint64_t getmicro()const;
        uint64_t getsecond()const;
        const Timestamp&now();
        operator uint64_t()const;
        public:
        static Timestamp Now();//现在时间
        static Timestamp Invalid();
        static const uint32_t KMicPerSec=1000*1000;
    };
}
#endif