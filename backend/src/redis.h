#pragma once

#include <hiredis/hiredis.h>

#include <mutex>
#include <string>

class Redis {
public:
    Redis(const std::string& host, int port);
    ~Redis();

    Redis(const Redis&) = delete;
    Redis& operator=(const Redis&) = delete;

    void push(const std::string& key, const std::string& value);

private:
    redisContext* ctx_;
    // ponytail: single shared connection under a global lock; upgrade to a
    // per-thread connection pool once concurrency makes this a bottleneck.
    std::mutex mutex_;
};