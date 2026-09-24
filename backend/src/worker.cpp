#include "worker.h"

const char* worker_status_name(WorkerStatus status) {
    switch (status) {
        case WorkerStatus::Voluntary: return "voluntary";
        case WorkerStatus::Busy: return "busy";
    }
    return "unknown";
}

void to_json(nlohmann::json& j, const Worker& w) {
    j = nlohmann::json{{"id", w.id},
                       {"name", w.name},
                       {"status", worker_status_name(w.status)},
                       {"last_seen", w.last_seen}};
}