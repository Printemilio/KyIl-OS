from __future__ import annotations

from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from matplotlib.patches import PathPatch
from svgpath2mpl import parse_path


ROOT = Path(__file__).resolve().parents[1]
KERNEL_SRC = ROOT / "kernel" / "src"
BUILD_DIR = ROOT / "build"
WALLPAPER_SIZE = (1152, 648)
ICON_SIZE = 64
WORDMARK_WIDTH = 300
SVG_VIEWBOX = (0.0, 0.0, 45.0, 11.0)
TEXT_GRADIENT = ((129, 140, 248), (167, 139, 250), (192, 132, 252), (96, 165, 250))
ICON_GRADIENT = ((232, 121, 249), (147, 51, 234), (67, 56, 202))
SVG_PATHS = [
    ("M43.3274 3.45446C43.2763 3.02264 43.0689 2.68741 42.7053 2.44878C42.3416 2.21014 41.8956 2.09082 41.3672 2.09082C40.9808 2.09082 40.6428 2.15332 40.353 2.27832C40.066 2.40332 39.8416 2.5752 39.6797 2.79395C39.5206 3.0127 39.441 3.26127 39.441 3.53968C39.441 3.77264 39.4964 3.97292 39.6072 4.14054C39.7209 4.30531 39.8658 4.44309 40.0419 4.55389C40.218 4.66184 40.4027 4.75133 40.5959 4.82235C40.7891 4.89054 40.9666 4.94593 41.1285 4.98855L42.0149 5.22718C42.2422 5.28684 42.495 5.36923 42.7734 5.47434C43.0547 5.57946 43.3232 5.72292 43.5788 5.90474C43.8374 6.08372 44.0504 6.31383 44.218 6.59508C44.3857 6.87633 44.4695 7.2215 44.4695 7.63059C44.4695 8.10218 44.3459 8.52832 44.0987 8.909C43.8544 9.28968 43.4964 9.59224 43.0249 9.81667C42.5561 10.0411 41.9865 10.1533 41.316 10.1533C40.691 10.1533 40.1499 10.0525 39.6925 9.85076C39.2379 9.64906 38.88 9.36781 38.6186 9.00701C38.3601 8.64622 38.2138 8.22718 38.1797 7.74991H39.2706C39.299 8.07946 39.4098 8.35218 39.603 8.56809C39.799 8.78116 40.0462 8.94025 40.3445 9.04537C40.6456 9.14764 40.9695 9.19878 41.316 9.19878C41.7195 9.19878 42.0817 9.13343 42.4027 9.00275C42.7237 8.86923 42.978 8.68457 43.1655 8.44878C43.353 8.21014 43.4467 7.93173 43.4467 7.61355C43.4467 7.32377 43.3658 7.08798 43.2038 6.90616C43.0419 6.72434 42.8288 6.57662 42.5646 6.46298C42.3004 6.34934 42.0149 6.24991 41.7081 6.16468L40.6342 5.85787C39.9524 5.66184 39.4126 5.38201 39.0149 5.01838C38.6172 4.65474 38.4183 4.17889 38.4183 3.59082C38.4183 3.10218 38.5504 2.67605 38.8146 2.31241C39.0817 1.94593 39.4396 1.66184 39.8885 1.46014C40.3402 1.25559 40.8445 1.15332 41.4013 1.15332C41.9638 1.15332 42.4638 1.25417 42.9013 1.45588C43.3388 1.65474 43.6854 1.92747 43.941 2.27406C44.1996 2.62065 44.3359 3.01412 44.3501 3.45446H43.3274Z", "text"),
    ("M26.5242 5.16748V6.10498H22.7061V5.16748H26.5242Z", "text"),
    ("M16.5431 1.27271L18.7931 4.90339H18.8613L21.1113 1.27271H22.3556L19.6113 5.63634L22.3556 9.99998H21.1113L18.8613 6.43748H18.7931L16.5431 9.99998H15.2988L18.1113 5.63634L15.2988 1.27271H16.5431Z", "text"),
    ("M8.85938 9.99998V1.27271H14.1264V2.21021H9.91619V5.15907H13.8537V6.09657H9.91619V9.06248H14.1946V9.99998H8.85938Z", "text"),
    ("M1.10795 1.27271L3.69886 8.6193H3.80114L6.39205 1.27271H7.5L4.29545 9.99998H3.20455L0 1.27271H1.10795Z", "text"),
    ("M36.9561 2.75V8.25L32.1934 11L27.4307 8.25V2.75L32.1934 0L36.9561 2.75ZM28.4307 3.32715V7.67188L32.1934 9.84473L35.9561 7.67188V3.32715L32.1934 1.1543L28.4307 3.32715Z", "icon"),
]


