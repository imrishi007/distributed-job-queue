#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "job.h"
#include "pq.h"
#include "redis.h"

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

    Redis redis("127.0.0.1", 6379);
    Pq pq("host=/var/run/postgresql dbname=jobqueue");
    pq.ensure_schema();
    std::cout << "[" << name << "] started, waiting for jobs" << std::endl;

    // Self-heal on startup: a previous crash may have left orphans behind.
    sweep(redis, pq, name);

    // Self-healer: rescues work orphaned by crashed peers. The main loop keeps
    // its zero-CPU blocking BLPOP; this thread owns the recovery pacing.
    for (;;) {
        // Bounded block (5s) instead of "0 = forever": the wakeup doubles as a
        // heartbeat so the single worker thread can rescue orphans too.
        // (A forever-blocking pop on the shared Redis connection would starve
        // any concurrent sweeper trying to re-push to the same mutex.)
        const auto raw = redis.pop("jobs", 5 /* seconds */);
        if (!raw) {
            sweep(redis, pq, name);
            continue;
        }

        Job job;
        std::string output;
        try {
            job = nlohmann::json::parse(*raw).get<Job>();
            std::cout << "[" << name << "] job " << job.id << " started" << std::endl;
            job.status = JobStatus::Running;
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