#include <iostream>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <ctime>
#include <arpa/inet.h>
#include "AsyncLogging.hpp"
#include <cstring>
#include "chat.pb.h"

wangt::AsyncLogging *asynclog = new wangt::AsyncLogging("/home/wangt/ThreadPoolAction/logmsg/cli/client", 1024 * 10);
void asyncWriteFile(const string &info)
{
    asynclog->append(info);
}
void asyncFlushFile()
{
    asynclog->flush();
}

static bool sendAll(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

int main(int argc, char *argv[])
{
    wangt::Logger::setOutput(asyncWriteFile);
    wangt::Logger::setFlush(asyncFlushFile);
    if (argc < 3)
    {
        WT_LOG_ERROR << "Usage: " << argv[0] << " <ip> <port>";
        return 1;
    }
    string ip = argv[1];
    int port = atoi(argv[2]);
    int cilsockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cilsockfd == -1)
    {
        WT_LOG_ERROR << "create socket failed";
        return -1;
    }
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
    if (connect(cilsockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1)
    {
        WT_LOG_ERROR << "connect server failed";
        return -1;
    }

    chat::BaseMessage login;
    login.set_type(chat::LOGIN_MSG);
    login.set_payload("hello");
    string buf;
    if (!login.SerializeToString(&buf))
    {
        WT_LOG_ERROR << "Failed to serialize login message";
        close(cilsockfd);
        return -1;
    }

    WT_LOG_INFO << "Login message serialized, size: " << buf.size();
    uint32_t bodyLen = htonl(static_cast<uint32_t>(buf.size()));
    sendAll(cilsockfd, reinterpret_cast<const char*>(&bodyLen), sizeof(bodyLen));
    sendAll(cilsockfd, buf.data(), buf.size());
        // while(n--){
        // // 发送数据
        // int n = send(cilsockfd, buf.data(), buf.size(), MSG_NOSIGNAL);
        // if (n == -1)
        // {
        //     WT_LOG_ERROR << "send login message failed: " << strerror(errno);
        //     close(cilsockfd);
        //     return -1;
        // }
        // WT_LOG_INFO << "Sent " << n << " bytes";
        
        // n = recv(cilsockfd, buf.data(), buf.size(), 0);
        // if (n == -1)
        // {
        //     WT_LOG_ERROR << "recv failed: " << strerror(errno);
        //     close(cilsockfd);
        //     return -1;
        // }
        // WT_LOG_INFO << "Received " << n << " bytes";
        // std::this_thread::sleep_for(std::chrono::seconds(1));
        // }
    while(1);
}