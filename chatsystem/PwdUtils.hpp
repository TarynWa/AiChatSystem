#ifndef PWDUTILS_H
#define PWDUTILS_H
#include <string>
using namespace std;
#include "AsyncLogging.hpp"

// 密码加密工具类
// 提供基于SHA256+随机盐的密码哈希与校验
// 全部为静态方法，无需实例化
// 设计目标：禁止明文密码入库，避免彩虹表攻击
class PwdUtils
{
public:
    // 生成16字节随机盐值，返回32字符十六进制字符串
    static string generateSalt();
    // 计算输入字符串的SHA256哈希，返回64字符十六进制字符串
    static string sha256(const string &input);
    // 密码校验：使用相同盐值对明文密码哈希，与已存储哈希比较
    // plainPassword: 明文密码
    // salt: 注册时生成的盐值
    // hashedPassword: 数据库中存储的哈希值
    static bool verify(const string &plainPassword, const string &salt, const string &hashedPassword);
};
#endif