def mix_color(stops: tuple[tuple[int, int, int], ...], t: float) -> tuple[int, int, int]:
    if t <= 0.0:
        return stops[0]
    if t >= 1.0:
        return stops[-1]

    scaled = t * (len(stops) - 1)
    index = int(scaled)
    local = scaled - index
    left = stops[index]
    right = stops[index + 1]
    return (
        int(left[0] + (right[0] - left[0]) * local),
        int(left[1] + (right[1] - left[1]) * local),
        int(left[2] + (right[2] - left[2]) * local),
    )


def draw_radial_gradient(base: Image.Image, center_x: float, center_y: float, radius_x: float,
                         radius_y: float, color: tuple[int, int, int], alpha: float) -> None:
    width, height = base.size
    overlay = Image.new("RGBA", base.size, (0, 0, 0, 0))
    pixels = overlay.load()

    for y in range(height):
        ny = (y - center_y) / radius_y
        for x in range(width):
            nx = (x - center_x) / radius_x
            distance = nx * nx + ny * ny
            if distance >= 1.0:
                continue
            strength = 1.0 - distance
            pixel_alpha = int(255 * alpha * strength)
            if pixel_alpha > 0:
                pixels[x, y] = (color[0], color[1], color[2], pixel_alpha)

    base.alpha_composite(overlay)


def gradient_array(stops: tuple[tuple[int, int, int], ...], width: int, height: int, vertical: bool) -> np.ndarray:
    arr = np.zeros((height, width, 4), dtype=np.float32)
    if vertical:
        for y in range(height):
            r, g, b = mix_color(stops, y / max(1, height - 1))
            arr[y, :, 0] = r / 255.0
            arr[y, :, 1] = g / 255.0
            arr[y, :, 2] = b / 255.0
            arr[y, :, 3] = 1.0
    else:
        for x in range(width):
            r, g, b = mix_color(stops, x / max(1, width - 1))
            arr[:, x, 0] = r / 255.0
            arr[:, x, 1] = g / 255.0
            arr[:, x, 2] = b / 255.0
            arr[:, x, 3] = 1.0
    return arr


def render_svg_logo(output_width: int) -> Image.Image:
    _, _, view_w, view_h = SVG_VIEWBOX
    output_height = max(1, round(output_width * view_h / view_w))
    dpi = 200
    fig = plt.figure(figsize=(output_width / dpi, output_height / dpi), dpi=dpi)
    fig.patch.set_alpha(0.0)
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, view_w)
    ax.set_ylim(view_h, 0)
    ax.axis("off")

    text_fill = gradient_array(TEXT_GRADIENT, 1600, 400, vertical=False)
    icon_fill = gradient_array(ICON_GRADIENT, 400, 1200, vertical=True)

    for path_d, kind in SVG_PATHS:
        path = parse_path(path_d)
        patch = PathPatch(path, facecolor="none", edgecolor="none", antialiased=True)
        ax.add_patch(patch)
        if kind == "text":
            image = ax.imshow(text_fill, extent=[-0.306762, 45.6932, 11.0, 0.0], interpolation="bicubic", origin="upper")
        else:
            image = ax.imshow(icon_fill, extent=[27.4307, 36.9561, 11.0, 0.0], interpolation="bicubic", origin="upper")
        image.set_clip_path(patch)

    BUILD_DIR.mkdir(exist_ok=True)
    temp_path = BUILD_DIR / "_vex_svg_logo.png"
    fig.savefig(temp_path, dpi=dpi, transparent=True, pad_inches=0)
    plt.close(fig)
    image = Image.open(temp_path).convert("RGBA")
    temp_path.unlink(missing_ok=True)
    return image


