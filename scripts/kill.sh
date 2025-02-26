#!/bin/bash

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 默认值
KILL_ALL=false
TIMER_TYPE=""
PORT=""
FORCE_KILL=false
WAIT_TIME=0.5  # 缩短等待时间，从1秒减少到0.5秒

# 帮助函数
function show_help {
    echo -e "${CYAN}Zener服务器终止脚本${NC}"
    echo -e "${BLUE}用法: $0 [选项]${NC}"
    echo ""
    echo -e "选项:"
    echo -e "  -t, --timer <type>    终止指定定时器类型的服务器 (map 或 heap)"
    echo -e "  -p, --port <port>     终止指定端口的服务器"
    echo -e "  -a, --all             终止所有Zener服务器进程"
    echo -e "  -f, --force           强制终止进程 (直接使用SIGKILL信号)"
    echo -e "  -h, --help            显示此帮助信息"
    echo ""
    echo -e "示例:"
    echo -e "  $0 -t map             终止所有MAP定时器版本的服务器"
    echo -e "  $0 -p 1316            终止端口1316上的服务器"
    echo -e "  $0 -a                 终止所有Zener服务器进程"
    echo -e "  $0 -a -f              强制终止所有Zener服务器进程"
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
        -a|--all)
            KILL_ALL=true
            shift
            ;;
        -f|--force)
            FORCE_KILL=true
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

# 如果没有指定任何参数，显示帮助
if [ "$KILL_ALL" = false ] && [ -z "$TIMER_TYPE" ] && [ -z "$PORT" ]; then
    echo -e "${YELLOW}警告: 未指定任何终止条件${NC}"
    show_help
fi

# 终止函数 - 统一处理进程终止逻辑
function terminate_processes {
    local pids=$1
    local description=$2
    
    if [ -z "$pids" ]; then
        echo -e "${BLUE}未找到${description}进程${NC}"
        return
    fi
    
    echo -e "${RED}找到以下${description}进程:${NC}"
    ps -fp $pids
    
    # 如果指定了强制终止选项，则直接使用SIGKILL
    if [ "$FORCE_KILL" = true ]; then
        echo -e "${YELLOW}强制终止进程...${NC}"
        kill -9 $pids 2>/dev/null
    else
        echo -e "${YELLOW}正在尝试优雅终止进程...${NC}"
        kill -15 $pids 2>/dev/null
        
        # 等待短暂时间后检查进程状态
        sleep $WAIT_TIME
        
        # 检查每个PID是否还在运行
        for pid in $pids; do
            if ps -p $pid > /dev/null 2>&1; then
                echo -e "${YELLOW}进程 $pid 未响应SIGTERM，尝试SIGKILL...${NC}"
                kill -9 $pid 2>/dev/null
            fi
        done
    fi
    
    # 最终确认进程已终止
    sleep 0.2  # 短暂等待确认进程终止
    local remaining_pids=""
    for pid in $pids; do
        if ps -p $pid > /dev/null 2>&1; then
            remaining_pids="$remaining_pids $pid"
        fi
    done
    
    if [ -n "$remaining_pids" ]; then
        echo -e "${RED}警告: 无法终止以下进程: $remaining_pids${NC}"
        echo -e "${YELLOW}尝试最后一次强制终止...${NC}"
        kill -9 $remaining_pids 2>/dev/null
        sleep 0.2
        
        # 最终检查
        for pid in $remaining_pids; do
            if ps -p $pid > /dev/null 2>&1; then
                echo -e "${RED}无法终止进程 $pid，可能需要手动处理${NC}"
            fi
        done
    else
        echo -e "${GREEN}所有${description}进程已终止${NC}"
    fi
}

# 查找所有类型的Zener进程（改进匹配逻辑）
function find_zener_processes {
    # 匹配Zener、Zener-map、Zener-heap、Zener_map、Zener_heap等多种可能的格式
    pgrep -f "Zener"
}

# 终止所有Zener服务器
if [ "$KILL_ALL" = true ]; then
    echo -e "${YELLOW}正在终止所有Zener服务器进程...${NC}"
    PIDS=$(find_zener_processes)
    terminate_processes "$PIDS" "Zener服务器"
    exit 0
fi

# 根据定时器类型查找进程
if [ -n "$TIMER_TYPE" ]; then
    if [[ "$TIMER_TYPE" != "map" && "$TIMER_TYPE" != "heap" ]]; then
        echo -e "${RED}错误: 定时器类型必须是 'map' 或 'heap'${NC}"
        exit 1
    fi
    
    echo -e "${YELLOW}正在查找${TIMER_TYPE}定时器版本的服务器...${NC}"
    # 改进匹配逻辑，同时匹配-map和_map格式
    PIDS=$(pgrep -f "Zener[-_]${TIMER_TYPE}")
    terminate_processes "$PIDS" "${TIMER_TYPE}定时器版本的服务器"
fi

# 根据端口查找进程
if [ -n "$PORT" ]; then
    if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
        echo -e "${RED}错误: 端口必须是1-65535之间的数字${NC}"
        exit 1
    fi
    
    echo -e "${YELLOW}正在查找端口${PORT}上的服务器...${NC}"
    PIDS=$(lsof -i :$PORT -t)
    terminate_processes "$PIDS" "端口${PORT}上的服务器"
fi

echo -e "${GREEN}操作完成${NC}"