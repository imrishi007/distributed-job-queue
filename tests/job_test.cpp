#include "job.h"

#include <cassert>
#include <iostream>

int main() {
    const Job original{"job-1", "sleep", R"({"seconds": 1})", JobStatus::Queued, "slept 1s"};

    const nlohmann::json json = original;
    const Job round = json.get<Job>();

    assert(round.id == original.id);
    assert(round.type == original.type);
    assert(round.payload == original.payload);
    assert(round.status == original.status);
    assert(round.output == original.output);

    const nlohmann::json expected = {{"id", "job-1"},
                                     {"type", "sleep"},
                                     {"payload", "{\"seconds\": 1}"},
                                     {"status", "queued"},
                                     {"output", "slept 1s"},
                                     {"created_at", ""}};
    assert(json == expected);

    std::cout << "job JSON round-trip OK\n";
}