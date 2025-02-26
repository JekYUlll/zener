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
CONNECTIONS=1000
DURATION=10
URL_PATH="/"
START_SERVER=true

# 帮助函数
function show_help {
    echo -e "${CYAN}Zener服务器性能测试脚本${NC}"
    echo -e "${BLUE}用法: $0 [选项]${NC}"
    echo ""
    echo -e "选项:"
    echo -e "  -t, --timer <type>      定时器类型 (map 或 heap) [默认: map]"
    echo -e "  -p, --port <port>       服务器端口 [默认: 1316]"
    echo -e "  -c, --connections <num> 并发连接数 [默认: 1000]"
    echo -e "  -d, --duration <sec>    测试持续时间(秒) [默认: 10]"
    echo -e "  -u, --url <path>        请求URL路径 [默认: /]"
    echo -e "  -n, --no-start          不自动启动服务器(假设服务器已运行)"
    echo -e "  -h, --help              显示此帮助信息"
    echo ""
    echo -e "示例:"
    echo -e "  $0 -t map -c 5000 -d 30       运行MAP定时器版本压力测试,5000并发,30秒"
    echo -e "  $0 -t heap -p 8080 -n         在端口8080测试已运行的HEAP定时器版本"
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
        -c|--connections)
            CONNECTIONS="$2"
            shift 2
            ;;
        -d|--duration)
            DURATION="$2"
            shift 2
            ;;
        -u|--url)
            URL_PATH="$2"
            shift 2
            ;;
        -n|--no-start)
            START_SERVER=false
            shift
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

# 验证参数
if [[ "$TIMER_TYPE" != "map" && "$TIMER_TYPE" != "heap" ]]; then
    echo -e "${RED}错误: 定时器类型必须是 'map' 或 'heap'${NC}"
    exit 1
fi

if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    echo -e "${RED}错误: 端口必须是1-65535之间的数字${NC}"
    exit 1
fi

if ! [[ "$CONNECTIONS" =~ ^[0-9]+$ ]] || [ "$CONNECTIONS" -lt 1 ]; then
    echo -e "${RED}错误: 连接数必须是正整数${NC}"
    exit 1
fi

if ! [[ "$DURATION" =~ ^[0-9]+$ ]] || [ "$DURATION" -lt 1 ]; then
    echo -e "${RED}错误: 持续时间必须是正整数${NC}"
    exit 1
fi

# 检查webbench是否可用
if [ ! -f "./webbench-1.5/webbench" ]; then
    echo -e "${YELLOW}警告: webbench未找到, 请确保WebBench已安装${NC}"
    echo -e "${BLUE}可以通过以下命令安装WebBench:${NC}"
    echo -e "  git clone https://github.com/EZLippi/WebBench.git webbench-1.5"
    echo -e "  cd webbench-1.5 && make"
    exit 1
fi

# 确保结果目录存在
mkdir -p results

# 确定服务器可执行文件
EXECUTABLE="bin/Zener_${TIMER_TYPE}"
if [ ! -f "$EXECUTABLE" ] && [ "$START_SERVER" = true ]; then
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

# 服务器进程ID
SERVER_PID=""

# 清理函数
function cleanup {
    echo -e "\n${YELLOW}正在清理...${NC}"
    if [ -n "$SERVER_PID" ]; then
        echo -e "${BLUE}终止服务器进程 (PID: $SERVER_PID)${NC}"
        kill -15 "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    echo -e "${GREEN}清理完成${NC}"
}

# 设置退出处理
trap cleanup EXIT

# 启动服务器(如果需要)
if [ "$START_SERVER" = true ]; then
    echo -e "${GREEN}启动 ${TIMER_TYPE} 定时器版本服务器在端口 ${PORT}...${NC}"
    mkdir -p logs
    TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
    LOG_FILE="logs/zener_${TIMER_TYPE}_${PORT}_${TIMESTAMP}.log"
    
    # 启动服务器并放入后台
    "${PROJECT_ROOT}/${EXECUTABLE}" --port "$PORT" > "$LOG_FILE" 2>&1 &
    SERVER_PID=$!
    
    echo -e "${BLUE}服务器启动, PID: ${SERVER_PID}${NC}"
    echo -e "${BLUE}等待服务器初始化 (5秒)...${NC}"
    sleep 5
    
    # 检查服务器是否正在运行
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo -e "${RED}错误: 服务器启动失败, 查看日志: $LOG_FILE${NC}"
        exit 1
    fi
else
    echo -e "${BLUE}跳过服务器启动, 假设服务器已在端口 ${PORT} 运行${NC}"
fi

# 构建URL
TEST_URL="http://127.0.0.1:${PORT}${URL_PATH}"
RESULT_FILE="results/benchmark_${TIMER_TYPE}_c${CONNECTIONS}_d${DURATION}_$(date +"%Y%m%d_%H%M%S").txt"

# 运行基准测试
echo -e "${CYAN}开始性能测试:${NC}"
echo -e "  服务器类型:    ${BLUE}${TIMER_TYPE} 定时器${NC}"
echo -e "  URL:          ${BLUE}${TEST_URL}${NC}"
echo -e "  并发连接数:    ${BLUE}${CONNECTIONS}${NC}"
echo -e "  持续时间:      ${BLUE}${DURATION}秒${NC}"
echo -e "${YELLOW}正在运行测试...${NC}"

# 执行测试并保存结果
./webbench-1.5/webbench -c "$CONNECTIONS" -t "$DURATION" "$TEST_URL" | tee "$RESULT_FILE"

# 测试完成
echo -e "\n${GREEN}测试完成!${NC}"
echo -e "结果已保存到: ${BLUE}${RESULT_FILE}${NC}"

# 如果我们启动了服务器,现在可以停止它
if [ "$START_SERVER" = true ] && [ -n "$SERVER_PID" ]; then
    echo -e "${BLUE}停止服务器...${NC}"
    kill -15 "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    SERVER_PID=""
    echo -e "${GREEN}服务器已停止${NC}"
fi

# 显示服务器日志摘要(如果有)
if [ "$START_SERVER" = true ] && [ -f "$LOG_FILE" ]; then
    echo -e "\n${CYAN}服务器日志摘要:${NC}"
    echo -e "${YELLOW}----------------------------${NC}"
    tail -n 20 "$LOG_FILE"
    echo -e "${YELLOW}----------------------------${NC}"
    echo -e "完整日志: ${BLUE}${LOG_FILE}${NC}"
fi 