#!/bin/bash

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 获取项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

# 欢迎信息
echo -e "${CYAN}欢迎使用Zener定时器实现构建脚本${NC}"
echo -e "${BLUE}此脚本将构建MAP和HEAP两种定时器实现的服务器版本${NC}"
echo -e "项目根目录: ${PROJECT_ROOT}"
echo ""

# 确保bin目录存在
mkdir -p bin

# 构建MAP版本
echo -e "${YELLOW}开始构建MAP定时器版本...${NC}"
mkdir -p build_map
cd build_map || exit 1
cmake .. -DTIMER_IMPLEMENTATION=MAP -DCMAKE_BUILD_TYPE=Release
make -j $(nproc)

if [ $? -eq 0 ]; then
    echo -e "${GREEN}MAP版本构建成功!${NC}"
else
    echo -e "${RED}MAP版本构建失败!${NC}"
fi

# 返回项目根目录
cd "$PROJECT_ROOT" || exit 1

# 构建HEAP版本
echo -e "${YELLOW}开始构建HEAP定时器版本...${NC}"
mkdir -p build_heap
cd build_heap || exit 1
cmake .. -DTIMER_IMPLEMENTATION=HEAP -DCMAKE_BUILD_TYPE=Release
make -j $(nproc)

if [ $? -eq 0 ]; then
    echo -e "${GREEN}HEAP版本构建成功!${NC}"
else
    echo -e "${RED}HEAP版本构建失败!${NC}"
fi

# 返回项目根目录
cd "$PROJECT_ROOT" || exit 1

# 创建符号链接以确保向后兼容性
echo -e "${YELLOW}创建符号链接以确保向后兼容性...${NC}"
bash ./scripts/create_symlinks.sh

# 总结
echo ""
echo -e "${CYAN}构建摘要:${NC}"
if [ -f "bin/Zener-map" ]; then
    echo -e "${GREEN}✓ MAP版本可执行文件: bin/Zener-map${NC}"
    if [ -L "bin/Zener_map" ]; then
        echo -e "${GREEN}✓ MAP版本符号链接: bin/Zener_map${NC}"
    fi
else
    echo -e "${RED}✗ MAP版本构建不完整${NC}"
fi

if [ -f "bin/Zener-heap" ]; then
    echo -e "${GREEN}✓ HEAP版本可执行文件: bin/Zener-heap${NC}"
    if [ -L "bin/Zener_heap" ]; then
        echo -e "${GREEN}✓ HEAP版本符号链接: bin/Zener_heap${NC}"
    fi
else
    echo -e "${RED}✗ HEAP版本构建不完整${NC}"
fi

echo ""
echo -e "${PURPLE}使用以下命令运行服务器:${NC}"
echo -e "${GREEN}./bin/Zener-map${NC} 或 ${GREEN}./bin/Zener_map${NC} - 运行MAP定时器版本"
echo -e "${GREEN}./bin/Zener-heap${NC} 或 ${GREEN}./bin/Zener_heap${NC} - 运行HEAP定时器版本"
echo -e "${BLUE}或使用 ./scripts/run_server.sh 脚本选择版本运行${NC}"
echo "" 