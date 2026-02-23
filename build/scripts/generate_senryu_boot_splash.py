#!/usr/bin/env python

import sys, os
from PIL import Image, ImageEnhance, ImageFilter

# 1024x768 Scaled BBoxes for SEN, ryu and boot stage sections
SPLASH_AREA= (1024, 768)
SEN_BBOX   = (0,   0, 226,  112)    # Top-left part of the logo
RYU_BBOX   = (230, 0, 406,  182)    # Bottom-right part of the logo
STAGE_BBOX = (482, 0, 1024, 388)    # boot stage wave and glowing icons

states = ["cold", "warm", "vibrant", "overdrive"]

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

def generate_sen_256_palette(base_16):
    full_palette = list(base_16)

    def interpolate(idx1, idx2, count):
        c1 = base_16[idx1*3 : idx1*3+3]
        c2 = base_16[idx2*3 : idx2*3+3]
        return [int(c1[i] + (c2[i]-c1[i]) * (j/count))
                for j in range(count) for i in range(3)]

    # Neon Color Ramps
    full_palette += interpolate(3, 4, 32)   # Blue Ramps (Slots 16-47)
    full_palette += interpolate(9, 10, 32)  # Rose Ramps (Slots 48-79)
    full_palette += interpolate(5, 6, 32)   # Teal Ramps (Slots 80-111)

    # NEW: White Hot Ramp for the "Core" of the neon tubes
    # Interpolates between Wild Sand (7) and Pure White (15)
    full_palette += interpolate(7, 15, 32)  # White Ramps (Slots 112-143)

    full_palette += [0] * (768 - len(full_palette))
    return full_palette[:768]

# Interpolate SEN 16 palette to full 256 colors for PIL
sen_palette = generate_sen_256_palette(sen_palette)

def generate_neon_logo(input_path, out_dir):
    base_img = Image.open(input_path).convert("RGB").resize(SPLASH_AREA, Image.Resampling.LANCZOS)
    p_img = Image.new('P', (1,1)); p_img.putpalette(sen_palette)

    # Process both parts
    for part_name, bbox in [("sen", SEN_BBOX), ("ryu", RYU_BBOX)]:
        crop = base_img.crop(bbox)
        for state, level in [("cold", 0.3), ("warm", 1.0), ("vibrant", 1.5), ("overdrive", 3.5)]:
            enhancer = ImageEnhance.Brightness(crop)
            final = enhancer.enhance(level).quantize(palette=p_img, dither=Image.Dither.NONE)

            with open(os.path.join(out_dir, f"{part_name}_{state}.raw"), "wb") as f:
                f.write(final.tobytes())

def process_neon_image(img, target_size, p_img):
    # 1. Pre-Processing: Sharpen and Saturation Boost
    # This keeps the neon 'hot' before it gets scaled down
    img = ImageEnhance.Color(img).enhance(1.5)
    img = img.filter(ImageFilter.SHARPEN)

    # 2. Color Clamping: Force the core of the neon to stay bright
    # This prevents the downscaler from turning the bright core into muddy grey
    def clamp_logic(pixel):
        r, g, b, a = pixel
        # If the pixel is very bright, force it toward the 'White Hot' ramp
        if (r + g + b) / 3 > 220:
            return (255, 255, 255, a)
        return (r, g, b, a)

    # Apply clamping across the image pixels
    pixels = img.load()
    for y in range(img.height):
        for x in range(img.width):
            pixels[x, y] = clamp_logic(pixels[x, y])

    # convert to RGB for quantization
    img = img.convert("RGB")

    # 3. Resize and Quantize without dithering to keep the look clean
    img = img.resize(target_size, Image.Resampling.LANCZOS)
    final = img.quantize(palette=p_img, dither=Image.Dither.NONE)

    return final

def generate_neon_states(input_path, stage, out_dir):
    # Load base image - ensure we are in RGBA for accurate scaling and clamping
    base_img = Image.open(input_path).convert("RGBA")
    # convert to indexed 256 color SEN palette
    p_img = Image.new('P', (1,1)); p_img.putpalette(sen_palette)

    # If it's the background, just save one version and generate the logo states once
    if stage == 0:
        generate_neon_logo(input_path, out_dir)

        # cut out logo extracted above by painting it black
        draw = ImageDraw.Draw(base_img)
        # logo section
        logo_sect = (0,0, RYU_BBOX[2], RYU_BBOX[3])
        draw.rectangle(logo_sect, fill=(0, 0, 0, 255))

        final = process_neon_image(base_img, SPLASH_AREA, p_img)

        with open(os.path.join(out_dir, "stage_0.raw"), "wb") as f:
            f.write(final.tobytes())

#        return    # done for stage 0

    # For remaining stages, crop only changing part and create 4 luminosity levels
    base_img = base_img.crop(STAGE_BBOX)
    stage_sect = (STAGE_BBOX[2] - STAGE_BBOX[0], STAGE_BBOX[3] - STAGE_BBOX[1])

    for state in states:
        # Create a copy so we don't degrade the original base in the loop
        state_img = base_img.copy()

        # Create the specific visual state
        # (You can add specific logic here per state if needed, e.g., brightness adjustments)
        processed = process_neon_image(state_img, stage_sect, p_img)

        # Save as RAW binary for tinyxxd
        raw_name = f"stage_{stage}_{state}.raw"
        with open(os.path.join(out_dir, raw_name), "wb") as f:
            f.write(processed.tobytes())

if __name__ == "__main__":
    os.makedirs(sys.argv[3], exist_ok=True)

    # args = in, out, stage
    generate_neon_states(sys.argv[1], sys.argv[2], sys.argv[3])
