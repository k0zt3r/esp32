"""Convert existing Neon animation frames to OLED firmware bitmaps (Pillow required)."""
from pathlib import Path
from PIL import Image, ImageChops, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
sources = sorted((ROOT / "ThermoDashboard/assets/neon/idle").glob("*.png"))
if not sources:
    raise SystemExit("Missing source Neon frames")
frames = [Image.open(path).convert("RGBA") for path in sources]
boxes = [frame.getchannel("A").getbbox() for frame in frames]
box = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
bitmaps = []
for frame in frames:
    sprite = frame.crop(box)
    sprite.thumbnail((58, 60), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (64, 64))
    canvas.paste(sprite, ((64-sprite.width)//2, 62-sprite.height))
    alpha = canvas.getchannel("A").point(lambda a: 255 if a >= 100 else 0)
    # Preserve silhouette and the bright eyes/jacket against an unlit OLED.
    outline = ImageChops.subtract(alpha, alpha.filter(ImageFilter.MinFilter(3)))
    bitmap = Image.new("1", (64, 64))
    for y in range(64):
        for x in range(64):
            r, g, b, a = canvas.getpixel((x, y))
            bright = (g > 75 and g > r * 1.45) or min(r, g, b) > 170
            bitmap.putpixel((x, y), bool(alpha.getpixel((x, y)) and (bright or outline.getpixel((x, y)))))
    bitmaps.append(bitmap)
rows = []
for bitmap in bitmaps:
    packed = bitmap.tobytes()
    assert len(packed) == 512
    rows.append("  {\n    " + ",\n    ".join(", ".join(f"0x{v:02x}" for v in packed[i:i+16]) for i in range(0, 512, 16)) + "\n  }")
header = "#pragma once\n#include <Arduino.h>\n// Generated from existing Neon idle frames by tools/pack_neon.py.\n"
header += f"constexpr uint8_t NEON_FRAME_COUNT = {len(rows)};\nconst uint8_t NEON_FRAMES[{len(rows)}][512] PROGMEM = {{\n" + ",\n".join(rows) + "\n};\n"
(ROOT / "ThermoDashboard/neon_frames.h").write_text(header, encoding="ascii")
preview = ROOT / ".preview/neon"
preview.mkdir(parents=True, exist_ok=True)
bitmaps[0].resize((384, 384), Image.Resampling.NEAREST).save(preview / "neon.png")
bitmaps[0].resize((256,256), Image.Resampling.NEAREST).save(preview / "neon.gif", save_all=True,
    append_images=[b.resize((256,256), Image.Resampling.NEAREST) for b in bitmaps[1:]], duration=180, loop=0)
print(f"Neon: {len(rows)} frames, 64x64, {len(rows)*512} bytes in flash")
