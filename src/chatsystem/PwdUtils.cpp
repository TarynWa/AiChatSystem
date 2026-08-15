#include "PwdUtils.hpp"
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <cstdio>
#include <cstring>

string PwdUtils::generateSalt()
{
    unsigned char buf[16] = {0};
    if (RAND_bytes(buf, sizeof(buf)) != 1)
    {
        WT_LOG_ERROR << "PwdUtils::generateSalt RAND_bytes failed";
        return string();
    }
    char hex[33] = {0};
    for (size_t i = 0; i < sizeof(buf); ++i)
    {
        snprintf(hex + i * 2, 3, "%02x", buf[i]);
    }
    return string(hex, 32);
}

string PwdUtils::sha256(const string &input)
{
    unsigned char hash[SHA256_DIGEST_LENGTH] = {0};
    SHA256(reinterpret_cast<const unsigned char *>(input.data()), input.size(), hash);
    char hex[65] = {0};
    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    }
    return string(hex, 64);
}

bool PwdUtils::verify(const string &plainPassword, const string &salt, const string &hashedPassword)
{
    if (salt.empty() || hashedPassword.empty())
    {
        return false;
    }
    string computed = sha256(salt + plainPassword);
    return computed == hashedPassword;
}
