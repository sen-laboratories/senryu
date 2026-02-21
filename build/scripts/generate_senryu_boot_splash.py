#!/usr/bin/env python

import sys, os, re
from PIL import Image, ImageEnhance

# 1024x768 Scaled BBoxes for SEN, ryu and boot stage sections
SPLASH_WIDTH = 1024
SPLASH_HEIGHT = 768
SEN_BBOX   = (0,   0, 226,  112)    # Top-left part of the logo
RYU_BBOX   = (230, 0, 406,  182)    # Bottom-right part of the logo
STAGE_BBOX = (482, 0, 1024, 388)    # boot stage wave and glowing icons

# SEN-16 Palette (Extended Neon)
sen_palette = [
    0, 0, 0,        # 0: Pure Black
    47, 47, 47,     # 1: Dark Grey
    80, 80, 80,     # 2: Mid Grey
    120, 180, 255,  # 3: Primary Blue (Neon)
    60, 90, 180,    # 4: Deep Blue (Gradient)
    46, 191, 212,   # 5: Scooter (Teal)
    20, 100, 110,   # 6: Deep Teal
    244, 244, 244,  # 7: Wild Sand (White-ish)
    247, 242, 225,  # 8: Cream
    255, 51, 204,   # 9: Razzle Rose (Magenta)
    150, 0, 100,    # 10: Deep Rose
    100, 255, 100,  # 11: Accent Green (optional)
    255, 165, 0,    # 12: Accent Orange (optional)
    200, 200, 200,  # 13: Light Grey
    30, 30, 60,     # 14: Midnight Blue (Shadows)
    255, 255, 255   # 15: Pure White
]
# Pad remaining to 256 for PIL
sen_palette += [0] * (768 - len(sen_palette))

def generate_neon_logo(input_path, out_dir):
    base_img = Image.open(input_path).convert("RGB").resize((SPLASH_WIDTH, SPLASH_HEIGHT), Image.Resampling.LANCZOS)
    p_img = Image.new('P', (1,1)); p_img.putpalette(sen_palette)

    # Process both parts
    for part_name, bbox in [("sen", SEN_BBOX), ("ryu", RYU_BBOX)]:
        crop = base_img.crop(bbox)
        for state, level in [("cold", 0.3), ("warm", 1.0), ("vibrant", 1.5), ("overdrive", 3.5)]:
            enhancer = ImageEnhance.Brightness(crop)
            final = enhancer.enhance(level).quantize(palette=p_img)
            final.save(os.path.join(out_dir, f"{part_name}_{state}.png"))

def generate_neon_states(input_path, out_dir):
    idx_match = re.search(r'_(\d+)\.png$', input_path)
    index = int(idx_match.group(1)) if idx_match else 0

    # Load and scale
    base_img = Image.open(input_path).convert("RGB").resize((1024, 768), Image.Resampling.LANCZOS)
    p_img = Image.new('P', (1,1)); p_img.putpalette(sen_palette)

    # If it's the background, just save one version and generate the logo states once
    if index == 0:
        final = base_img.quantize(palette=p_img, dither=Image.Dither.FLOYDSTEINBERG)
        final.save(os.path.join(out_dir, "stage_0.png"))
        generate_neon_logo(input_path, out_dir)
        return

    # For stages, create 4 luminosity levels
    crop = base_img.crop(STAGE_BBOX)

    # 1. Warm (Standard)
    warm = crop.quantize(palette=p_img, dither=Image.Dither.FLOYDSTEINBERG)
    warm.save(os.path.join(out_dir, f"stage_{index}_warm.png"))

    # 2. Cold (Dimmed)
    cold_enhancer = ImageEnhance.Brightness(crop)
    cold = cold_enhancer.enhance(0.4).quantize(palette=p_img, dither=Image.Dither.FLOYDSTEINBERG)
    cold.save(os.path.join(out_dir, f"stage_{index}_cold.png"))

    # 3. Vibrant (Slightly Over-bright)
    vib_enhancer = ImageEnhance.Brightness(crop)
    vibrant = vib_enhancer.enhance(1.4).quantize(palette=p_img, dither=Image.Dither.FLOYDSTEINBERG)
    vibrant.save(os.path.join(out_dir, f"stage_{index}_vibrant.png"))

    # 4. Overdrive (The Fade-to-White transition)
    ovr_enhancer = ImageEnhance.Brightness(crop)
    # Push brightness to 3.0+ to wash out the neon into white
    overdrive = ovr_enhancer.enhance(3.5).quantize(palette=p_img)
    overdrive.save(os.path.join(out_dir, f"stage_{index}_overdrive.png"))

if __name__ == "__main__":
    os.makedirs(sys.argv[2], exist_ok=True)
    generate_neon_states(sys.argv[1], sys.argv[2])
