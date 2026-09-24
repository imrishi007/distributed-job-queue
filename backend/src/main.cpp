#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <thread>

#include "job.h"
#include "pq.h"
#include "redis.h"
#include "worker.h"

// How long a worker may be silent before the registry reaps it. Must exceed
// the voluntary heartbeat gap (5s) plus the longest plausible job run; busy
// long-running jobs are NOT reaped while inside this window (ponytail: true
// production needs in-band liveness, i.e. a worker that keeps heartbeating
// while busy; here jobs are sub-minute so a coarse TTL is safe).
constexpr int kWorkerTtlSeconds = 120;
constexpr int kHousekeepingIntervalSeconds = 5;

int main() {
    Pq pq("host=/var/run/postgresql dbname=jobqueue");
    pq.ensure_schema();
    Redis redis("127.0.0.1", 6379);

    httplib::Server svr;
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Hello from Job Queue", "text/plain");
    });
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });
    svr.Get("/metrics", [&redis, &pq](const httplib::Request&, httplib::Response& res) {
        try {
            const long long depth = redis.llen("jobs");
            const auto counts = pq.status_counts();
            nlohmann::json j = {{"queue_depth", depth}, {"jobs_by_status", nlohmann::json::object()}};
            for (const auto& [status, count] : counts) {
                j["jobs_by_status"][status] = count;
            }
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception&) {
            res.status = 500;
            res.set_content(R"({"error":"metrics unavailable"})", "application/json");
        }
    });
    svr.Post("/jobs", [&redis, &pq](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto body = nlohmann::json::parse(req.body);
            Job job;
            job.id = generate_job_id();
            job.type = body.at("type").get<std::string>();
            job.payload = body.at("payload").get<std::string>();
            job.status = JobStatus::Queued;

            pq.upsert_job(job);
            redis.push("jobs", nlohmann::json(job).dump());

            res.status = 201;
            res.set_content(nlohmann::json(job).dump(), "application/json");
        } catch (const nlohmann::json::exception&) {
            res.status = 400;
            res.set_content(R"({"error":"invalid job"})", "application/json");
        } catch (const std::exception&) {
            res.status = 500;
            res.set_content(R"({"error":"queue unavailable"})", "application/json");
        }
    });
    svr.Get("/jobs", [&pq](const httplib::Request&, httplib::Response& res) {
        try {
            const auto jobs = pq.list_jobs(50);
            nlohmann::json arr = nlohmann::json::array();
            for (const Job& j : jobs) {
                arr.push_back(j);
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception&) {
            res.status = 500;
            res.set_content(R"({"error":"store unavailable"})", "application/json");
        }
    });
    svr.Get(R"(/jobs/([0-9a-f]+))", [&pq](const httplib::Request& req, httplib::Response& res) {
        try {
            const auto job = pq.fetch_job(req.matches[1].str());
            if (!job) {
                res.status = 404;
                res.set_content(R"({"error":"not found"})", "application/json");
                return;
            }
            res.set_content(nlohmann::json(*job).dump(), "application/json");
        } catch (const std::exception&) {
            res.status = 500;
            res.set_content(R"({"error":"store unavailable"})", "application/json");
        }
    });
    svr.Get("/workers", [&pq](const httplib::Request&, httplib::Response& res) {
        try {
            const auto workers = pq.list_workers();
            nlohmann::json arr = nlohmann::json::array();
            for (const Worker& w : workers) {
                arr.push_back(w);
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception&) {
            res.status = 500;
            res.set_content(R"({"error":"store unavailable"})", "application/json");
        }
    });
    // Housekeeping: periodically reap workers whose heartbeats went silent.
    // A detached thread so the HTTP server stays responsive; a PG hiccup is
    // logged to stderr and retried next cycle.
    std::thread([&pq] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(kHousekeepingIntervalSeconds));
            try {
                pq.prune_stale_workers(kWorkerTtlSeconds);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "housekeeping: %s\n", e.what());
            }
        }
    }).detach();

    return svr.listen("127.0.0.1", 8080);
}