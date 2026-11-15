# -*- coding: utf-8 -*-
"""生成 L 语言图标: 圆角方形渐变底 + 白色几何粗体 L
输出: assets/l.ico (16/24/32/48/64/128/256) + assets/l.png (256 预览)
几何定义以 100x100 viewBox 为基准, 与 editors/vscode/icons/l.svg 保持一致

实现说明: 先在 size*SS (SS=4 超采样) 的画布上绘制, 所有坐标统一按
k = size*SS/100 缩放 (100 基准坐标系 → 实际画布), 最后 LANCZOS 缩回
size, 兼得抗锯齿与正确的满幅布局。"""
from PIL import Image, ImageDraw
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(SCRIPT_DIR)
ASSETS = os.path.join(ROOT, "assets")
os.makedirs(ASSETS, exist_ok=True)

# 100x100 基准几何
BG_BOX = (2, 2, 98, 98)      # 背景圆角方形 (近满幅)
BG_RADIUS = 22
STEM = (22, 12, 48, 88)      # L 竖笔: x22-48 (宽26), y12-88
FOOT = (22, 62, 80, 88)      # L 底笔: x22-80 (长58), y62-88 (高26)
TOP_COLOR = (79, 142, 247)   # #4F8EF7
BOT_COLOR = (36, 86, 214)    # #2456D6
SS = 4                       # 4x 超采样抗锯齿


def draw_at(size):
    """在 size*SS 画布上按 100 基准几何绘制, 返回缩回 size 的图像"""
    canvas = size * SS
    k = canvas / 100.0
    box = lambda b: [round(v * k) for v in b]

    img = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    x0, y0, x1, y1 = box(BG_BOX)

    # 背景垂直渐变 (逐行插值) + 圆角蒙版
    grad = Image.new("RGBA", (x1 - x0 + 1, y1 - y0 + 1), (0, 0, 0, 0))
    for yy in range(grad.height):
        t = yy / max(1, grad.height - 1)
        c = tuple(round(a + (b - a) * t) for a, b in zip(TOP_COLOR, BOT_COLOR)) + (255,)
        grad.paste(Image.new("RGBA", (grad.width, 1), c), (0, yy))
    mask = Image.new("L", grad.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, grad.width - 1, grad.height - 1],
        radius=round(BG_RADIUS * k), fill=255)
    img.paste(grad, (x0, y0), mask)

    # 白色 L (竖笔 + 底笔)
    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    d.rectangle(box(STEM), fill=white)
    d.rectangle(box(FOOT), fill=white)
    return img.resize((size, size), Image.LANCZOS)


sizes = [256, 128, 64, 48, 32, 24, 16]
frames = [draw_at(sz) for sz in sizes]
frames[0].save(os.path.join(ASSETS, "l.ico"), format="ICO",
               sizes=[(sz, sz) for sz in sizes])
frames[0].save(os.path.join(ASSETS, "l.png"), format="PNG")
print("OK:", os.listdir(ASSETS))
