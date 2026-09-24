#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "env.h"
#include "job.h"
#include "pq.h"
#include "redis.h"
#include "worker.h"

namespace {

std::string execute(const Job& job) {
    if (job.type == "sleep") {
        const int seconds = nlohmann::json::parse(job.payload).at("seconds").get<int>();
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        return "slept " + std::to_string(seconds) + "s";
    }
    if (job.type == "echo") {
        return job.payload;
    }
    throw std::runtime_error("unknown job type: " + job.type);
}

}  // namespace

namespace {

// ponytail: hardcoded lease; tune down for a snappy demo or up if jobs may
// legitimately run long. A job that outlives its lease gets rescued and
// re-run (at-least-once; duplicates are a known Phase-idempotency concern).
constexpr int kLeaseSeconds = 10;

void sweep(Redis& redis, Pq& pq, const std::string& name) {
    try {
        for (const Job& stale : pq.stale_running(kLeaseSeconds)) {
            // reclaim_job is a conditional UPDATE: only the sweeper whose
            // WHERE still matches (lease still old) wins and re-queues.
            if (pq.reclaim_job(stale.id, kLeaseSeconds)) {
                redis.push("jobs", nlohmann::json(stale).dump());
                std::cout << "[" << name << "] rescued orphaned job " << stale.id << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[" << name << "] warning: sweep failed: " << e.what() << std::endl;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string name = argc > 1 ? argv[1] : "worker";
    const std::string pg_dsn = env_or("DJQ_PG_DSN", "host=/var/run/postgresql dbname=jobqueue");
    const std::string redis_host = env_or("DJQ_REDIS_HOST", "127.0.0.1");
    const int redis_port = std::stoi(env_or("DJQ_REDIS_PORT", "6379"));

    Redis redis(redis_host, redis_port);
    Pq pq(pg_dsn);
    pq.ensure_schema();
    const std::string worker_id = generate_job_id();
    pq.register_worker(worker_id, name);
    std::cout << "[" << name << "] started, waiting for jobs" << std::endl;

    // Self-heal on startup: a previous crash may have left orphans behind.
    sweep(redis, pq, name);

    for (;;) {
        // Bounded block (5s) instead of "0 = forever": the wakeup doubles as a
        // heartbeat so the single worker thread can rescue orphans too.
        // (A forever-blocking pop on the shared Redis connection would starve
        // any concurrent sweeper trying to re-push to the same mutex.)
        const auto raw = redis.pop("jobs", 5 /* seconds */);
        if (!raw) {
            // ponytail: heartbeats only land between jobs, so a long-running
            // job can age last_seen and briefly look "down" until it finishes.
            pq.heartbeat(worker_id, WorkerStatus::Voluntary);
            sweep(redis, pq, name);
            continue;
        }

        Job job;
        std::string output;
        try {
            job = nlohmann::json::parse(*raw).get<Job>();
            std::cout << "[" << name << "] job " << job.id << " started" << std::endl;
            job.status = JobStatus::Running;
            pq.heartbeat(worker_id, WorkerStatus::Busy);
            // ponytail: a worker dying between pop and this lease leaves a job
            // neither in Redis nor marked running — a residual at-most-once
            // gap that needs transactioning across the two stores to close.
            pq.lease_job(job.id);
            output = execute(job);
            job.status = JobStatus::Succeeded;
        } catch (const std::exception& e) {
            job.status = JobStatus::Failed;
            output = e.what();
        }

        job.output = output;
        redis.set("job:" + job.id, nlohmann::json(job).dump());
        // ponytail: log-and-continue on Postgres hiccups so a worker crash
        // doesn't stall the whole queue; durable reconciliation comes later.
        try {
            pq.upsert_job(job);
        } catch (const std::exception& e) {
            std::cout << "[" << name << "] warning: postgres write failed: " << e.what()
                      << std::endl;
        }

        std::cout << "[" << name << "] job " << job.id << ' ' << job_status_name(job.status)
                  << " — " << output << std::endl;
    }
}