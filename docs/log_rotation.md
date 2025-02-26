# Zener 日志轮转功能说明

## 概述

Zener 现已支持日志轮转功能，可以防止单个日志文件过大，并自动管理日志文件数量。当日志文件达到配置的最大大小时，系统会自动创建新的日志文件，并保留指定数量的历史日志文件。

## 使用方法

### 基本用法

在应用程序中，使用 `Logger::WriteToFileWithRotation` 方法设置日志轮转：

```cpp
// 初始化日志系统
zener::Logger::Init();

// 启用日志轮转，使用默认配置（50MB最大文件大小，10个历史文件）
zener::Logger::WriteToFileWithRotation("logs", "app_name");
```

### 自定义配置

您可以自定义最大文件大小和要保留的历史文件数量：

```cpp
// 初始化日志系统
zener::Logger::Init();

// 启用日志轮转，文件最大100MB，保留5个历史文件
size_t max_size = 100 * 1024 * 1024; // 100MB
size_t max_files = 5;                // 5个历史文件
zener::Logger::WriteToFileWithRotation("logs", "app_name", max_size, max_files);
```

## 文件命名规则

日志文件使用以下命名规则：

- 当前活动日志文件：`<prefix>.log`
- 轮转的历史文件：`<prefix>.1.log`, `<prefix>.2.log`, ..., `<prefix>.<max_files>.log`

数字较小的文件包含较新的日志内容，例如：
- `app_name.log` - 当前活动日志文件
- `app_name.1.log` - 最近轮转的历史文件
- `app_name.2.log` - 第二最近轮转的历史文件
- ...以此类推

## 技术实现

日志轮转功能基于 SPDLog 库的 `rotating_file_sink_mt` 实现。该实现提供了线程安全的日志轮转功能，具有以下特点：

1. **线程安全**：使用互斥锁保护日志文件操作，确保多线程环境下的安全性
2. **零数据丢失**：日志轮转过程中不会丢失消息
3. **按需轮转**：只有在文件大小超过指定限制时才会轮转
4. **自动文件管理**：自动处理历史文件的命名和删除

## 自定义日志目录

可以指定日志文件的存储目录：

```cpp
// 将日志存储在 /var/log/myapp 目录下
zener::Logger::WriteToFileWithRotation("/var/log/myapp", "app_name");
```

如果指定的目录不存在，系统会尝试创建它。如果创建失败，会返回错误并记录到标准错误输出。

## 测试日志轮转

项目包含一个测试脚本 `scripts/test_log_rotation.sh`，可用于测试日志轮转功能。该脚本会生成大量日志消息，触发日志轮转，然后检查是否创建了多个日志文件。

使用方法：

```bash
cd /path/to/zener
./scripts/test_log_rotation.sh
```

## 注意事项

1. 如果在程序运行期间多次调用 `WriteToFileWithRotation`，只有最后一次调用的配置会生效
2. 日志轮转只会在文件大小超过限制时发生，如果文件大小未达到限制，不会创建新文件
3. 当达到最大历史文件数量时，最旧的日志文件将被删除
4. 在调用 `WriteToFileWithRotation` 之前，必须先调用 `Logger::Init()` 初始化日志系统

## 性能考虑

启用日志轮转可能会对性能产生轻微影响，特别是在日志轮转发生时。然而，SPDLog 库已经做了很多优化工作，如使用异步日志和批量写入，以最小化这种影响。

对于大多数应用来说，这种性能影响是可以接受的，并且远远低于存储空间耗尽或单个日志文件过大导致的问题。 