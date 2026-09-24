# Distributed Job Queue

A distributed job queue and task-processing system built in modern C++, developed as a learning-first project.

The finished system accepts computational jobs over a REST API, runs them on independent worker processes fed from a Redis queue, and records job state in PostgreSQL. The pieces land one at a time; this README reflects whatever currently works.

## What it can do right now

- Builds a single C++20 executable with CMake (`backend/`), requiring only g++ 13+, CMake 3.16+ and make.
- Bundles cpp-httplib (v0.57.1) as a vendored single-header dependency.
- Runs an HTTP server on `127.0.0.1:8080`.
- Serves `GET /` — a plain-text greeting.
- Serves `GET /health` — a JSON `{"status":"ok"}` readiness probe.
- Serves `GET /metrics` — live queue depth (Redis `LLEN jobs`) plus jobs-per-status counts from PostgreSQL, for a dashboard status strip.
- Replies `404 Not Found` to unknown routes.
- Accepts jobs via `POST /jobs` — validates the JSON body, assigns an id, persists the job to PostgreSQL, pushes it onto a Redis list, and returns `201` with the stored job. Request: `{"type":"...","payload":"..."}`.
- Serves `GET /jobs/<id>` — looks up a job by its 16-hex-char id in PostgreSQL and returns it (`404` if unknown).
- Serves `GET /jobs` — lists the most recent 50 jobs (newest first) from PostgreSQL, e.g. for the dashboard.
- Runs `djq-worker` processes that consume jobs off the queue, execute them (built-in types: `sleep`, `echo`), and record each result in PostgreSQL and Redis (`job:<id>`) with a `succeeded`/`failed` status and an output string. Run as many as you like — Redis hands each queued job to exactly one consumer, so workers scale horizontally with no coordination (pass a name argument, e.g. `djq-worker A`, to tell them apart in logs).
- Recovers from worker crashes: a worker leases a job (marks it `running` with a timestamp) before executing; a periodic sweep inside each worker re-queues any job whose lease has gone stale, so work **survives a killed worker** (at-least-once delivery — a job may run more than once, never less).
- Runs a worker registry: each `djq-worker` gets a stable id and heartbeats (`voluntary`/`busy`) into PostgreSQL, and `GET /workers` returns the live fleet — an absent heartbeat (`last_seen` going stale) is how a dead worker is distinguished from a busy one. A backend housekeeping thread deletes registrations whose heartbeat has been silent for 120s, so vanished workers are eventually reaped from the fleet.
- Persists every job and result in a `jobs` table (id, type, payload, status, output, created_at). PostgreSQL is the durable system of record; Redis carries the live queue. Data survives restarts and `flushdb`.
- Represents jobs as a typed C++ model (`Job` + status enum) and serializes them to/from JSON.
- Includes a small self-check executable (`build/tests/djq-tests`) exercising the JSON round-trip.
- Reads connection settings from the environment (`DJQ_PG_DSN`, `DJQ_REDIS_HOST`/`DJQ_REDIS_PORT`, `DJQ_BIND`) with bare-metal defaults, so the very same binaries run on a host or inside a container.
- Is fully containerised: `docker compose up --build` brings up redis, PostgreSQL (with a data volume), the backend, two workers, and the dashboard built to static files and served by nginx (which also reverse-proxies `/api` to the backend). The dashboard lands on `http://localhost:8081`; the backend API is on `http://localhost:8082`.
- Includes a live React dashboard (`dashboard/`) — worker fleet with online/offline liveness, queue depth and jobs-per-status strip, recent jobs, and a job-submission form with per-type payload defaults and client-side validation. In dev it runs as a Vite server that proxies `/api` to the backend on `127.0.0.1:8080`.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/backend/djq-backend
```

Run the dashboard (a Node.js LTS runtime — e.g. the official Linux tarball — is required):

```bash
cd dashboard
npm install
npm run dev
```

Then open `http://localhost:5173`.

Run everything in containers instead:

```bash
docker compose up -d --build
```

Open `http://localhost:8081` for the dashboard; submit works straight from there. `docker compose ps` shows the six services; `docker compose logs -f worker` tails the fleet.

## Tech stack

| Layer | Technology |
| --- | --- |
| Backend / worker processes | C++20, CMake, cpp-httplib, nlohmann/json |
| Queue | Redis 7 (via hiredis) |
| Persistence | PostgreSQL 16 (via libpq) |
| Dashboard | React + Vite |
| Packaging | Docker |