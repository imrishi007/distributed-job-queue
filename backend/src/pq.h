#pragma once

#include "job.h"
#include "worker.h"

#include <libpq-fe.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

class Pq {
public:
    Pq(const std::string& conninfo);
    ~Pq();

    Pq(const Pq&) = delete;
    Pq& operator=(const Pq&) = delete;

    void ensure_schema();
    void upsert_job(const Job& job);
    void lease_job(const std::string& id);
    std::vector<Job> stale_running(int older_than_seconds);
    bool reclaim_job(const std::string& id, int older_than_seconds);
    std::optional<Job> fetch_job(const std::string& id);

    void register_worker(const std::string& id, const std::string& name);
    void heartbeat(const std::string& id, WorkerStatus status);
    std::vector<Worker> list_workers();

private:
    PGconn* conn_;
    // ponytail: single connection under a global lock; upgrade to a per-thread
    // pool if query traffic ever becomes a bottleneck.
    std::mutex mutex_;
};