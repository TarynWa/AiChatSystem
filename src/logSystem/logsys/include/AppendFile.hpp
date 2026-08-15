#ifndef APPENDFILE_HPP
#define APPENDFILE_HPP
#include"Logger.hpp"
#include<memory>
namespace wangt{
    class AppendFile{
        static const int FILEBUFFSIZE=64*1024;
        FILE *fp_;
        unique_ptr<char[]>buffer_;
        size_t wittenBytes_;
        size_t write(const char*info,const size_t len);
        public:
        AppendFile(const string&filename);
        ~AppendFile();
        void append(const string& info);
        void append(const char*info,const size_t len);
        void flush();
        size_t wittenBytes()const; 
    };
}
#endif