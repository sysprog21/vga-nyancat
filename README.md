# VGA Nyancat

![CI](https://github.com/sysprog21/vga-nyancat/actions/workflows/main.yml/badge.svg)

Hardware-accelerated Nyancat (Pop-Tart Cat) animation on VGA display,
implemented in Verilog RTL and simulated using [Verilator](https://verilator.org/).
Features real-time hardware scaling, ROM-based animation storage, and a 2-stage rendering pipeline.

Note: This is an educational hardware design project demonstrating VGA timing,
ROM-based graphics, and hardware animation techniques.
The Nyancat character and animation are used under fair use for educational purposes.

## Features

- 12-frame animation cycling at ~11 fps (90ms per frame)
- Real-time 8× hardware scaling from 64×64 source to 512×512 display
- Efficient storage using 4-bit character indices + 14-color palette (230× compression)
- Pipelined rendering with 2-stage ROM lookup for minimal latency
- VESA-compliant timing at 640×480 @ 72Hz (31.5 MHz pixel clock)
- Automated data generation from upstream [klange/nyancat](https://github.com/klange/nyancat) source

## Prerequisites

Ensure that you have the required dependencies installed:

Ubuntu/Debian:
```shell
sudo apt-get install libsdl2-dev verilator python3
```

macOS:
```shell
brew install sdl2 verilator python3
```

## Building and Running

To build and run the interactive simulation:
```shell
make run
```

This will automatically:
1. Download animation source via curl/wget (if needed)
2. Generate animation data files
3. Build the Verilator simulation
4. Launch the interactive display

Interactive controls:
- p key: Save current frame to test.png
- ESC key: Reset animation
- q key: Quit

## Testing

To run automated tests and generate a test frame:
```shell
make check
```

This generates `test.png` containing a single animation frame.

## Code Formatting

Format all Verilog and C++ source files:
```shell
make indent
```

This project follows the `.verilog-style` guidelines for consistent code formatting:
- Verilog files formatted with `verible-verilog-format`
- C++ files formatted with `clang-format`

Install verible from [chipsalliance/verible releases](https://github.com/chipsalliance/verible/releases).

## How It Works

### Data Generation Pipeline

This project automatically extracts animation data from the upstream [klange/nyancat](https://github.com/klange/nyancat) repository and converts it to hardware-friendly format:

```
┌────────────────────────────────────────────────────────────┐
│ 1. Source Acquisition (Automated)                          │
│    make → Download animation.c (52KB) via curl/wget        │
│         → Save to build/animation.c                        │
│                                                            │
│ 2. Data Extraction (scripts/gen-nyancat.py)                │
│    Input:  animation.c (ASCII art frames)                  │
│    Parse:  Extract 12 frames of 64×64 character data       │
│    Output: Character indices + color palette               │
│                                                            │
│ 3. Format Conversion                                       │
│    ASCII characters → 4-bit indices (0-13)                 │
│    RGB888 colors    → 6-bit VGA (RRGGBB)                   │
│                                                            │
│ 4. Hardware Files Generated                                │
│    build/nyancat-frames.hex: 49,152 lines (4-bit each)     │
│    build/nyancat-colors.hex: 14 colors in 6-bit format     │
└────────────────────────────────────────────────────────────┘
```

Character to Index Mapping:

The script maps each ASCII character from the animation to a 4-bit index:

| Char | Index | Color | RGB | VGA 6-bit |
|------|-------|-------|-----|-----------|
| `,` | 0 | Blue background | (0,49,105) | `000001` |
| `.` | 1 | White stars | (255,255,255) | `111111` |
| `'` | 2 | Black border | (0,0,0) | `000000` |
| `@` | 3 | Tan poptart | (255,205,152) | `111110` |
| ... | ... | ... | ... | ... |
| `%` | 13 | Pink cheeks | (255,163,152) | `111010` |

Conversion Process:

1. Parse animation.c: Extract frame data using regex patterns
2. Build color map: Map 14 unique ASCII characters to palette indices
3. Convert frames: Transform each 64×64 character grid to 4-bit indices
4. Generate RGB to VGA: Convert 24-bit RGB to 6-bit VGA format (2R2G2B)

Result: 230× compression (24KB vs 5.4MB for raw RGB888 storage)

### System Architecture

```
                    ┌─────────────────────────────────────┐
                    │      VGA Nyancat Top Module         │
                    └───────────┬─────────────────┬───────┘
                                │                 │
                   ┌────────────▼──────────┐      │
                   │   VGA Sync Generator  │      │
                   │   (vga-sync-gen.v)    │      │
                   │                       │      │
                   │  • H/V counters       │      │
                   │  • Sync pulse gen     │      │
                   │  • Pixel coordinates  │      │
                   └────────────┬──────────┘      │
                                │                 │
                     {x_px, y_px, activevideo}    │
                                │                 │
                   ┌────────────▼─────────────────▼──────┐
                   │    Nyancat Animation Renderer       │
                   │         (nyancat.v)                 │
                   │  ┌──────────────────────────────┐   │
                   │  │  Coordinate Transformation   │   │
                   │  │  • Remove offset             │   │
                   │  │  • Descale by 8              │   │
                   │  │  • Calculate ROM address     │   │
                   │  └──────────┬───────────────────┘   │
                   │             │                       │
                   │  ┌──────────▼───────────────────┐   │
                   │  │   2-Stage Pipeline           │   │
                   │  │                              │   │
                   │  │  Stage 1: frame_mem[addr]    │   │
                   │  │           → char_idx         │   │
                   │  │                              │   │
                   │  │  Stage 2: color_mem[char_idx]│   │
                   │  │           → color            │   │
                   │  └──────────┬───────────────────┘   │
                   │             │                       │
                   └─────────────┼───────────────────────┘
                                 │
                          rrggbb (6-bit color)
                                 │
                                 ▼
                            VGA Display
```

### Data Flow Pipeline

The rendering pipeline transforms pixel coordinates into colors through multiple stages:

```
Clock  Input            Stage 1              Stage 2              Stage 3           Output
Cycle  Coordinates      ROM Addressing       Char Lookup          Color Lookup
─────  ─────────────    ──────────────       ───────────────      ────────────      ──────
  N    (x_px, y_px) ──▶ Transform ────────▶ [pipeline reg] ───▶ [pipeline reg] ──▶ (blank)
                        addr calculated

 N+1   (x_px+1, y_px) ─▶ Transform ────────▶ frame_mem[addr] ──▶ [pipeline reg] ──▶ (blank)
                        addr calculated      char_idx fetched

 N+2   (x_px+2, y_px) ─▶ Transform ────────▶ frame_mem[addr] ──▶ color_mem[idx] ──▶ color(N)
                        addr calculated      char_idx fetched    color fetched        ↑
                                                                                      |
                                                            2-clock latency ───────────
```

### Memory Organization

```
┌─────────────────────────────────────────────────────────────────────┐
│ Frame Memory (frame_mem): 49,152 × 4 bits = 24 KB                   │
├─────────────────────────────────────────────────────────────────────┤
│  Frame 0 (4096 entries)  ┌──────────────────────┐                   │
│  [0..4095]               │  64 × 64 = 4096      │                   │
│                          │  4-bit char indices  │                   │
│  Frame 1 (4096 entries)  │  Values: 0-13        │                   │
│  [4096..8191]            └──────────────────────┘                   │
│                                                                     │
│  ...                                                                │
│                                                                     │
│  Frame 11 (4096 entries) ┌──────────────────────┐                   │
│  [45056..49151]          │  Last frame data     │                   │
│                          └──────────────────────┘                   │
│                                                                     │
│  ROM Address Calculation:                                           │
│    addr = (frame_index × 4096) + (src_y × 64) + src_x               │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ Color Palette (color_mem): 16 × 6 bits = 12 bytes                   │
├─────────────────────────────────────────────────────────────────────┤
│  Index   Color         6-bit (RRGGBB)   RGB888                      │
│  ─────   ──────────    ──────────────   ───────────────────────     │
│    0     Dark Blue     000001           (  0,  49, 105)             │
│    1     White         111111           (255, 255, 255)             │
│    2     Black         000000           (  0,   0,   0)             │
│   ...    (10 more colors)                                           │
│   13     Light Pink    111010           (255, 163, 152)             │
│  14-15   (unused)                                                   │
└─────────────────────────────────────────────────────────────────────┘
```

### VGA Timing Diagram

```
One complete frame (640×480 @ 72Hz):

Horizontal timing (per line, 832 pixels):
  ├────────┬──────────┬───────────┬──────────────────────────┤
  │   FP   │   SYNC   │    BP     │        ACTIVE            │
  │  24px  │   40px   │  128px    │        640px             │
  │        │ (hsync=0)│           │    (visible data)        │
  └────────┴──────────┴───────────┴──────────────────────────┘
  ◄──────────────────────────────────────────────────────────►
           832 pixels × 31.5 MHz = 26.4 µs per line

Vertical timing (per frame, 520 lines):
  ├────────┬──────────┬───────────┬──────────────────────────┤
  │   FP   │   SYNC   │    BP     │        ACTIVE            │
  │  9 ln  │   3 ln   │  28 ln    │       480 ln             │
  │        │ (vsync=0)│           │   (visible lines)        │
  └────────┴──────────┴───────────┴──────────────────────────┘
  ◄──────────────────────────────────────────────────────────►
         520 lines × 26.4 µs = 13.73 ms per frame (~72.8 Hz)

Active video region (where animation is rendered):
  ┌───────────────────────────────────────────────────┐
  │ 640 × 480 VGA display                             │
  │    ┌─────────────────────────────────┐            │
  │    │                                 │            │
  │    │    512 × 512 Nyancat            │  ◄─ Centered horizontally
  │ 64 │    (8× scaled from 64×64)       │  64        │
  │ px │                                 │  px        │
  │    │                                 │  margin    │
  │    └─────────────────────────────────┘            │
  │                                                   │
  └───────────────────────────────────────────────────┘
  Note: Bottom 32 lines of animation are clipped (512 > 480)
```

## Command-Line Options

```shell
./obj_dir/Vvga_nyancat --save-png output.png  # Save a single frame and exit
./obj_dir/Vvga_nyancat --help                 # Show help message
```

## Technical Details

### Animation Data Storage

| Parameter | Value | Details |
|-----------|-------|---------|
| Source frame size | 64×64 pixels | Original animation resolution |
| Total frames | 12 | Complete animation loop |
| Storage format | 4-bit indices | Character codes (0-13) |
| Frame memory | 49,152 × 4 bits | 24 KB total (12 × 4096 entries) |
| Color palette | 14 colors × 6 bits | 12 bytes (2R2G2B VGA format) |
| Total ROM | ~24 KB | 230× smaller than raw RGB888 (5.4 MB) |

### VGA Display Timing

| Parameter | Value | Calculation |
|-----------|-------|-------------|
| Resolution | 640×480 @ 72Hz | VESA standard timing |
| Pixel clock | 31.5 MHz | Standard VGA clock |
| Horizontal total | 832 pixels/line | FP(24) + SYNC(40) + BP(128) + ACTIVE(640) |
| Vertical total | 520 lines/frame | FP(9) + SYNC(3) + BP(28) + ACTIVE(480) |
| Line period | 26.4 µs | 832 ÷ 31.5 MHz |
| Frame period | 13.73 ms | 520 × 26.4 µs |
| Refresh rate | 72.8 Hz | 1 ÷ 13.73 ms |
| Clocks/frame | 432,640 | 832 × 520 |

### Animation Timing

| Parameter | Value | Details |
|-----------|-------|---------|
| Frame duration | 90 ms | Target animation speed |
| Clocks/frame | 2,835,000 | 90 ms × 31.5 MHz |
| Animation rate | ~11.1 fps | 31.5 MHz ÷ 2,835,000 |
| Total loop time | 1.08 seconds | 12 frames × 90 ms |

### Hardware Implementation

| Feature | Implementation | Benefit |
|---------|----------------|---------|
| Scaling | 8× nearest-neighbor | Simple bit-shift (÷8 = >>3) |
| Pipeline stages | 2 (frame ROM → palette ROM) | 2-clock latency, full throughput |
| Display area | 512×512 centered | Symmetric margins (64px sides) |
| Coordinate transform | Offset removal + descaling | Minimal logic complexity |
| Frame sequencing | 22-bit counter + 4-bit index | Automatic wrap at 12 frames |
| ROM address calc | Bit concatenation + OR | Zero-delay, no multipliers |
| ROM reads | Synchronous block RAM | Synthesis-friendly implementation |

### Data Generation Automation

The build system automatically handles all data generation:

Makefile workflow:
```
make all
  ↓
1. Check if build/animation.c exists
  ↓ (if not)
2. Download animation.c (52KB) via curl or wget
   Source: https://raw.githubusercontent.com/klange/nyancat/...
  ↓
3. Run scripts/gen-nyancat.py build/animation.c build/
  ↓
4. Generate build/nyancat-frames.hex (49,152 lines)
  ↓
5. Generate build/nyancat-colors.hex (14 colors)
  ↓
6. Run Verilator to generate C++ files
  ↓
7. Compile C++ simulation binary
  ↓
Build complete (~4.7 seconds from clean state)
```

Available Make targets:
```shell
make all         # Build everything (default)
make build       # Same as 'all', explicit build target
make run         # Build and launch interactive simulation
make check       # Build and generate test.png
make clean       # Remove build artifacts (keep build/ directory)
make distclean   # Remove everything including build/ directory
make regen-data  # Force regeneration of animation data
make indent      # Format all Verilog and C++ source files
```

Manual data regeneration:
```shell
# Force regeneration using existing upstream source
make regen-data

# Clean everything and rebuild from scratch
make distclean && make all

# Generate from custom source file
python3 scripts/gen-nyancat.py /path/to/animation.c
```

Data file format:

`nyancat-frames.hex` - One hex digit per line (4 bits):
```
// Frame 0
0    ← Background pixel (char ',')
0
1    ← Star pixel (char '.')
...
```

`nyancat-colors.hex` - VGA 6-bit colors with comments:
```
01  //  0: ',' RGB(0,49,105)      ← Background
3f  //  1: '.' RGB(255,255,255)   ← Stars
00  //  2: ''' RGB(0,0,0)         ← Black
...
```

## Project Structure

```
vga-nyancat/
├── rtl/                              # Hardware RTL modules
│   ├── vga-sync-gen.v               # VGA sync generator (640×480@72Hz)
│   │                                 # • Generates hsync/vsync pulses
│   │                                 # • Outputs pixel coordinates
│   │                                 # • Provides activevideo flag
│   │
│   ├── nyancat.v                    # Nyancat animation renderer
│   │                                 # • Frame sequencing (12 frames)
│   │                                 # • Coordinate transformation
│   │                                 # • 2-stage ROM pipeline
│   │                                 # • ROM: 49,152×4b + 16×6b
│   │
│   └── vga-nyancat.v                # Top-level integration
│                                     # • Connects sync gen to renderer
│                                     # • Reset polarity conversion
│
├── sim/                              # Simulation testbench
│   └── main.cpp                     # Verilator + SDL2 wrapper
│                                     # • SDL2 framebuffer rendering
│                                     # • Standalone PNG encoder (no deps)
│                                     # • Interactive controls
│
├── scripts/                          # Data generation tools
│   └── gen-nyancat.py               # Animation data extractor
│                                     # • Downloads klange/nyancat source
│                                     # • Parses ASCII art frames
│                                     # • Generates hex files
│
├── build/                            # Generated files (gitignored)
│   ├── animation.c                  # Downloaded source (52KB)
│   ├── nyancat-frames.hex           # Frame data (49,152 lines)
│   ├── nyancat-colors.hex           # Color palette (14 colors)
│   ├── Vvga_nyancat                 # Simulation binary
│   └── test.png                     # Generated test image
│
├── Makefile                          # Build automation
│                                     # • Data generation
│                                     # • Verilator compilation
│                                     # • Test targets
│
└── README.md                         # This file
```

### Module Hierarchy

```
vga_nyancat (top)
    ├── vga_sync_gen
    │   ├── Inputs:  px_clk, reset
    │   └── Outputs: hsync, vsync, x_px[9:0], y_px[9:0], activevideo
    │
    └── nyancat
        ├── Inputs:  px_clk, reset, x_px[9:0], y_px[9:0], activevideo
        └── Outputs: rrggbb[5:0]
```

## License

VGA Nyancat is available under a permissive MIT-style license.
Use of this source code is governed by a MIT license that can be found in the [LICENSE](LICENSE) file.
