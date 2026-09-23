#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "job.h"
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

int main(int argc, char** argv) {
    const std::string name = argc > 1 ? argv[1] : "worker";

    Redis redis("127.0.0.1", 6379);
    std::cout << "[" << name << "] started, waiting for jobs" << std::endl;

    for (;;) {
        const auto raw = redis.pop("jobs", 0 /* block forever */);
        if (!raw) {
            continue;
        }

        Job job;
        std::string output;
        try {
            job = nlohmann::json::parse(*raw).get<Job>();
            std::cout << "[" << name << "] job " << job.id << " started" << std::endl;
            job.status = JobStatus::Running;
            output = execute(job);
            job.status = JobStatus::Succeeded;
        } catch (const std::exception& e) {
            job.status = JobStatus::Failed;
            output = e.what();
        }

        job.output = output;
        redis.set("job:" + job.id, nlohmann::json(job).dump());

        std::cout << "[" << name << "] job " << job.id << ' ' << job_status_name(job.status)
                  << " — " << output << std::endl;
    }
}