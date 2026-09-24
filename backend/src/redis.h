#pragma once

#include <hiredis/hiredis.h>

#include <mutex>
#include <optional>
#include <string>

class Redis {
public:
    Redis(const std::string& host, int port);
    ~Redis();

    Redis(const Redis&) = delete;
    Redis& operator=(const Redis&) = delete;

    void push(const std::string& key, const std::string& value);
    // Blocks up to timeout_seconds (0 = forever) for an element on key;
    // returns std::nullopt on timeout.
    std::optional<std::string> pop(const std::string& key, double timeout_seconds);
    void set(const std::string& key, const std::string& value);
    // Number of elements in key (LLEN); -1 on error. Used for queue depth.
    long long llen(const std::string& key);

private:
    redisContext* ctx_;
    // ponytail: single shared connection under a global lock; upgrade to a
    // per-thread connection pool once concurrency makes this a bottleneck.
    std::mutex mutex_;
};