def write_c_array(path: Path, symbol: str, data: bytes, channels: int, width: int, height: int) -> None:
    values = ",".join(str(byte) for byte in data)
    suffix = "rgba" if channels == 4 else "rgb"
    path.write_text(
        f'#include <unos/{symbol}.h>\n\n'
        f'const unsigned int {symbol}_width = {width}u;\n'
        f'const unsigned int {symbol}_height = {height}u;\n'
        f'const unsigned char {symbol}_{suffix}[] = {{\n{values}\n}};\n',
        encoding="utf-8",
    )


def build_wallpaper() -> None:
    wallpaper = Image.new("RGBA", WALLPAPER_SIZE, (30, 14, 62, 255))
    draw_radial_gradient(wallpaper, WALLPAPER_SIZE[0] * 0.50, WALLPAPER_SIZE[1] * 0.50,
                         WALLPAPER_SIZE[0] * 0.75, WALLPAPER_SIZE[1] * 0.55, (139, 92, 246), 0.38)
    draw_radial_gradient(wallpaper, WALLPAPER_SIZE[0] * 0.15, WALLPAPER_SIZE[1] * 1.00,
                         WALLPAPER_SIZE[0] * 0.50, WALLPAPER_SIZE[1] * 0.45, (59, 130, 246), 0.22)
    draw_radial_gradient(wallpaper, WALLPAPER_SIZE[0] * 0.88, WALLPAPER_SIZE[1] * 0.05,
                         WALLPAPER_SIZE[0] * 0.40, WALLPAPER_SIZE[1] * 0.35, (217, 70, 239), 0.14)

    logo = render_svg_logo(WORDMARK_WIDTH)
    logo_x = (WALLPAPER_SIZE[0] - logo.width) // 2
    logo_y = (WALLPAPER_SIZE[1] - logo.height) // 2 - 8

    glow = Image.new("RGBA", WALLPAPER_SIZE, (0, 0, 0, 0))
    glow_draw = ImageDraw.Draw(glow)
    glow_draw.rounded_rectangle(
        (logo_x - 56, logo_y - 42, logo_x + logo.width + 56, logo_y + logo.height + 42),
        radius=48,
        fill=(134, 92, 246, 54),
    )
    glow = glow.filter(ImageFilter.GaussianBlur(34))
    wallpaper.alpha_composite(glow)
    wallpaper.alpha_composite(logo, (logo_x, logo_y))

    write_c_array(KERNEL_SRC / "vex_wallpaper.c", "vex_wallpaper", wallpaper.convert("RGB").tobytes(), 3,
                  WALLPAPER_SIZE[0], WALLPAPER_SIZE[1])


def build_icon() -> None:
    icon = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (0, 0, 0, 0))

    halo = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (0, 0, 0, 0))
    halo_draw = ImageDraw.Draw(halo)
    halo_draw.ellipse((6, 6, ICON_SIZE - 6, ICON_SIZE - 6), fill=(168, 85, 247, 120))
    halo = halo.filter(ImageFilter.GaussianBlur(10))
    icon.alpha_composite(halo)

    logo = render_svg_logo(240)
    left = round(27.4307 / 45.0 * logo.width)
    right = round(36.9561 / 45.0 * logo.width)
    glyph = logo.crop((left, 0, right, logo.height)).resize((40, 40), Image.Resampling.LANCZOS)
    icon.alpha_composite(glyph, ((ICON_SIZE - glyph.width) // 2, (ICON_SIZE - glyph.height) // 2))

    write_c_array(KERNEL_SRC / "vex_logo.c", "vex_logo", icon.tobytes(), 4, ICON_SIZE, ICON_SIZE)


def main() -> None:
    build_wallpaper()
    build_icon()
    print("generated vex brand assets")


if __name__ == "__main__":
    main()
