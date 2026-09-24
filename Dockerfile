# Stage 1: build the C++ binaries.
FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      cmake g++ make libhiredis-dev libpq-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY backend ./backend
COPY worker ./worker
COPY tests ./tests
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)"

# Stage 2: minimal runtime image carrying both binaries.
FROM ubuntu:24.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libhiredis1.1.0 libpq5 ca-certificates \
 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/backend/djq-backend /usr/local/bin/djq-backend
COPY --from=build /src/build/worker/djq-worker /usr/local/bin/djq-worker
EXPOSE 8080
CMD ["djq-backend"]