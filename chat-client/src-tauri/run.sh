#!/usr/bin/env bash
# ============================================================
# run.sh — Tauri 桌面客户端启动脚本（一键解决 GPU / 字体 / 显示问题）
# ============================================================
# 用法：
#   cd /home/wangt/ThreadPoolAction/chat-client/src-tauri
#   bash run.sh               # 开发模式（cargo run）
#   bash run.sh --build       # 先 cargo build 再跑 release 二进制
#   bash run.sh --no-gpu      # 强制 CPU 软件渲染（无视 /dev/dri）
# ============================================================
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_DIR"

# ----------- 环境变量默认值 -----------
export WEBKIT_FORCE_SANDBOX=0          # 容器/沙箱下 WebKit 沙箱通常不能正常隔离
export GSK_RENDERER=cairo              # GTK4：优先 Cairo 2D（不依赖 GL，最稳）
export GDK_BACKEND=x11                 # 统一走 X11，避免 Wayland 兼容问题
export WINIT_UNIX_BACKEND=x11          # winit 也统一走 X11

# ----------- GPU 自动检测 -----------
if [ "$1" = "--no-gpu" ] || [ ! -d /dev/dri ]; then
    echo "[run.sh] 无 GPU 直通 /dev/dri 或指定 --no-gpu，强制 CPU 软件渲染"
    export LIBGL_ALWAYS_SOFTWARE=1
    export GALLIUM_DRIVER=llvmpipe
    export WEBKIT_DISABLE_COMPOSITING_MODE=1
    export WEBKIT_HARDWARE_ACCELERATION_POLICY=never
else
    echo "[run.sh] 检测到 /dev/dri，尝试 GPU 加速；如仍黑屏可再加 --no-gpu"
    # 如果 GPU 仍然黑，取消下面两行注释：
    #export WEBKIT_DISABLE_COMPOSITING_MODE=1
    #export WEBKIT_HARDWARE_ACCELERATION_POLICY=on-demand
fi

# ----------- 中文字体检测 -----------
if ! fc-match "Noto Sans CJK SC":style=Regular >/dev/null 2>&1 \
   && ! fc-match "WenQuanYi Micro Hei":style=Regular >/dev/null 2>&1 \
   && ! fc-match "PingFang SC":style=Regular >/dev/null 2>&1; then
    echo
    echo "⚠  [run.sh] 未检测到中文字体，界面会显示豆腐字（方块）"
    echo "   请执行：apt install -y fonts-noto-cjk && fc-cache -f"
    echo
fi

# ----------- 执行模式 -----------
MODE="dev"
for a in "$@"; do case "$a" in
    --build)  MODE="release" ;;
    --no-gpu) ;;
    *) ;;
esac; done

echo "[run.sh] 目录: $PROJECT_DIR"
echo "[run.sh] 模式: $MODE"
echo "[run.sh] DISPLAY=$DISPLAY  WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
echo "------------------------------------------------------------"

if [ "$MODE" = "release" ]; then
    cargo build --release
    exec "$PROJECT_DIR/target/release/chat-client"
else
    exec cargo run
fi
