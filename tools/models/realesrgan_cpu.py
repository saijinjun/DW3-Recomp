#!/usr/bin/env python3
"""A CPU stand-in for realesrgan-ncnn-vulkan, for machines where that program can't run (no Vulkan GPU, or running
downloaded programs isn't allowed). It takes the same command line, so `texpack generate --upscaler` can point at it:

  realesrgan_cpu.py -i IN_DIR -o OUT_DIR -n RealESRGAN_General_x4_v3 -s 4 -m MODELS_DIR -f png [-g N] [-t TILE]

It runs the compact Real-ESRGAN network (SRVGGNetCompact: 3x3 convolutions with PReLU, a pixel shuffle and the input
added back, nearest-upscaled), reading the weights straight from the ncnn .param/.bin files the GPU program uses
(convert_realesrgan.py writes them; the official realesr-animevideov3 files have the same layout). Other model
layouts are refused. -g is accepted and ignored. Needs python3, numpy and Pillow; numpy's BLAS uses all cores.

It is several times slower than the GPU program: about 35,000 input pixels a second on four cores (-j, one process per
core by default).
"""
import argparse
import multiprocessing
import os
import sys
from pathlib import Path

# One image per process, each on one core: faster than one process with a threaded BLAS (the im2col copies are serial).
for _var in ("OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS"):
    os.environ.setdefault(_var, "1")

import numpy as np
from PIL import Image

PRE_PAD = 10   # border given to the network around each tile, as the GPU program does
FP16_TAG = 0x01306B47


