# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

The binary is output to `./bin/Zener`.

For debug builds:
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

## Run

```bash
./bin/Zener
```

Requires MySQL running (see docker-compose). Config is read from `config.toml` in the working directory.

```bash
docker-compose up -d mysql
```

## Benchmarking

```bash
./scripts/benchmark.sh -t map -c 1000 -d 10
# -t: timer type (map or heap)
# -c: concurrent connections
# -d: duration in seconds
# -p: port (default 1316)
# -u: URL path
```

## Architecture

Zener is a C++20 HTTP/1.1 web server using the **Reactor pattern** with Linux epoll.

### Request lifecycle

```
epoll_wait → Server::dealRead/dealWrite
               → ThreadPool::enqueue
                 → Conn::Process (parse + build response)
                   → Request (parse HTTP)
                   → Response (build reply, serve static files)
                   → SqlConnector (auth queries if needed)
               → Conn::Write (send to client fd)
```

### Key components

- `include/core/` — `Server` (main reactor loop, connection lifecycle) and `Epoller` (epoll wrapper)
- `include/http/` — `Conn` (per-connection state + buffers), `Request` (HTTP parser), `Response` (response builder), `FileCache` (mmap-based static file cache)
- `include/task/` — `ThreadPool` (worker threads), `HeapTimer`/`MapTimer` (connection timeout, two implementations selectable at runtime)
- `include/database/` — `SqlConnector` singleton connection pool; `sqlconnRAII.hpp` for RAII checkout
- `include/config/` — TOML config loader; settings include port, epoll trigger mode, thread pool size, MySQL credentials, log level
- `include/buffer/` — Read/write buffer abstraction used by `Conn`

### Timer implementations

Two timer backends exist (`HeapTimer` and `MapTimer`), selectable via benchmark script `-t` flag. The server instantiates one at startup based on config/compile choice.

### Static files

Served from `./static/`. `FileCache` uses `mmap` and caches file descriptors to avoid repeated syscalls.

### Database

MySQL only. Single `user` table (username, password). Connection pool size and credentials are in `config.toml`. Bootstrap schema is in `database/bootstrap.sql`.

## Configuration (`config.toml`)

| Key | Default | Notes |
|-----|---------|-------|
| `port` | 1316 | Listen port |
| `trig` | 3 | epoll trigger mode bitmask |
| `timeout` | 60000 | Connection timeout (ms) |
| `thread.poolSize` | 8 | Worker threads |
| `mysql.poolSize` | 8 | DB connection pool |
| `log.level` | DEBUG | spdlog level |

## Dependencies

Vendored under `third_party/`: spdlog, nlohmann/json, Boost (regex). MySQL client linked via system library. CMake finds them automatically.
