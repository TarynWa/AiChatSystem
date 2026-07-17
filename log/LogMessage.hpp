#ifndef LOG_MESSAGE_HPP
#define LOG_MESSAGE_HPP
#include <string>
#include "TimeStamp.hpp"
#include "LogLevel.hpp"
#include <iostream>
#include <sstream>
class LogMessage
{
    std::string head_;
    std::string text_;
    std::stringstream ss_;
public:
   LogMessage(Timestamp ts, int level, const char* msg, int line, const char* func){
     head_ = "[" + ts.toFormattedString(true) + "] [" + levelStr[level] + "] [" + func + ":" + std::to_string(line) + "] ";
    ss_ << msg << " ";
   }
  ~LogMessage(){ 
  }
    template<typename T>
    LogMessage& operator<<(const T& value) {
        ss_ << value;
        return *this;
    }
    const std::string getmsg(){
        text_ += ss_.str();
        ss_.str(""); // 清空stringstream
        ss_.clear(); // 重置状态
        std::string msg = head_+text_;
        return msg;
    }
    LogMessage& operator=(const LogMessage& other) {
        if (this != &other) {
            head_ = other.head_;
            text_ = other.text_;
            ss_.str(other.ss_.str());
            ss_.clear(other.ss_.rdstate());
        }
        return *this;
    }
};
#endif