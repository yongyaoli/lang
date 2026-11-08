#!/usr/bin/env bash
# ========================================================
# Linux & macOS (Unix) 一键自动安装脚本
# 自动编译/安装二进制文件 l，并自动配置 PATH 环境变量
# ========================================================

set -e

# ANSI 颜色
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}   L 语言 Linux & macOS 一键自动安装程序   ${NC}"
echo -e "${CYAN}========================================${NC}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# 1. 查找或编译 l 可执行文件
TARGET_BIN=""
if [ -f "$ROOT_DIR/l" ]; then
    TARGET_BIN="$ROOT_DIR/l"
    echo -e "${GREEN}[1/3] 找到现有的 l 可执行文件。${NC}"
elif command -v gcc >/dev/null 2>&1; then
    echo -e "${YELLOW}[1/3] 正在使用 gcc 编译 l...${NC}"
    cd "$ROOT_DIR"
    gcc -O2 -Wall -Wextra -I. lexer.c ast.c parser.c value.c gc.c interp.c builtins.c stdlib/*.c main.c -o l
    TARGET_BIN="$ROOT_DIR/l"
elif command -v clang >/dev/null 2>&1; then
    echo -e "${YELLOW}[1/3] 正在使用 clang 编译 l...${NC}"
    cd "$ROOT_DIR"
    clang -O2 -Wall -Wextra -I. lexer.c ast.c parser.c value.c gc.c interp.c builtins.c stdlib/*.c main.c -o l
    TARGET_BIN="$ROOT_DIR/l"
else
    echo "错误: 未检测到 gcc 或 clang 编译器，且未找到预编译二进制文件！"
    exit 1
fi

chmod +x "$TARGET_BIN"

# 2. 确定安装路径
INSTALL_DIR=""
USE_SUDO=0

if [ -w "/usr/local/bin" ]; then
    INSTALL_DIR="/usr/local/bin"
elif [ "$EUID" -eq 0 ]; then
    INSTALL_DIR="/usr/local/bin"
elif command -v sudo >/dev/null 2>&1; then
    # 尝试询问或测试 sudo 是否可用
    echo -e "${YELLOW}[2/3] 尝试安装到系统全局目录 /usr/local/bin (需要 sudo 权限)...${NC}"
    if sudo -n true 2>/dev/null || sudo -v 2>/dev/null; then
        INSTALL_DIR="/usr/local/bin"
        USE_SUDO=1
    fi
fi

# 如果无法使用全局目录，则安装到用户本地目录 ~/.local/bin
if [ -z "$INSTALL_DIR" ]; then
    INSTALL_DIR="$HOME/.local/bin"
    echo -e "${YELLOW}[2/3] 将安装到当前用户目录: $INSTALL_DIR${NC}"
    mkdir -p "$INSTALL_DIR"
fi

echo -e "${YELLOW}正在将程序复制到 $INSTALL_DIR/l ...${NC}"
if [ "$USE_SUDO" -eq 1 ]; then
    sudo cp -f "$TARGET_BIN" "$INSTALL_DIR/l"
    sudo chmod +x "$INSTALL_DIR/l"
else
    cp -f "$TARGET_BIN" "$INSTALL_DIR/l"
    chmod +x "$INSTALL_DIR/l"
fi

# 3. 环境变量 PATH 检查与自动补齐
echo -e "${YELLOW}[3/3] 正在检查 PATH 环境变量...${NC}"

if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    # 需将 INSTALL_DIR 添加到用户的 shell 配置中
    RC_FILES=()
    [ -f "$HOME/.bashrc" ] && RC_FILES+=("$HOME/.bashrc")
    [ -f "$HOME/.zshrc" ] && RC_FILES+=("$HOME/.zshrc")
    [ -f "$HOME/.profile" ] && RC_FILES+=("$HOME/.profile")

    if [ ${#RC_FILES[@]} -eq 0 ]; then
        # 默认创建 .profile
        RC_FILES=("$HOME/.profile")
    fi

    EXPORT_LINE="export PATH=\"\$PATH:$INSTALL_DIR\""
    for rc in "${RC_FILES[@]}"; do
        if ! grep -q "$INSTALL_DIR" "$rc" 2>/dev/null; then
            echo "" >> "$rc"
            echo "# L Language PATH" >> "$rc"
            echo "$EXPORT_LINE" >> "$rc"
            echo -e "${GREEN}已将 PATH 写入: $rc${NC}"
        fi
    done
    export PATH="$PATH:$INSTALL_DIR"
else
    echo -e "${GREEN}$INSTALL_DIR 已在系统的 PATH 中。${NC}"
fi

echo -e "\n${GREEN}安装完成！${NC}"
echo "----------------------------------------"
"$INSTALL_DIR/l" --version
echo "----------------------------------------"
echo -e "${CYAN}现在可以在终端中直接输入 'l' 运行交互式命令行，或者输入 'l 脚本.l' 执行脚本！${NC}\n"
