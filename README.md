# Distributed Job Queue

A distributed job queue and task-processing system built in modern C++, developed as a learning-first project.

The finished system accepts computational jobs over a REST API, runs them on independent worker processes fed from a Redis queue, and records job state in PostgreSQL. The pieces land one at a time; this README reflects whatever currently works.

## What it can do right now

- Builds a single C++20 executable with CMake (`backend/`), requiring only g++ 13+, CMake 3.16+ and make.
- Bundles cpp-httplib (v0.57.1) as a vendored single-header dependency.
- Runs a minimal HTTP server on `127.0.0.1:8080` that accepts incoming connections.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/backend/djq-backend
```

## Tech stack

| Layer | Technology |
| --- | --- |
| Backend / worker processes | C++20, CMake, cpp-httplib |
| Queue | Redis |
| Persistence | PostgreSQL |
| Dashboard | React + Vite |
| Packaging | Docker |