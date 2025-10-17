#!/usr/bin/env python3
"""
Nyancat Animation Extractor and VGA Converter

Extracts animation frames from nyancat source and converts them to
compact format for hardware display. Stores 64x64 frames as 4-bit
character indices. Hardware performs scaling and color lookup.

Input:  animation.c from nyancat project
Output: nyancat-frames.hex (compressed format)
        nyancat-colors.hex (color palette)
"""

import re
import sys
from pathlib import Path


# Color mapping from nyancat (truecolor mode, case 8)
# Maps ASCII character to RGB values
COLOR_MAP = {
    ",": (0, 49, 105),  # 0: Blue background
    ".": (255, 255, 255),  # 1: White stars
    "'": (0, 0, 0),  # 2: Black border
    "@": (255, 205, 152),  # 3: Tan poptart
    "$": (255, 169, 255),  # 4: Pink poptart
    "-": (255, 76, 152),  # 5: Red poptart
    ">": (255, 25, 0),  # 6: Red rainbow
    "&": (255, 154, 0),  # 7: Orange rainbow
    "+": (255, 240, 0),  # 8: Yellow rainbow
    "#": (40, 220, 0),  # 9: Green rainbow
    "=": (0, 144, 255),  # 10: Light blue rainbow
    ";": (104, 68, 255),  # 11: Dark blue rainbow
    "*": (153, 153, 153),  # 12: Gray cat face
    "%": (255, 163, 152),  # 13: Pink cheeks
}


def rgb_to_vga6(r, g, b):
    """Convert 8-bit RGB to 6-bit VGA format (2R2G2B)."""
    r2 = (r >> 6) & 0b11
    g2 = (g >> 6) & 0b11
    b2 = (b >> 6) & 0b11
    return (r2 << 4) | (g2 << 2) | b2


def build_char_to_index():
    """Build character to index mapping."""
    chars = list(COLOR_MAP.keys())
    return {ch: idx for idx, ch in enumerate(chars)}


def parse_animation_c(filepath):
    """Parse animation.c and extract all frame data."""
    with open(filepath, "r") as f:
        content = f.read()

    frame_pattern = r"const char \* frame(\d+)\[\] = \{([^}]+)\};"
    frames = []

    for match in re.finditer(frame_pattern, content, re.DOTALL):
        frame_num = int(match.group(1))
        frame_data = match.group(2)
        lines = re.findall(r'"([^"]+)"', frame_data)

        if len(lines) > 0:
            frames.append((frame_num, lines))

    frames.sort(key=lambda x: x[0])
    return frames


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <animation.c> [output_dir]")
        sys.exit(1)

    input_file = Path(sys.argv[1])

    # Use specified output directory or default to rtl/
    if len(sys.argv) >= 3:
        output_dir = Path(sys.argv[2])
    else:
        output_dir = Path(__file__).parent.parent / "rtl"

    frames_file = output_dir / "nyancat-frames.hex"
    colors_file = output_dir / "nyancat-colors.hex"

    print(f"Parsing {input_file}...")
    frames = parse_animation_c(input_file)
    print(f"Found {len(frames)} frames")

    if len(frames) == 0:
        print("Error: No frames found!")
        sys.exit(1)

    # Build character index mapping
    char_to_idx = build_char_to_index()
    print(f"Character palette: {len(char_to_idx)} colors")

    # Write color palette
    print(f"\nWriting {colors_file}...")
    with open(colors_file, "w") as f:
        f.write("// Nyancat color palette (6-bit VGA: RRGGBB)\n")
        for char, rgb in COLOR_MAP.items():
            vga6 = rgb_to_vga6(*rgb)
            idx = char_to_idx[char]
            f.write(f"{vga6:02x}  // {idx:2d}: '{char}' RGB{rgb}\n")

    # Write frames (4-bit indices, 64x64 per frame)
    print(f"Writing {frames_file}...")
    total_pixels = 0

    with open(frames_file, "w") as f:
        for frame_num, lines in frames:
            f.write(f"// Frame {frame_num}\n")

            # Each frame is 64x64
            for y, line in enumerate(lines):
                for x, char in enumerate(line):
                    idx = char_to_idx.get(char, 0)  # Default to background
                    f.write(f"{idx:x}\n")
                    total_pixels += 1

    frames_kb = total_pixels / 1024
    print(f"\nDone! Generated {len(frames)} frames")
    print(f"Frame size: 64x64 pixels")
    print(f"Total: {total_pixels} pixels ({frames_kb:.1f} KB)")
    print(f"Color palette: {len(COLOR_MAP)} colors")


if __name__ == "__main__":
    main()
