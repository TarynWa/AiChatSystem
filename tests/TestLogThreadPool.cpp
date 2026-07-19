#include <iostream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include "Logger.hpp"
#include "FixedThreadPool.hpp"

int main() {
    namespace fs = std::filesystem;
    fs::create_directories("logmsg");
    std::ofstream logfile("logmsg/test_log.txt", std::ios::out | std::ios::app);
    auto outputFn = [&logfile](const std::string &msg) {
        if (logfile.is_open()) {
            logfile << msg;
        } else {
            std::cout << msg;
        }
    };
    auto flushFn = [&logfile]() {
        if (logfile.is_open()) {
            logfile.flush();
        } else {
            std::cout.flush();
        }
    };

    Logger::setOutput(outputFn);
    Logger::setFlush(flushFn);
    Logger::setLogLevel(INFO);

    LOG_INFO << "Starting log + threadpool test";

    FixedThreadPool pool(2, 10);
    std::atomic<int> counter{0};
    const int taskCount = 5;

    for (int i = 0; i < taskCount; ++i) {
        pool.addTask([i, &counter]() {
            LOG_INFO << "Task " << i << " running";
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pool.stop();

    if (counter.load(std::memory_order_relaxed) != taskCount) {
        std::cerr << "Task execution failed, counter = " << counter << std::endl;
        return 1;
    }

    LOG_INFO << "All tasks completed, counter = " << counter;
    return 0;
}
