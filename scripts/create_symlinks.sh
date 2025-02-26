#!/bin/bash

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 获取项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

echo -e "${BLUE}创建可执行文件符号链接...${NC}"

# 确保bin目录存在
mkdir -p bin

# 为MAP版本创建符号链接
if [ -f "bin/Zener-map" ]; then
    if [ -L "bin/Zener_map" ]; then
        rm "bin/Zener_map"
    fi
    ln -sf "Zener-map" "bin/Zener_map"
    echo -e "${GREEN}✓ 创建符号链接: bin/Zener_map -> bin/Zener-map${NC}"
else
    echo -e "${RED}✗ 无法创建符号链接: bin/Zener-map 不存在${NC}"
fi

# 为HEAP版本创建符号链接
if [ -f "bin/Zener-heap" ]; then
    if [ -L "bin/Zener_heap" ]; then
        rm "bin/Zener_heap"
    fi
    ln -sf "Zener-heap" "bin/Zener_heap"
    echo -e "${GREEN}✓ 创建符号链接: bin/Zener_heap -> bin/Zener-heap${NC}"
else
    echo -e "${RED}✗ 无法创建符号链接: bin/Zener-heap 不存在${NC}"
fi

# 为默认版本创建符号链接
if [ -f "bin/Zener" ]; then
    if [ -L "bin/Zener_default" ]; then
        rm "bin/Zener_default"
    fi
    ln -sf "Zener" "bin/Zener_default"
    echo -e "${GREEN}✓ 创建符号链接: bin/Zener_default -> bin/Zener${NC}"
else
    echo -e "${YELLOW}! 跳过默认版本符号链接: bin/Zener 不存在${NC}"
fi

echo -e "${BLUE}符号链接创建完成${NC}" 