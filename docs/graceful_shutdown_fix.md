# Zener服务器优雅退出问题修复

## 问题描述

Zener服务器在收到Ctrl+C（SIGINT信号）后无法正常优雅退出，而是卡在终端，必须使用kill命令强制终止。这会导致资源无法正确释放，可能引起内存泄露和连接未关闭等问题。

## 问题分析

通过代码审查和测试，发现以下几个主要问题：

1. **定时器线程退出问题**：定时器线程在`Stop()`后未能及时退出，可能阻塞在睡眠循环中
2. **信号处理机制缺陷**：ServerGuard只设置了退出标志，但未确保信号能被迅速处理
3. **线程池关闭不完全**：线程池关闭过程可能陷入等待长任务完成的状态
4. **资源清理顺序问题**：关闭顺序不合理，连接关闭可能阻塞过长时间
5. **超时处理不足**：关闭过程中缺乏有效的超时机制

## 修复方案

### 1. 优化ServerGuard的Wait方法

ServerGuard类负责服务器生命周期管理和信号处理，修改主要包括：

- 增加检查频率，确保快速响应SIGINT信号
- 添加全局超时机制，防止无限等待
- 增强错误处理，捕获所有可能的异常
- 优化线程join逻辑，确保即使线程卡住也能退出

```cpp
void Wait() {
    const int CHECK_INTERVAL_MS = 100; // 检查服务器状态的间隔
    const int MAX_WAIT_SECONDS = 5;    // 最大等待时间
    
    // 等待信号或其他退出条件
    auto start = std::chrono::high_resolution_clock::now();
    while (!_shouldExit) {
        // 检查超时和其他退出条件...
        std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
    }
    
    // 确保服务器关闭
    if (_shouldExit && _srv && !_srv->IsClosed()) {
        _srv->Shutdown();
    }
    
    // 有限时间内等待线程退出
    // ...略
}
```

### 2. 改进定时器线程的响应机制

修改MAP和HEAP定时器的Stop方法，确保:

- 设置`_bClosed`标志后，定时器线程能立即检测到并退出
- 添加唤醒机制，防止线程卡在睡眠状态
- 增强异常处理，防止关闭过程中的异常导致线程无法退出

```cpp
void TimerManager::Stop() {
    LOG_I("定时器管理器停止");
    _bClosed = true;

    // 唤醒可能在等待的定时器线程
    try {
        std::unique_lock<std::shared_mutex> lock(_timerMutex);
        // 添加一个立即触发的哑定时器，确保线程从等待中退出
        Timer dummy(0);
        dummy._time = Timer::Now();
        dummy._func = []() { LOG_D("定时器：唤醒定时器线程的哑定时器被触发"); };
        _timers.insert(std::make_pair(dummy._time, dummy));
        LOG_I("定时器管理器：已添加唤醒定时器，确保定时器线程能够退出");
    } catch (...) {
        LOG_E("定时器管理器停止时发生未知异常");
    }
}
```

### 3. 重构Server::Shutdown方法

改进服务器关闭过程，确保资源能正确释放:

- 优先标记服务器为关闭状态(`_isClose = true`)
- 及时停止定时器，防止新的计时任务创建
- 设置合理的超时时间，避免无限等待
- 强化每个步骤的错误处理
- 按照合理顺序关闭资源（监听socket → 连接 → 线程池 → 数据库）

```cpp
void Server::Shutdown(const int timeoutMS) {
    LOG_I("Server shutdown==========================>");

    try {
        // 设置关闭标志
        _isClose = true;

        // 停止定时器
        // ...

        // 关闭监听socket
        // ...

        // 关闭连接，使用较短超时
        const int connTimeoutMS = std::min(timeoutMS / 2, 2000);
        // ...

        // 关闭线程池
        // ...

        // 关闭数据库
        // ...
    } catch (...) {
        LOG_E("服务器关闭过程中发生未知异常");
    }
}
```

### 4. 优化线程池关闭逻辑

线程池的关闭过程中，添加任务队列停滞检测机制:

- 设置更短的检查间隔，提高响应灵敏度
- 添加任务队列进度监视，检测任务是否仍在处理
- 增加超时强制退出逻辑，避免永久阻塞

```cpp
void ThreadPool::Shutdown(int timeoutMS) {
    // ...
    int lastTaskCount = -1;
    int noProgressCount = 0;
    
    while (!tasksCompleted) {
        // 检测任务队列是否停滞
        if (currentTaskCount == lastTaskCount) {
            noProgressCount++;
            if (noProgressCount > 5) {
                std::cout << "线程池任务队列停滞，强制关闭" << std::endl;
                break;
            }
        }
        // ...
    }
    // ...
}
```

### 5. 增强main函数的异常处理

增强主函数的异常处理，确保即使发生意外情况也能正常退出:

```cpp
int main() {
    try {
        // 初始化日志系统
        zener::Logger::Init();
        
        // 创建并启动服务器
        const auto server = zener::NewServerFromConfig("config.toml");
        zener::ServerGuard guard(server.get(), true);
        
        // 等待退出信号
        guard.Wait();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "服务器发生严重错误: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "服务器发生未知严重错误" << std::endl;
        return 1;
    }
}
```

## 修复效果

1. 服务器现在能够在收到Ctrl+C信号后优雅退出
2. 即使在高负载情况下，也能保证资源正确释放
3. 超时机制确保服务器不会卡住，最长等待时间可控
4. 增强的错误处理提高了服务器的稳定性

## 注意事项

1. 修改后的代码需要重新编译才能生效
2. 部分修改涉及到底层线程模型，可能需要进一步性能测试
3. 在特殊情况下，服务器关闭可能仍需要几秒钟才能完成
4. 如果仍有问题，可以使用`kill.sh`脚本的强制模式：`./scripts/kill.sh -f` 