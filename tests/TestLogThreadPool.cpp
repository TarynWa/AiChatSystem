#include <iostream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include"AsyncLogging.hpp"
#include "chatserver.hpp"
namespace fs = std::filesystem;
wangt::AsyncLogging *asynclog = nullptr;
void asyncWriteFile(const string &info)
{
    asynclog->append(info);
}
void asyncFlushFile()
{
    asynclog->flush();
}
int main(int argc, char *argv[])
 {
    string path = "/home/wangt/ThreadPoolAction/logmsg/ser";
    if (!fs::exists(path))
    {
        fs::create_directories(path);
    }
    asynclog = new wangt::AsyncLogging("/home/wangt/ThreadPoolAction/logmsg/ser/server", 1024 * 10);
    asynclog->start();
    wangt::Logger::setOutput(asyncWriteFile);
    wangt::Logger::setFlush(asyncFlushFile);
    if (argc < 3)
    {
        WT_LOG_ERROR << "Usage: " << argv[0] << " <ip> <port>";
        return 1;
    }
    EventLoop loop;
    InetAddress addr(argv[1], atoi(argv[2]));
    ChatServer server(&loop, addr, "ChatServer");
    server.start();
    loop.loop();
    return 0;
}

