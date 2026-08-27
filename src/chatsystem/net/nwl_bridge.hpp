#ifndef NWL_BRIDGE_HPP
#define NWL_BRIDGE_HPP
// 自研网络库（nwl）→ 聊天系统 类型桥接：回调签名与 muduo 同构（plan.md §5.5）
// 以全局别名最小侵入平替 muduo::net，业务层代码零改动；
// muduo/base/ThreadPool 与本桥接无关，仍用于异步 DB 工作线程池，不受影响
#include <Logger.hpp>           // WT_LOG_* 日志桥接（lib::muduo_log）
#include "nwl/TcpServer.hpp"    // TcpServer/TcpConnection/Buffer/InetAddress/Callbacks
#include "nwl/EventLoop.hpp"    // 提供 EventLoop 完整类型（loop/runInLoop 等需成员可见）

using TcpConnectionPtr = nwl::TcpConnPtr;
using nwl::Buffer;
using nwl::EventLoop;
using nwl::InetAddress;
using nwl::Timestamp;
using nwl::TcpServer;

#endif // NWL_BRIDGE_HPP
