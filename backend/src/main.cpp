#include <httplib.h>
#include <nlohmann/json.hpp>

#include "job.h"
#include "pq.h"
#include "redis.h"

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
    return svr.listen("127.0.0.1", 8080);
}