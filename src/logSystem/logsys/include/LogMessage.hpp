#ifndef LOGMESSAGE_HPP
#define LOGMESSAGE_HPP
#include"LogCommon.hpp"
#include<sstream>
namespace wangt{
    class LogMessage{
        string header_;
        string text_;
        wangt::LOG_LEVEL level_;
        public:
        LogMessage(const wangt::LOG_LEVEL&le,const string &filename,const string &funcname,const int line);
        ~LogMessage();
        template<class T>
        LogMessage&operator<<(const T&text){
            stringstream ss;
            ss<<text;
            text_+=ss.str();
            return *this;
        }
        const wangt::LOG_LEVEL & getlevel()const;
        const string toString()const;
    };
}
#endif