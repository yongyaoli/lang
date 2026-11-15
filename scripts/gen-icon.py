# -*- coding: utf-8 -*-
"""生成 L 语言图标: 圆角方形渐变底 + 白色几何粗体 L
输出: assets/l.ico (16/24/32/48/64/128/256) + assets/l.png (256 预览)
几何定义以 100x100 viewBox 为基准, 与 editors/vscode/icons/l.svg 保持一致"""
from PIL import Image, ImageDraw
import os

ROOT = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(ROOT, "assets")
os.makedirs(ASSETS, exist_ok=True)

# 100x100 基准几何
BG_BOX = (4, 4, 96, 96)      # 背景圆角方形
BG_RADIUS = 20
STEM = (30, 18, 46, 82)      # L 竖笔: x30-46, y18-82
FOOT = (30, 66, 72, 82)      # L 底笔: x30-72, y66-82
TOP_COLOR = (79, 142, 247)   # #4F8EF7
BOT_COLOR = (36, 86, 214)    # #2456D6

SS = 4  # 4x 超采样抗锯齿


def draw_at(size):
    s = size / 100.0
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # 背景垂直渐变 (逐行插值)
    x0, y0, x1, y1 = [round(v * s) for v in BG_BOX]
    r = BG_RADIUS * s
    grad = Image.new("RGBA", (x1 - x0 + 1, y1 - y0 + 1), (0, 0, 0, 0))
    for yy in range(grad.height):
        t = yy / max(1, grad.height - 1)
        c = tuple(round(a + (b - a) * t) for a, b in zip(TOP_COLOR, BOT_COLOR)) + (255,)
        for_line = Image.new("RGBA", (grad.width, 1), c)
        grad.paste(for_line, (0, yy))
    mask = Image.new("L", grad.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, grad.width - 1, grad.height - 1], radius=round(r), fill=255)
    img.paste(grad, (x0, y0), mask)
    # 白色 L (竖笔 + 底笔, 外角直角保持字重感)
    box = lambda b: [round(v * s) * SS // 1 for v in b]
    d.rectangle([round(STEM[0]*s), round(STEM[1]*s), round(STEM[2]*s), round(STEM[3]*s)], fill=(255, 255, 255, 255))
    d.rectangle([round(FOOT[0]*s), round(FOOT[1]*s), round(FOOT[2]*s), round(FOOT[3]*s)], fill=(255, 255, 255, 255))
    return img.resize((size, size), Image.LANCZOS)


sizes = [256, 128, 64, 48, 32, 24, 16]
frames = [draw_at(sz) for sz in sizes]
frames[0].save(os.path.join(ASSETS, "l.ico"), format="ICO", sizes=[(sz, sz) for sz in sizes], append_images=frames[1:])
frames[0].save(os.path.join(ASSETS, "l.png"), format="PNG")
print("OK:", os.listdir(ASSETS))
