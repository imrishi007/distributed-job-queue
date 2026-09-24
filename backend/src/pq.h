#pragma once

#include "job.h"
#include "worker.h"

#include <libpq-fe.h>

#include <map>
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
    std::vector<Job> list_jobs(int limit);
    // Count of jobs per status, e.g. {{"queued",4},{"running",1},{"succeeded",9}}.
    std::map<std::string, long> status_counts();

    void register_worker(const std::string& id, const std::string& name);
    void heartbeat(const std::string& id, WorkerStatus status);
    std::vector<Worker> list_workers();
    // Deletes registrations whose heartbeat has been silent for
    // older_than_seconds — a dead (or vanished) worker, not a busy one.
    void prune_stale_workers(int older_than_seconds);

private:
    PGconn* conn_;
    // ponytail: single connection under a global lock; upgrade to a per-thread
    // pool if query traffic ever becomes a bottleneck.
    std::mutex mutex_;
};