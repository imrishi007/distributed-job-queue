#include "job.h"

#include <cassert>
#include <iostream>

int main() {
    const Job original{"job-1", "sleep", R"({"seconds": 1})", JobStatus::Queued};

    const nlohmann::json json = original;
    const Job round = json.get<Job>();

    assert(round.id == original.id);
    assert(round.type == original.type);
    assert(round.payload == original.payload);
    assert(round.status == original.status);

    const nlohmann::json expected = {{"id", "job-1"},
                                     {"type", "sleep"},
                                     {"payload", "{\"seconds\": 1}"},
                                     {"status", "queued"}};
    assert(json == expected);

    std::cout << "job JSON round-trip OK\n";
}