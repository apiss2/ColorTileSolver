"""Reproduce screenshot recognition checks; Pillow is a test-only dependency.

Build the driver with C++17, for example from the project directory:
    g++ -O2 -std=c++17 -Isrc tests/test_recognition.cpp src/recognition.cpp -o test_recognition
Then run:
    python tests/test_recognition.py --driver ./test_recognition
The Windows application itself does not need Python or Pillow.
"""
from __future__ import annotations

import argparse
from collections import Counter
from io import BytesIO
import json
from pathlib import Path
import subprocess
import tempfile

try:
    from PIL import Image, ImageOps
except ImportError as error:
    raise SystemExit("These optional tests require Pillow: python -m pip install Pillow") from error


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--example", type=Path,
                        default=Path(__file__).resolve().parents[1] / "examples" / "sample.png")
    parser.add_argument("--board", type=Path,
                        default=Path(__file__).resolve().parents[1] / "examples" / "sample.ctb")
    args = parser.parse_args()
    driver = str(args.driver.resolve())
    subprocess.run([driver, "--self-test"], check=True)
    values = [int(value) for value in args.board.read_text(encoding="utf-8-sig").split()]
    width, height, *expected = values
    assert (width, height) == (23, 15) and len(expected) == width * height
    assert Counter(expected) == Counter({0: 145, **{color: 20 for color in range(1, 11)}})

    source = Image.open(args.example).convert("RGB")
    cases = {"original": source}
    for scale in (0.5, 0.75, 1.25, 1.5, 2.0, 3.0):
        cases[f"scale_{scale}"] = source.resize(
            (round(source.width * scale), round(source.height * scale)), Image.Resampling.LANCZOS)
    cases["padded"] = ImageOps.expand(source, (173, 127, 89, 43), fill=(255, 255, 255))
    for quality in (95, 85, 70, 50):
        buffer = BytesIO()
        source.save(buffer, format="JPEG", quality=quality)
        cases[f"jpeg_{quality}"] = Image.open(BytesIO(buffer.getvalue())).convert("RGB")
    cases["crop"] = source.crop((31, 47, 606, 422))
    cases["nonuniform_scale"] = source.resize((961, 550), Image.Resampling.BICUBIC)
    cases["padded_scaled"] = ImageOps.expand(
        source.resize((801, 550), Image.Resampling.LANCZOS), (40, 75, 333, 101), fill=(32, 35, 39))

    with tempfile.TemporaryDirectory(prefix="colortile-recognition-") as folder:
        for name, image in cases.items():
            path = Path(folder) / f"{name}.ppm"
            image.save(path)
            result = json.loads(subprocess.check_output([driver, str(path)], text=True))
            assert result["ok"], f"Grid detection failed: {name}"
            differing = [i for i, (actual, target) in enumerate(zip(result["cells"], expected))
                         if actual != target]
            assert result["cells"] == expected, f"Incorrect cells in {name}: {differing}"
            print(f"{name}: all 345 cells match; {result['uncertain']} uncertain")
        blank = Path(folder) / "blank.ppm"
        Image.new("RGB", (641, 440), (250, 250, 250)).save(blank)
        result = json.loads(subprocess.check_output([driver, str(blank)], text=True))
        assert not result["ok"], "A blank image must not be accepted as a detected game board."
    print(f"Passed {len(cases)} screenshot cases and the blank-image rejection check.")


if __name__ == "__main__":
    main()
