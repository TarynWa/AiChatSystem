#include <iostream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include "AsyncLogging.hpp"
#include "FixedThreadPool.hpp"

wangt::AsyncLogging *asynclog = nullptr;
void asyncWriteFile(const string &info)
{
    asynclog->append(info);
}
void asyncFlushFile()
{
    asynclog->flush();
}
// int main() {

// wangt::Logger::setOutput(asyncWriteFile);
// wangt::Logger::setFlush(asyncFlushFile);
// wangt::Logger::setLogLevel(wangt::LOG_LEVEL::INFO);

//     WT_LOG_INFO << "Starting log + threadpool test";

//     FixedThreadPool pool(2, 10);
//     std::atomic<int> counter{0};
//     const int taskCount = 5;

//     for (int i = 0; i < taskCount; ++i) {
//         pool.addTask([i, &counter]() {
//            WT_LOG_INFO << "Task " << i << " running";
//             counter.fetch_add(1, std::memory_order_relaxed);
//         });
//     }

//     std::this_thread::sleep_for(std::chrono::milliseconds(200));
//     pool.stop();

//     if (counter.load(std::memory_order_relaxed) != taskCount) {
//         WT_LOG_INFO << "Task execution failed, counter = " << counter;
//         return 1;
//     }

//     WT_LOG_INFO << "All tasks completed, counter = " << counter;
//     return 0;
// }
#include "chatserver.hpp"
int main(int argc, char *argv[])
 {
    asynclog = new wangt::AsyncLogging("/home/wangt/ThreadPoolAction/logmsg/ser/server", 1024 * 10);
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

