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