class Model:
    def __init__(self, param: Path, weights: Path):
        lines = param.read_text().split("\n")
        if lines[0].strip() != "7767517":
            raise ValueError(f"{param}: not an ncnn param file")
        blob = weights.read_bytes()
        pos = 0

        def take(count, tagged):
            nonlocal pos
            if tagged:
                tag = int.from_bytes(blob[pos:pos + 4], "little")
                pos += 4
                if tag == FP16_TAG:
                    data = np.frombuffer(blob, np.float16, count, pos).astype(np.float32)
                    pos += 2 * count
                    pos += (-pos) % 4
                    return data
                if tag != 0:
                    raise ValueError(f"{weights}: unsupported weight storage {tag:#x}")
            data = np.frombuffer(blob, np.float32, count, pos).copy()
            pos += 4 * count
            return data

        # The body: conv, prelu, conv, prelu, ..., conv, then pixel shuffle and the input added back.
        self.layers = []   # (weights O x 9C, bias O, prelu slope O or None)
        self.scale = 0
        for line in lines[2:]:
            parts = line.split()
            if not parts:
                continue
            kind = parts[0]
            params = dict(p.split("=", 1) for p in parts[4 + int(parts[2]) + int(parts[3]):] if "=" in p)
            if kind in ("Input", "Split", "Interp", "BinaryOp"):
                continue
            if kind == "Convolution":
                out = int(params["0"])
                if params.get("1") != "3" or params.get("4", "0") != "1" or params.get("2", "1") != "1" or params.get("3", "1") != "1":
                    raise ValueError(f"{param}: only 3x3 stride 1 pad 1 convolutions are supported")
                count = int(params["6"])
                w = take(count, True).reshape(out, count // (out * 9), 3, 3)
                b = take(out, False) if params.get("5", "0") == "1" else np.zeros(out, np.float32)
                # Columns ordered (dy, dx, channel), the order the im2col below stacks them in.
                self.layers.append([np.ascontiguousarray(w.transpose(0, 2, 3, 1).reshape(out, -1)), b[:, None], None])
            elif kind == "PReLU":
                self.layers[-1][2] = (take(int(params["0"]), False) - 1.0)[:, None, None]
            elif kind == "PixelShuffle":
                self.scale = int(params["0"])
            else:
                raise ValueError(f"{param}: layer {kind} is not part of the compact Real-ESRGAN network")
        if self.scale == 0 or self.layers[0][0].shape[1] != 27 or self.layers[-1][0].shape[0] != 3 * self.scale ** 2:
            raise ValueError(f"{param}: not a compact Real-ESRGAN network")
        if pos != len(blob):
            raise ValueError(f"{weights}: {len(blob) - pos} bytes left over; the .bin does not match the .param")

    def run(self, rgb: np.ndarray) -> np.ndarray:
        """rgb: H x W x 3 float32 in 0..1. Returns (H*s) x (W*s) x 3."""
        h, w, _ = rgb.shape
        x = np.ascontiguousarray(rgb.transpose(2, 0, 1))
        for weights, bias, slope in self.layers:
            c = x.shape[0]
            padded = np.zeros((c, h + 2, w + 2), np.float32)
            padded[:, 1:-1, 1:-1] = x
            cols = np.empty((3, 3, c, h, w), np.float32)
            for dy in range(3):
                for dx in range(3):
                    cols[dy, dx] = padded[:, dy:dy + h, dx:dx + w]
            x = (weights @ cols.reshape(9 * c, h * w)) + bias
            x = x.reshape(-1, h, w)
            if slope is not None:
                x += slope * np.minimum(x, 0.0)
        s = self.scale
        out = x.reshape(3, s, s, h, w).transpose(3, 1, 4, 2, 0).reshape(h * s, w * s, 3)
        return out + np.repeat(np.repeat(rgb, s, axis=0), s, axis=1)


def upscale(model: Model, rgb: np.ndarray, tile: int) -> np.ndarray:
    """Upscales tile by tile; each tile is run with PRE_PAD pixels of the image around it (none past the image's edges,
    where the GPU program repeats the edge pixels: callers that care give their own border and crop it)."""
    h, w, _ = rgb.shape
    s = model.scale
    out = np.empty((h * s, w * s, 3), np.float32)
    for y in range(0, h, tile):
        for x in range(0, w, tile):
            th, tw = min(tile, h - y), min(tile, w - x)
            y0, x0 = max(0, y - PRE_PAD), max(0, x - PRE_PAD)
            y1, x1 = min(h, y + th + PRE_PAD), min(w, x + tw + PRE_PAD)
            big = model.run(rgb[y0:y1, x0:x1])
            out[y * s:(y + th) * s, x * s:(x + tw) * s] = big[(y - y0) * s:(y - y0 + th) * s, (x - x0) * s:(x - x0 + tw) * s]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("-i", required=True, help="input folder (or one image)")
    ap.add_argument("-o", required=True, help="output folder (or one image)")
    ap.add_argument("-n", default="RealESRGAN_General_x4_v3", help="model name")
    ap.add_argument("-s", type=int, default=0, help="scale; must be the model's")
    ap.add_argument("-m", default="models", help="folder with the model's .param and .bin")
    ap.add_argument("-f", default="png", help="output format (png only)")
    ap.add_argument("-g", default=None, help="ignored (GPU choice of the GPU program)")
    ap.add_argument("-t", type=int, default=192, help="tile size in input pixels (0 = 192)")
    ap.add_argument("-j", type=int, default=0, help="processes (default: one per core)")
    args = ap.parse_args()

    models = Path(args.m)
    try:
        model = Model(models / f"{args.n}.param", models / f"{args.n}.bin")
    except (OSError, ValueError) as e:
        print(f"realesrgan_cpu: {e}", file=sys.stderr)
        return 1
    if args.s and args.s != model.scale:
        print(f"realesrgan_cpu: {args.n} upscales {model.scale}x, not {args.s}x", file=sys.stderr)
        return 1
    if args.f != "png":
        print("realesrgan_cpu: only png output is supported", file=sys.stderr)
        return 1
    tile = args.t if args.t > 0 else 192

    src, dst = Path(args.i), Path(args.o)
    if src.is_dir():
        dst.mkdir(parents=True, exist_ok=True)
        jobs = [(p, dst / (p.stem + ".png")) for p in sorted(src.iterdir()) if p.suffix.lower() in (".png", ".jpg", ".jpeg", ".webp")]
    else:
        jobs = [(src, dst)]
    global _model, _tile
    _model, _tile = model, tile
    workers = args.j if args.j > 0 else (os.cpu_count() or 1)
    if workers > 1 and len(jobs) > 1:
        with multiprocessing.get_context("fork").Pool(min(workers, len(jobs))) as pool:
            errors = [e for e in pool.imap_unordered(_one, jobs, chunksize=4) if e]
    else:
        errors = [e for e in map(_one, jobs) if e]
    for e in errors:
        print(f"realesrgan_cpu: {e}", file=sys.stderr)
    return 1 if errors else 0


_model = None
_tile = 192


def _one(job) -> str:
    path, out_path = job
    try:
        with Image.open(path) as img:
            rgb = np.asarray(img.convert("RGB"), np.float32) / 255.0
        big = upscale(_model, rgb, _tile)
        pixels = np.clip(big * 255.0 + 0.5, 0, 255).astype(np.uint8)
        Image.fromarray(pixels, "RGB").save(out_path, compress_level=1)
    except OSError as e:
        return f"{path}: {e}"
    return ""


if __name__ == "__main__":
    sys.exit(main())
