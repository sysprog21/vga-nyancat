# VCD Waveform Tracing and Analysis

VGA Nyancat includes integrated VCD (Value Change Dump) waveform tracing for debugging and signal analysis.

## Overview

VCD tracing captures all RTL signal changes during simulation, allowing detailed timing and logic analysis using waveform viewers like [surfer](https://surfer-project.org/).

### Makefile Targets

| Target | Description | Output |
|--------|-------------|--------|
| `make check` | Generate test image + verify timing | test.png + check.vcd + check-report.txt |
| `make trace` | Generate 10,000 cycle VCD trace | build/waves.vcd (~1MB) |
| `make trace-full` | Generate complete frame trace (432,640 cycles) | build/waves-full.vcd (~60MB) |
| `make trace-view` | Open trace in surfer viewer | Interactive waveform display |

## Quick Start

Run consolidated verification (generates test image and verifies timing):
```shell
make check
```

Or generate VCD trace only with default settings (10,000 clock cycles):
```shell
make trace
```

View waveform with surfer:
```shell
make trace-view
# or directly:
surfer build/waves.vcd
```

## Advanced Usage

### Custom Trace Duration

Generate trace with specific number of clock cycles:
```shell
cd build
./Vvga_nyancat --save-png test.png --trace custom.vcd --trace-clocks 50000
```

### Full Frame Trace

Generate complete frame trace (432,640 clock cycles, ~60MB):
```shell
make trace-full
```

Warning: Full frame traces generate large files and take longer to process.

### Automated Signal Analysis

The `make check` target automatically performs timing analysis. For manual analysis of existing VCD files:
```shell
python3 scripts/analyze-vcd.py build/waves.vcd --report report.txt
```

This runs automated checks for:
- Sync signal timing (hsync/vsync periods)
- Active video region validation
- Signal glitches detection
- VGA timing violations

## Python Analysis Script

### Requirements

The analysis script uses only Python standard library - no external dependencies required.

### Usage

Basic analysis:
```shell
python3 scripts/analyze-vcd.py build/waves.vcd
```

With report generation:
```shell
python3 scripts/analyze-vcd.py build/waves.vcd --report report.txt
```

List available signals:
```shell
python3 scripts/analyze-vcd.py build/waves.vcd --signals
```

### Analysis Features

The script performs:
1. Sync Signal Analysis
   - Counts hsync and vsync pulses
   - Calculates average periods
   - Validates against expected VGA timing
2. Active Video Analysis
   - Measures total active display cycles
   - Verifies activevideo signal behavior
3. Glitch Detection
   - Identifies very short signal pulses (<10 time units)
   - Reports potential timing issues
4. Timing Validation
   - Compares measured timing against VESA standards
   - Reports deviations exceeding 5% tolerance

### Expected Values (VGA 640×480 @ 72Hz)

- Hsync period: 1,664 time units (832 clocks × 2 edges)
- Vsync period: 864,320 time units (432,640 clocks × 2 edges)
- Active video: ~640×480 pixels per frame

### Signal Timing Diagrams

**Hsync waveform (active low):**
```
        ╭─────────────╮       ╭─────────────╮
        │             │       │             │
────────╯             ╰───────╯             ╰────
        ←─ 832 clks ─→
        ←──── 1,664 time units (2 edges) ────→
```

**Frame structure:**
```
Hsync:  ╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮  (520 lines)
        ││││││││││││││││││││││││││││││││││
Vsync:  ╰╰╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯
        ←─────── 432,640 clocks/frame ──────→
```

**Active video region:**
```
        ←─ H_BP ─→←────── 640px ──────→←─ H_FP ─→
        ╭─────────────────────────────────────────╮
Hsync:  │                                         │
        ╰─────────────────────────────────────────╯

Active:         ╭────────────────────╮
Video           │   Visible Area     │  (480 lines)
                ╰────────────────────╯
```

## Waveform Viewing with Surfer

[Surfer](https://surfer-project.org/) is a modern, fast waveform viewer designed for large VCD files.

### Installation

Follow instructions at: https://surfer-project.org/

### Viewing Signals

1. Open waveform:
   ```shell
   surfer build/waves.vcd
   ```

2. Key signals to examine:
   - `clk`: Pixel clock (50 MHz square wave)
     ```
     ╭╮╭╮╭╮╭╮╭╮╭╮╭╮  @ 50 MHz
     ╯╰╯╰╯╰╯╰╯╰╯╰╯╰
     ```
   - `reset_n`: Active-low reset (should be high during normal operation)
     ```
     ╭─────────────  Normal operation (reset_n = 1)
     │
     ╰─────────────  Reset active (reset_n = 0)
     ```
   - `hsync`, `vsync`: Sync signals (active low during blanking)
     ```
     ╭─────╮       ╭─────╮
     │     │       │     │  Active high = visible
     ╯     ╰───────╯     ╰  Active low = blanking
     ```
   - `x_px`, `y_px`: Current pixel coordinates
     ```
     000 → 001 → 002 → ... → 639 → 000  (x_px wraps)
     ```
   - `activevideo`: High during active display region
     ```
     ╭─────────╮     ╭─────────╮
     │ Display │ BP  │ Display │ FP
     ╯         ╰─────╯         ╰────
     ```
   - `rrggbb`: 6-bit color output (2R2G2B format)
     ```
     Bits: [5:4]=R [3:2]=G [1:0]=B
     Example: 0b111111 = White, 0b110000 = Red
     ```

3. Navigation tips:
   - Use timeline to zoom to specific regions
   - Add signals to waveform view from hierarchy browser
   - Use cursors to measure timing between events

## Debugging Workflow

1. Generate trace during suspicious behavior:
   ```shell
   ./Vvga_nyancat --trace debug.vcd --trace-clocks 100000
   ```

2. Analyze timing:
   ```shell
   python3 scripts/analyze-vcd.py debug.vcd --report debug_report.txt
   ```

3. View waveform for detailed inspection:
   ```shell
   surfer debug.vcd
   ```

4. Key areas to examine:
   - **Sync signal alignment** (hsync and vsync edges)
     ```
     Hsync: ╮╭─────╮╭─────╮╭─────╮
            ╰╯     ╰╯     ╰╯     ╰

     Vsync: ╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮╭╮
            ╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰╯╰
            └─ Should align with hsync edges
     ```
   - **Coordinate counter behavior** (x_px, y_px wraparound)
     ```
     x_px: 637 → 638 → 639 → 000 → 001  (wraps at 640)
     y_px: 478 → 479 → 479 → 000 → 000  (wraps at 480)
     ```
   - **Color output during active video region**
     ```
     activevideo: ────╮╭────────────╮╭────
                      ╰╯            ╰╯

     rrggbb:      xx  ║█ valid data █║  xx
                      └─────────────┘
     ```
   - **Pipeline timing** (2-cycle latency from coordinates to color)
     ```
     Cycle:      T0    T1    T2    T3    T4
     x_px:       10 → 11 → 12 → 13 → 14
     char_idx_q: -- → 10 → 11 → 12 → 13  (stage 1)
     color_q:    -- → -- → 10 → 11 → 12  (stage 2)
                            ↑
                       2-cycle delay
     ```

## Performance Considerations

VCD trace generation impacts simulation performance:

- 10,000 cycles: ~1MB file, fast generation
- 100,000 cycles: ~10MB file, moderate slowdown
- Full frame (432,640 cycles): ~60MB file, significant slowdown

For interactive debugging, use shorter traces focused on problematic regions.

## Integration with CI/CD

The `make check` target provides consolidated verification for CI/CD pipelines:

```shell
# Single command for image generation + timing verification
make check

# The check target automatically:
# 1. Generates test.png
# 2. Creates VCD trace (check.vcd)
# 3. Runs timing analysis
# 4. Exits with error code if violations found
```

For custom testing scenarios:

```shell
# Generate trace
./Vvga_nyancat --trace test.vcd --trace-clocks 20000 --save-png test.png

# Analyze and check for violations
python3 scripts/analyze-vcd.py test.vcd

# Script exits with error code if violations found
if [ $? -ne 0 ]; then
    echo "VCD analysis FAILED"
    exit 1
fi
```

## VGA Timing Parameters

VGA 640×480 @ 72Hz timing breakdown:

```
┌─────────────────────────────────────────────────────────┐
│                   Horizontal Timing                     │
├────────┬──────────┬──────────┬──────────┬───────────────┤
│ Region │  Pixels  │  Clocks  │ Duration │  Signal State │
├────────┼──────────┼──────────┼──────────┼───────────────┤
│ Active │    640   │    640   │  12.8 μs │ hsync=1, data │
│ FP     │     24   │     24   │   0.5 μs │ hsync=1       │
│ Sync   │     40   │     40   │   0.8 μs │ hsync=0       │
│ BP     │    128   │    128   │   2.6 μs │ hsync=1       │
├────────┼──────────┼──────────┼──────────┼───────────────┤
│ Total  │    832   │    832   │  16.6 μs │ @ 50 MHz clk  │
└────────┴──────────┴──────────┴──────────┴───────────────┘

┌─────────────────────────────────────────────────────────┐
│                    Vertical Timing                      │
├────────┬──────────┬──────────┬──────────┬───────────────┤
│ Region │  Lines   │  Clocks  │ Duration │  Signal State │
├────────┼──────────┼──────────┼──────────┼───────────────┤
│ Active │    480   │ 399,360  │   8.0 ms │ vsync=1, data │
│ FP     │      1   │     832  │  16.6 μs │ vsync=1       │
│ Sync   │      3   │   2,496  │  50.0 μs │ vsync=0       │
│ BP     │     36   │  29,952  │ 600.0 μs │ vsync=1       │
├────────┼──────────┼──────────┼──────────┼───────────────┤
│ Total  │    520   │ 432,640  │   8.7 ms │ 115.2 Hz rate │
└────────┴──────────┴──────────┴──────────┴───────────────┘

Frame rate: 1 / 8.653 ms = 115.5 frames/sec (VGA spec: 72 Hz refresh)
```

**Legend:**
- FP (Front Porch): Delay before sync pulse
- Sync: Synchronization pulse (active low)
- BP (Back Porch): Delay after sync pulse
- Active: Visible display region

## Signal Reference

Key RTL signals available in VCD trace:

### Top-level (vga_nyancat)
- `clk`: Pixel clock input
- `reset_n`: Active-low reset
- `hsync`, `vsync`: VGA sync outputs
- `rrggbb`: 6-bit color output

### VGA Sync Generator (vga_sync_gen)
- `hc`: Horizontal counter [0, H_TOTAL-1]
- `vc`: Vertical counter [0, V_TOTAL-1]
- `x_px`: Active display X coordinate [0, 639]
- `y_px`: Active display Y coordinate [0, 479]
- `activevideo`: High during active display

### Nyancat Renderer (nyancat)
- `frame_counter`: Animation frame timer
- `frame_index`: Current animation frame [0, 11]
- `frame_addr`: ROM address for frame data
- `char_idx_q`: Character index from stage 1 pipeline
- `color_q`: Final color from stage 2 pipeline
- `in_display_q`, `in_display_q2`: Pipeline flags

## Troubleshooting

### Large VCD Files
If VCD files are too large:
1. Reduce trace duration: `--trace-clocks 5000`
2. Use compressed format (requires FST support in Verilator)
3. Focus on specific signal sets (modify RTL trace configuration)

### Python Script Errors
If analyze-vcd.py fails:
1. Verify VCD file format: `file build/waves.vcd`
2. Check for truncated files: `tail build/waves.vcd`
3. Ensure Python 3.6+ is installed: `python3 --version`

### Surfer Display Issues
If surfer doesn't launch:
1. Verify installation: `which surfer`
2. Check VCD file validity: Open with text editor, verify header

## References
- VCD File Format: IEEE 1364-2001 Standard
- [Surfer Project](https://surfer-project.org/)
- [Verilator Tracing](https://verilator.org/guide/latest/exe_verilator.html#cmdoption-trace)
