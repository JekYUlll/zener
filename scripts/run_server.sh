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

# 默认值
TIMER_TYPE="map"
PORT=1316

# 帮助函数
function show_help {
    echo -e "${CYAN}Zener服务器启动脚本${NC}"
    echo -e "${BLUE}用法: $0 [选项]${NC}"
    echo ""
    echo -e "选项:"
    echo -e "  -t, --timer <type>    定时器类型 (map 或 heap) [默认: map]"
    echo -e "  -p, --port <port>     服务器端口 [默认: 1316]"
    echo -e "  -h, --help            显示此帮助信息"
    echo ""
    echo -e "示例:"
    echo -e "  $0 -t map -p 1316     启动使用MAP定时器的服务器在端口1316"
    echo -e "  $0 -t heap -p 8080    启动使用HEAP定时器的服务器在端口8080"
    echo ""
    exit 0
}

# 参数解析
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--timer)
            TIMER_TYPE="$2"
            shift 2
            ;;
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            ;;
        *)
            echo -e "${RED}错误: 未知选项 $1${NC}"
            show_help
            ;;
    esac
done

# 转换为小写
TIMER_TYPE=$(echo "$TIMER_TYPE" | tr '[:upper:]' '[:lower:]')

# 验证参数
if [[ "$TIMER_TYPE" != "map" && "$TIMER_TYPE" != "heap" ]]; then
    echo -e "${RED}错误: 定时器类型必须是 'map' 或 'heap'${NC}"
    exit 1
fi

if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    echo -e "${RED}错误: 端口必须是1-65535之间的数字${NC}"
    exit 1
fi

# 确保bin目录存在
mkdir -p bin

# 检查可执行文件是否存在
EXECUTABLE="bin/Zener_${TIMER_TYPE}"
if [ ! -f "$EXECUTABLE" ]; then
    echo -e "${YELLOW}警告: ${EXECUTABLE} 不存在, 尝试构建...${NC}"
    
    if [ "$TIMER_TYPE" == "map" ]; then
        mkdir -p build_map
        cd build_map || exit 1
        cmake .. -DTIMER_IMPLEMENTATION=MAP
        make -j $(nproc)
        cp Zener "../bin/Zener_map" 2>/dev/null
    else
        mkdir -p build_heap
        cd build_heap || exit 1
        cmake .. -DTIMER_IMPLEMENTATION=HEAP
        make -j $(nproc)
        cp Zener "../bin/Zener_heap" 2>/dev/null
    fi
    
    cd "$PROJECT_ROOT" || exit 1
    
    if [ ! -f "$EXECUTABLE" ]; then
        echo -e "${RED}错误: 无法构建或找到 $EXECUTABLE${NC}"
        exit 1
    fi
fi

# 创建日志目录
mkdir -p logs

# 启动服务器
echo -e "${GREEN}启动Zener服务器:${NC}"
echo -e "  定时器类型: ${BLUE}${TIMER_TYPE}${NC}"
echo -e "  端口: ${BLUE}${PORT}${NC}"
echo -e "  可执行文件: ${BLUE}${EXECUTABLE}${NC}"
echo -e "${YELLOW}日志将保存在 logs/zener_${TIMER_TYPE}_${PORT}.log${NC}"
echo ""
echo -e "${PURPLE}按 Ctrl+C 停止服务器${NC}"
echo ""

# 使用当前时间作为日志前缀
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="logs/zener_${TIMER_TYPE}_${PORT}_${TIMESTAMP}.log"

# 默认参数+端口设置
"${PROJECT_ROOT}/${EXECUTABLE}" --port "$PORT" 2>&1 | tee "$LOG_FILE"

# 退出处理
echo ""
if [ $? -eq 0 ]; then
    echo -e "${GREEN}服务器已正常关闭${NC}"
else
    echo -e "${RED}服务器异常退出，错误代码: $?${NC}"
    echo -e "${YELLOW}查看日志了解详情: ${LOG_FILE}${NC}"
fi 