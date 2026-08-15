#ifndef LOGCOMMON_HPP
#define LOGCOMMON_HPP
#include"Timestamp.hpp"
namespace wangt
{
    enum class LOG_LEVEL
    {
        TRACE = 0,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL,
        SYSERR,
        SYSFATAL,
        NUM_LOG_LEVELS
    };
    // ostream&operator<<(ostream&out,wangt::LOG_LEVEL& l){
    //     if(l==LOG_LEVEL::TRACE)out<<"TRACE"<<endl;
    //     if(l==LOG_LEVEL::DEBUG)out<<"DEBUG"<<endl;
    //     if(l==LOG_LEVEL::INFO)out<<"INFO"<<endl;
    //     if(l==LOG_LEVEL::WARN)out<<"WARN"<<endl;
    //     if(l==LOG_LEVEL::ERROR)out<<"ERROR"<<endl;
    //     if(l==LOG_LEVEL::FATAL)out<<"FATAL"<<endl;
    //     if(l==LOG_LEVEL::SYSERR)out<<"SYSERR"<<endl;
    //     if(l==LOG_LEVEL::SYSFATAL)out<<"SYSFATAL"<<endl;
    //     if(l==LOG_LEVEL::NUM_LOG_LEVELS)out<<"NUM_LOG_LEVELS"<<endl;
    //     return out;
    // }
    static  string GetIndexTostr(LOG_LEVEL &le){
        string str="NOT TYPE";
        switch (le)
        {
        case LOG_LEVEL::TRACE:
            str="TRACE";
            break;
        case LOG_LEVEL::DEBUG:
            str="DEBUG";
            break;
        case LOG_LEVEL::INFO:
            str="INFO";
            break;
        case LOG_LEVEL::WARN:
            str="WARN";
            break;
        case LOG_LEVEL::ERROR:
            str="ERROR";
            break;
        case LOG_LEVEL::FATAL:
            str="FATAL";
            break;
        case LOG_LEVEL::NUM_LOG_LEVELS:
            str="NUM_LOG_LEVELS";
            break;
        case LOG_LEVEL::SYSERR:
            str="SYSERR";
            break;
        case LOG_LEVEL::SYSFATAL:
            str="SYSFATAL";
            break;
        default:
            break;
        }
        return str;
    }
}
#endif