#include "AppendFile.hpp"
#include<assert.h>
#include <cstring>
namespace wangt{
    size_t AppendFile::write(const char *info, const size_t len)
    {
        return fwrite_unlocked(info, 1, len, fp_);
    }

AppendFile::AppendFile(const string &filename):fp_{nullptr},buffer_(new char[FILEBUFFSIZE]),wittenBytes_(0){
   // cout<<filename<<endl;
    fp_ = fopen(filename.c_str(), "a");
    assert(fp_!=nullptr);
    setbuffer(fp_,buffer_.get(),FILEBUFFSIZE);
}

AppendFile::~AppendFile()
{
    fclose(fp_);
}
void AppendFile::append(const string &info)
{
    append(info.c_str(),info.size());
}

void AppendFile::append(const char *info, const size_t len)
{
    //cout<<"111"<<endl;
    size_t n = write(info, len); // //n是已经写了的字节数
    size_t remain = len - n; ////相减大于0表示未写完
    while (remain > 0)
    {
        size_t x = write(info + n, remain);
        if (x == 0)
        {
            int err = ferror(fp_);
            if (err)
            {
                // fprintf(stderr,"AppendFile::append() failed %s\n",strerror_t1(err));
                fprintf(stderr, "AppendFile::append() failed %s\n", strerror(err));
            }
            break;
        }
        n += x;
        remain = len - n;
    }
    wittenBytes_ += len;
}
void AppendFile::flush()
{
    fflush(fp_);
}
size_t AppendFile::wittenBytes() const
{
    return wittenBytes_;
}
}