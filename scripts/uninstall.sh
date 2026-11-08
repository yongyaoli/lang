#!/usr/bin/env bash
# ========================================================
# Linux & macOS 卸载脚本
# ========================================================

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}正在卸载 L 语言...${NC}"

# 1. 检查 /usr/local/bin/l
if [ -f "/usr/local/bin/l" ]; then
    if [ -w "/usr/local/bin/l" ]; then
        rm -f "/usr/local/bin/l"
    else
        sudo rm -f "/usr/local/bin/l"
    fi
    echo -e "${GREEN}已删除: /usr/local/bin/l${NC}"
fi

# 2. 检查 ~/.local/bin/l
if [ -f "$HOME/.local/bin/l" ]; then
    rm -f "$HOME/.local/bin/l"
    echo -e "${GREEN}已删除: $HOME/.local/bin/l${NC}"
fi

echo -e "${GREEN}卸载完成！${NC}"
