#!/bin/bash

# 颜色设置
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # 无颜色

echo -e "${YELLOW}测试日志轮转功能${NC}"
echo "此脚本将生成大量日志消息以触发日志轮转功能"

# 检查二进制文件是否存在
if [ ! -f "./bin/Zener" ]; then
    echo -e "${RED}错误: 找不到服务器二进制文件 ./bin/Zener${NC}"
    echo "请先编译服务器"
    exit 1
fi

# 确保日志目录存在
mkdir -p logs

# 启动服务器
echo -e "${GREEN}启动Zener服务器...${NC}"
./bin/Zener &
SERVER_PID=$!

# 等待服务器完全启动
sleep 2

# 检查服务器是否正常运行
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}错误: 服务器启动失败${NC}"
    exit 1
fi

echo -e "${GREEN}服务器启动成功，PID: ${SERVER_PID}${NC}"
echo "开始生成大量日志..."

# 使用curl发送多个请求来生成日志
for i in {1..10000}; do
    # 发送请求到服务器
    curl -s "http://localhost:8080/api/v1/test?message=测试消息_$i" > /dev/null
    
    # 每100个请求显示一次进度
    if [ $((i % 100)) -eq 0 ]; then
        echo "已发送 $i 个请求..."
    fi
    
    # 检查服务器是否仍在运行
    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}错误: 服务器意外停止${NC}"
        exit 1
    fi
done

echo -e "${GREEN}成功发送10000个请求${NC}"

# 检查日志文件
echo "检查日志文件..."
LOG_COUNT=$(ls -l logs/server*.log 2>/dev/null | wc -l)

if [ "$LOG_COUNT" -gt 1 ]; then
    echo -e "${GREEN}测试成功: 检测到 $LOG_COUNT 个日志文件，日志轮转工作正常${NC}"
    ls -lh logs/server*.log
else
    echo -e "${YELLOW}日志轮转可能未触发，只发现 $LOG_COUNT 个日志文件${NC}"
    echo "可能需要发送更多请求或检查轮转大小设置"
    ls -lh logs/server*.log
fi

# 停止服务器
echo "停止服务器..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo -e "${GREEN}测试完成${NC}"
exit 0 