#!/usr/bin/env python

import sys, os, re, PIL
from PIL import Image, ImageEnhance

# 1024x768 Scaled BBoxes for SEN, ryu and boot stage sections
SPLASH_WIDTH = 1024
SPLASH_HEIGHT = 768
SEN_BBOX   = (0,   0, 226,  112)    # Top-left part of the logo
RYU_BBOX   = (230, 0, 406,  182)    # Bottom-right part of the logo
STAGE_BBOX = (482, 0, 1024, 388)    # boot stage wave and glowing icons

# Define the 16-color Neon Palette
pal = [
          0,0,0, 47,47,47, 80,80,80, 120,180,255,          # Blue/Grey
          60,90,180, 46,191,212, 20,100,110, 244,244,244,  # Teal/White
          247,242,225, 255,51,204, 150,0,100, 100,255,100, # Pink/Green
          255,165,0, 200,200,200, 30,30,60, 255,255,255    # Accents
      ] + [0]*720

def generate_neon_logo(input_path, out_dir):
    base_img = Image.open(input_path).convert("RGB").resize((SPLASH_WIDTH, SPLASH_HEIGHT), Image.Resampling.LANCZOS)
    p_img = Image.new('P', (1,1)); p_img.putpalette(pal)

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

    p_img = Image.new('P', (1,1)); p_img.putpalette(pal)

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
