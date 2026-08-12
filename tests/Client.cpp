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
    string path = "/home/wangt/ThreadPoolAction/logmsg/cli";
    if (!fs::exists(path))
    {
        fs::create_directories(path);
    }
    asynclog = new wangt::AsyncLogging("/home/wangt/ThreadPoolAction/logmsg/cli/client", 1024 * 10);
    asynclog->start();
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
    // login.set_type(chat::LOGIN_MSG);
    // chat::LoginRequest loginRequest;
    // loginRequest.set_username("testuser");
    // loginRequest.set_password("testpassword");
    // loginRequest.set_id(1);
    // login.set_payload(loginRequest.SerializeAsString());
    login.set_type(chat::REG_MSG);
    chat::RegisterRequest registerRequest;
    registerRequest.set_username("testuser");
    registerRequest.set_password("testpassword");
    registerRequest.set_id(1);
    login.set_payload(registerRequest.SerializeAsString());
    string buf;
    if (!login.SerializeToString(&buf))
    {
        WT_LOG_ERROR << "Failed to serialize login message";
        close(cilsockfd);
        return -1;
    }

    WT_LOG_INFO << "Login message serialized, size: " << buf.size();
    uint32_t bodyLen = htonl(static_cast<uint32_t>(buf.size()));
        // 发送数据
        int n = send(cilsockfd, buf.data(), buf.size(), MSG_NOSIGNAL);
        if (n == -1)
        {
            WT_LOG_ERROR << "send login message failed: " << strerror(errno);
            close(cilsockfd);
            return -1;
        }
        WT_LOG_INFO << "Sent " << n << " bytes";
        
        // n = recv(cilsockfd, buf.data(), buf.size(), 0);
        // if (n == -1)
        // {
        //     WT_LOG_ERROR << "recv failed: " << strerror(errno);
        //     close(cilsockfd);
        //     return -1;
        // }
        // WT_LOG_INFO << "Received " << n << " bytes";
        // std::this_thread::sleep_for(std::chrono::seconds(1));
    while(1);
}