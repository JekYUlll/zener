#!/bin/bash
# 批量替换中文日志消息的脚本

find src/http -type f -name "*.cpp" -exec sed -i 's/写入连接/Writing connection/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/处理连接/Processing connection/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/解析成功/Parsing successful/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/解析失败/Parsing failed/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/准备生成响应/Preparing to generate response/g' {} \;

# 文件缓存相关
find src/http -type f -name "*.cpp" -exec sed -i 's/文件缓存命中/File cache hit/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/文件已被修改，重新加载/File has been modified, reloading/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/移除过期文件缓存/Removing expired file cache/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/新增文件缓存/New file cache added/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/清理空闲文件缓存/Cleaning idle file cache/g' {} \;
find src/http -type f -name "*.cpp" -exec sed -i 's/跳过仍在使用的文件/Skipping file still in use/g' {} \;

# Server类中的日志
find src/core -type f -name "*.cpp" -exec sed -i 's/服务器关闭完成/Server shutdown completed/g' {} \;
find src/core -type f -name "*.cpp" -exec sed -i 's/开始执行定期文件缓存清理/Starting periodic file cache cleaning/g' {} \;
find src/core -type f -name "*.cpp" -exec sed -i 's/文件缓存清理完成/File cache cleaning completed/g' {} \;
find src/core -type f -name "*.cpp" -exec sed -i 's/文件缓存清理异常/File cache cleaning exception/g' {} \;

# ServerGuard类中的日志
find include -type f -name "*.h" -exec sed -i 's/服务器线程已退出或服务器已关闭/Server thread has exited or server is closed/g' {} \;
find include -type f -name "*.h" -exec sed -i 's/收到退出信号，正在关闭服务器/Exit signal received, shutting down server/g' {} \;
find include -type f -name "*.h" -exec sed -i 's/最大等待时间/Maximum wait time/g' {} \;

# 检查成功替换的数量
echo "替换完成。以下是剩余的中文日志消息:"
grep -r "LOG_[DIEWT].*[\u4e00-\u9fff]" --include="*.cpp" --include="*.h" src/ include/ 2>/dev/null || echo "没有找到中文日志消息！"
