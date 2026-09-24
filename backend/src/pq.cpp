#include "pq.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <vector>

namespace {

constexpr const char* kSchemaSql =
    "CREATE TABLE IF NOT EXISTS jobs ("
    "id TEXT PRIMARY KEY, "
    "type TEXT NOT NULL, "
    "payload TEXT NOT NULL, "
    "status TEXT NOT NULL, "
    "output TEXT NOT NULL DEFAULT '', "
    "leased_at TIMESTAMPTZ, "
    "created_at TIMESTAMPTZ NOT NULL DEFAULT now() "
    ")";

constexpr const char* kMigrateSql =
    "ALTER TABLE jobs ADD COLUMN IF NOT EXISTS leased_at TIMESTAMPTZ";

}  // namespace

Pq::Pq(const std::string& conninfo) : conn_(PQconnectdb(conninfo.c_str())) {
    if (conn_ == nullptr || PQstatus(conn_) != CONNECTION_OK) {
        std::string msg = "connection failed";
        if (conn_ != nullptr) {
            msg = PQerrorMessage(conn_);
            PQfinish(conn_);
            conn_ = nullptr;
        }
        throw std::runtime_error("postgres: " + msg);
    }
    // Drops libpq's NOTICEs (e.g. "relation already exists, skipping") which
    // are expected during schema self-healing.
    PQsetNoticeProcessor(conn_, [](void*, const char*) {}, nullptr);
}

Pq::~Pq() {
    if (conn_ != nullptr) {
        PQfinish(conn_);
    }
}

void Pq::ensure_schema() {
    std::lock_guard lock(mutex_);
    for (const char* sql : {kSchemaSql, kMigrateSql}) {
        PGresult* res = PQexec(conn_, sql);
        if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
            const std::string detail =
                res != nullptr ? PQerrorMessage(conn_) : "PQexec returned null";
            if (res != nullptr) {
                PQclear(res);
            }
            // Concurrent workers racing CREATE/ALTER can lose with a
            // "duplicate key ... pg_type_typname_nsp_index" error; treat that
            // as "already done" and move on.
            if (detail.find("already exists") != std::string::npos ||
                detail.find("duplicate key") != std::string::npos) {
                continue;
            }
            throw std::runtime_error("postgres: ensure_schema failed: " + detail);
        }
        PQclear(res);
    }
}

void Pq::upsert_job(const Job& job) {
    std::lock_guard lock(mutex_);
    const std::vector<std::string> params = {
        job.id, job.type, job.payload, job_status_name(job.status), job.output};
    std::vector<const char*> values;
    values.reserve(params.size());
    for (const std::string& p : params) {
        values.push_back(p.c_str());
    }

    constexpr const char* sql =
        "INSERT INTO jobs (id, type, payload, status, output) "
        "VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (id) DO UPDATE SET status = EXCLUDED.status, output = EXCLUDED.output, "
        "leased_at = NULL";

    PGresult* res = PQexecParams(conn_, sql, 5, nullptr, values.data(), nullptr, nullptr, 0);
    if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
        const std::string detail = res != nullptr ? PQerrorMessage(conn_) : "PQexecParams returned null";
        if (res != nullptr) {
            PQclear(res);
        }
        throw std::runtime_error("postgres: upsert_job failed: " + detail);
    }
    PQclear(res);
}

void Pq::lease_job(const std::string& id) {
    std::lock_guard lock(mutex_);
    const char* param = id.c_str();
    constexpr const char* sql =
        "UPDATE jobs SET status = 'running', leased_at = now() WHERE id = $1";
    PGresult* res = PQexecParams(conn_, sql, 1, nullptr, &param, nullptr, nullptr, 0);
    if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
        const std::string detail =
            res != nullptr ? PQerrorMessage(conn_) : "PQexecParams returned null";
        if (res != nullptr) {
            PQclear(res);
        }
        throw std::runtime_error("postgres: lease_job failed: " + detail);
    }
    PQclear(res);
}

std::vector<Job> Pq::stale_running(int older_than_seconds) {
    std::lock_guard lock(mutex_);
    const std::string secs = std::to_string(older_than_seconds);
    const char* param = secs.c_str();
    constexpr const char* sql =
        "SELECT id, type, payload, status, output FROM jobs "
        "WHERE status = 'running' "
        "AND leased_at < now() - make_interval(secs => $1::int)";
    PGresult* res = PQexecParams(conn_, sql, 1, nullptr, &param, nullptr, nullptr, 0);
    if (res == nullptr || PQresultStatus(res) != PGRES_TUPLES_OK) {
        const std::string detail =
            res != nullptr ? PQerrorMessage(conn_) : "PQexecParams returned null";
        if (res != nullptr) {
            PQclear(res);
        }
        throw std::runtime_error("postgres: stale_running failed: " + detail);
    }

    std::vector<Job> jobs;
    for (int i = 0; i < PQntuples(res); ++i) {
        const nlohmann::json j = {{"id", PQgetvalue(res, i, 0)},
                                  {"type", PQgetvalue(res, i, 1)},
                                  {"payload", PQgetvalue(res, i, 2)},
                                  {"status", PQgetvalue(res, i, 3)},
                                  {"output", PQgetvalue(res, i, 4)}};
        jobs.push_back(j.get<Job>());
    }
    PQclear(res);
    return jobs;
}

bool Pq::reclaim_job(const std::string& id, int older_than_seconds) {
    std::lock_guard lock(mutex_);
    const std::string secs = std::to_string(older_than_seconds);
    const char* params[] = {id.c_str(), secs.c_str()};
    constexpr const char* sql =
        "UPDATE jobs SET status = 'running', leased_at = now() "
        "WHERE id = $1 AND status = 'running' "
        "AND leased_at < now() - make_interval(secs => $2::int)";
    PGresult* res = PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0);
    if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
        const std::string detail =
            res != nullptr ? PQerrorMessage(conn_) : "PQexecParams returned null";
        if (res != nullptr) {
            PQclear(res);
        }
        throw std::runtime_error("postgres: reclaim_job failed: " + detail);
    }
    const bool won = std::string(PQcmdTuples(res)) == "1";
    PQclear(res);
    return won;
}

std::optional<Job> Pq::fetch_job(const std::string& id) {
    std::lock_guard lock(mutex_);
    constexpr const char* sql = "SELECT type, payload, status, output FROM jobs WHERE id = $1";
    const char* param = id.c_str();

    PGresult* res = PQexecParams(conn_, sql, 1, nullptr, &param, nullptr, nullptr, 0);
    if (res == nullptr || PQresultStatus(res) != PGRES_TUPLES_OK) {
        const std::string detail = res != nullptr ? PQerrorMessage(conn_) : "PQexecParams returned null";
        if (res != nullptr) {
            PQclear(res);
        }
        throw std::runtime_error("postgres: fetch_job failed: " + detail);
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    const nlohmann::json j = {{"id", id},
                              {"type", PQgetvalue(res, 0, 0)},
                              {"payload", PQgetvalue(res, 0, 1)},
                              {"status", PQgetvalue(res, 0, 2)},
                              {"output", PQgetvalue(res, 0, 3)}};
    PQclear(res);
    return j.get<Job>();
}