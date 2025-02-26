# Zener高并发服务器

Zener是一个高性能的并发服务器，支持多种定时器实现，针对高并发场景进行了深度优化。

## 定时器实现

服务器支持两种定时器实现：

1. **HEAP定时器** - 基于小顶堆的定时器实现，吞吐量略高
2. **MAP定时器** - 基于红黑树的定时器实现，稳定性略好

两种实现各有优劣，可以根据实际需求选择合适的版本。

## 日志系统

Zener使用高性能的SPDLog库实现日志功能，支持以下特性：

1. **日志分级** - 支持TRACE、DEBUG、INFO、WARN、ERROR、CRITICAL等级别
2. **日志轮转** - 自动管理日志文件大小和数量，防止磁盘占用过大
3. **控制台和文件双向输出** - 日志同时输出到控制台和文件

### 日志轮转

当日志文件达到指定大小(默认50MB)时，系统会自动创建新的日志文件，并保留指定数量(默认10个)的历史文件。详细说明请参考[日志轮转文档](docs/log_rotation.md)。

使用示例:
```cpp
// 使用默认配置(50MB文件大小，10个历史文件)
zener::Logger::WriteToFileWithRotation("logs", "server");

// 自定义配置
zener::Logger::WriteToFileWithRotation("logs", "server", 100*1024*1024, 5);
```

## 构建与运行

### 脚本概述

为了简化开发和测试流程，我们提供了以下脚本：

- `scripts/build_all.sh` - 构建MAP和HEAP两个版本的服务器
- `scripts/run_server.sh` - 运行指定版本的服务器
- `scripts/benchmark.sh` - 对服务器进行性能测试
- `scripts/kill.sh` - 停止正在运行的服务器
- `scripts/test_log_rotation.sh` - 测试日志轮转功能

### 构建所有版本

```bash
./scripts/build_all.sh
```

此脚本会同时构建MAP和HEAP两个版本的服务器，输出文件分别为：

- `bin/Zener_map` - MAP定时器版本
- `bin/Zener_heap` - HEAP定时器版本

### 运行服务器

```bash
./scripts/run_server.sh -t map -p 1316  # 运行MAP版本
./scripts/run_server.sh -t heap -p 8080  # 运行HEAP版本
```

参数说明：
- `-t, --timer <type>` - 定时器类型 (map 或 heap)
- `-p, --port <port>` - 服务器端口
- `-h, --help` - 显示帮助信息

### 性能测试

```bash
./scripts/benchmark.sh -t map -c 10000 -d 10  # 测试MAP版本，10000并发，10秒
```

参数说明：
- `-t, --timer <type>` - 定时器类型 (map 或 heap)
- `-p, --port <port>` - 服务器端口
- `-c, --connections <num>` - 并发连接数
- `-d, --duration <sec>` - 测试持续时间(秒)
- `-u, --url <path>` - 请求URL路径
- `-n, --no-start` - 不自动启动服务器(假设服务器已运行)
- `-h, --help` - 显示帮助信息

测试结果会保存在 `results/` 目录下。

## 定时器优化记录

关于定时器实现的深度优化和问题修复，请参考 [调试与优化文档](docs/debugging_and_optimization.md)。

## 调试建议

1. 对比测试两种定时器实现，找出最适合您应用场景的版本
2. 日志文件保存在 `logs/` 目录下，可以通过日志分析定位问题
3. 使用日志轮转功能防止日志文件过大导致的磁盘空间问题
4. 在进行性能测试前，确保没有其他高负载程序正在运行

## 开发者指南

如果您需要进一步开发或修改定时器实现，建议查看：

- `include/task/timer/timer.h` - 定时器基类和接口定义
- `include/task/timer/heaptimer.h` - 堆定时器头文件
- `include/task/timer/maptimer.h` - MAP定时器头文件
- `src/task/timer/heaptimer.cpp` - 堆定时器实现
- `src/task/timer/maptimer.cpp` - MAP定时器实现

日志系统相关文件：
- `include/utils/log/use_spd_log.h` - 日志系统头文件
- `src/utils/log/use_spd_log.cpp` - 日志系统实现

在修改实现时，请特别注意线程安全、资源管理和异常处理。

## 性能参考

在AMD Ryzen 7 处理器，32GB内存的测试环境下，使用WebBench测试工具(10000并发，10秒)的参考性能：

- **MAP定时器**：~73,000 pages/min，成功率 99.98%
- **HEAP定时器**：~80,000 pages/min，成功率 99.76%

实际性能可能因硬件配置和网络环境而异。
