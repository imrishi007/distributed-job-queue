# Distributed Job Queue

A distributed job queue and task-processing system built in modern C++, developed as a learning-first project.

The finished system accepts computational jobs over a REST API, runs them on independent worker processes fed from a Redis queue, and records job state in PostgreSQL. The pieces land one at a time; this README reflects whatever currently works.

## What it can do right now

- Builds a single C++20 executable with CMake (`backend/`), requiring only g++ 13+, CMake 3.16+ and make.
- Bundles cpp-httplib (v0.57.1) as a vendored single-header dependency.
- Runs an HTTP server on `127.0.0.1:8080`.
- Serves `GET /` — a plain-text greeting.
- Serves `GET /health` — a JSON `{"status":"ok"}` readiness probe.
- Replies `404 Not Found` to unknown routes.
- Accepts jobs via `POST /jobs` — validates the JSON body, assigns an id, persists the job to PostgreSQL, pushes it onto a Redis list, and returns `201` with the stored job. Request: `{"type":"...","payload":"..."}`.
- Serves `GET /jobs/<id>` — looks up a job by its 16-hex-char id in PostgreSQL and returns it (`404` if unknown).
- Runs `djq-worker` processes that consume jobs off the queue, execute them (built-in types: `sleep`, `echo`), and record each result in PostgreSQL and Redis (`job:<id>`) with a `succeeded`/`failed` status and an output string. Run as many as you like — Redis hands each queued job to exactly one consumer, so workers scale horizontally with no coordination (pass a name argument, e.g. `djq-worker A`, to tell them apart in logs).
- Recovers from worker crashes: a worker leases a job (marks it `running` with a timestamp) before executing; a periodic sweep inside each worker re-queues any job whose lease has gone stale, so work **survives a killed worker** (at-least-once delivery — a job may run more than once, never less).
- Runs a worker registry: each `djq-worker` gets a stable id and heartbeats (`voluntary`/`busy`) into PostgreSQL, and `GET /workers` returns the live fleet — an absent heartbeat (`last_seen` going stale) is how a dead worker is distinguished from a busy one.
- Persists every job and result in a `jobs` table (id, type, payload, status, output, created_at). PostgreSQL is the durable system of record; Redis carries the live queue. Data survives restarts and `flushdb`.
- Represents jobs as a typed C++ model (`Job` + status enum) and serializes them to/from JSON.
- Includes a small self-check executable (`build/tests/djq-tests`) exercising the JSON round-trip.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/backend/djq-backend
```

## Tech stack

| Layer | Technology |
| --- | --- |
| Backend / worker processes | C++20, CMake, cpp-httplib, nlohmann/json |
| Queue | Redis 7 (via hiredis) |
| Persistence | PostgreSQL 16 (via libpq) |
| Dashboard | React + Vite |
| Packaging | Docker |