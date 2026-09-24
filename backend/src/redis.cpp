#include "redis.h"

#include <stdexcept>

Redis::Redis(const std::string& host, int port)
    : ctx_(redisConnect(host.c_str(), port)) {
    if (ctx_ == nullptr || ctx_->err != 0) {
        std::string msg = "connection failed";
        if (ctx_ != nullptr) {
            msg = ctx_->errstr;
            redisFree(ctx_);
            ctx_ = nullptr;
        }
        throw std::runtime_error("redis: " + msg);
    }
}

Redis::~Redis() {
    if (ctx_ != nullptr) {
        redisFree(ctx_);
    }
}

void Redis::push(const std::string& key, const std::string& value) {
    std::lock_guard lock(mutex_);
    redisReply* reply =
        static_cast<redisReply*>(redisCommand(ctx_, "RPUSH %s %s", key.c_str(), value.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        throw std::runtime_error("redis: RPUSH failed");
    }
    freeReplyObject(reply);
}

std::optional<std::string> Redis::pop(const std::string& key, double timeout_seconds) {
    std::lock_guard lock(mutex_);
    redisReply* reply =
        static_cast<redisReply*>(redisCommand(ctx_, "BLPOP %s %f", key.c_str(), timeout_seconds));
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        throw std::runtime_error("redis: BLPOP failed");
    }

    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        return std::nullopt;
    }

    // BLPOP on one key returns [key, value].
    const redisReply* value = reply->element[1];
    std::string element(value->str, value->len);
    freeReplyObject(reply);
    return element;
}

void Redis::set(const std::string& key, const std::string& value) {
    std::lock_guard lock(mutex_);
    redisReply* reply =
        static_cast<redisReply*>(redisCommand(ctx_, "SET %s %s", key.c_str(), value.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        throw std::runtime_error("redis: SET failed");
    }
    freeReplyObject(reply);
}

long long Redis::llen(const std::string& key) {
    std::lock_guard lock(mutex_);
    redisReply* reply =
        static_cast<redisReply*>(redisCommand(ctx_, "LLEN %s", key.c_str()));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER) {
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
        throw std::runtime_error("redis: LLEN failed");
    }
    const long long len = reply->integer;
    freeReplyObject(reply);
    return len;
}