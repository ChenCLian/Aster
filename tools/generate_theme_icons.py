"""Regenerate embedded UI indicators after changing theme palettes.
Development-only dependency: Pillow. Building/running Aster needs no Qt SVG module.
"""
from pathlib import Path
import json
from PIL import Image, ImageDraw

resources = Path(__file__).resolve().parents[1] / "resources"
for theme in ("light", "dark"):
    palette = json.loads((resources / "themes" / (theme + ".json")).read_text())
    target = resources / "icons" / theme
    target.mkdir(parents=True, exist_ok=True)
    for name, size, points, role in (
        ("down", (12, 8), [(2, 2), (6, 6), (10, 2)], "muted"),
        ("up", (12, 8), [(2, 6), (6, 2), (10, 6)], "muted"),
        ("check", (14, 14), [(3, 7), (6, 10), (11, 4)], "accentText"),
    ):
        scale = 8
        image = Image.new("RGBA", (size[0] * scale, size[1] * scale))
        draw = ImageDraw.Draw(image)
        draw.line([(x * scale, y * scale) for x, y in points], fill=palette[role], width=14, joint="curve")
        image.resize((size[0] * 4, size[1] * 4), Image.Resampling.LANCZOS).save(target / (name + ".png"))
