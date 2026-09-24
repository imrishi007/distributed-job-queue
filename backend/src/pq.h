#pragma once

#include "job.h"

#include <libpq-fe.h>

#include <mutex>
#include <optional>
#include <string>

class Pq {
public:
    Pq(const std::string& conninfo);
    ~Pq();

    Pq(const Pq&) = delete;
    Pq& operator=(const Pq&) = delete;

    void ensure_schema();
    void upsert_job(const Job& job);
    std::optional<Job> fetch_job(const std::string& id);

private:
    PGconn* conn_;
    // ponytail: single connection under a global lock; upgrade to a per-thread
    // pool if query traffic ever becomes a bottleneck.
    std::mutex mutex_;
};