// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

// Video Mode Definitions for VGA Display
//
// This file provides parameterized timing definitions for various VESA
// standard video modes. Each mode defines horizontal and vertical timing
// parameters following the standard format:
//
//   Total = Front Porch + Sync Pulse + Back Porch + Active Display
//
// Timing parameters are organized as:
//   H_ACTIVE: Horizontal active pixels (visible display width)
//   H_FP:     Horizontal front porch (pixels before sync)
//   H_SYNC:   Horizontal sync pulse width (pixels)
//   H_BP:     Horizontal back porch (pixels after sync)
//   V_ACTIVE: Vertical active lines (visible display height)
//   V_FP:     Vertical front porch (lines before sync)
//   V_SYNC:   Vertical sync pulse width (lines)
//   V_BP:     Vertical back porch (lines after sync)
//
// Usage:
//   Define VIDEO_MODE before including this file:
//   `define VIDEO_MODE_VGA_640x480_72
//   `include "videomode.vh"

`ifndef VIDEOMODE_VH
`define VIDEOMODE_VH

// Default to 640×480 @ 72Hz if no mode specified
`ifndef VIDEO_MODE_VGA_640x480_72
`ifndef VIDEO_MODE_VGA_640x480_60
`ifndef VIDEO_MODE_VGA_800x600_60
`ifndef VIDEO_MODE_SVGA_800x600_72
`ifndef VIDEO_MODE_XGA_1024x768_60
  `define VIDEO_MODE_VGA_640x480_72
`endif
`endif
`endif
`endif
`endif

// ============================================================================
// VGA 640×480 @ 72Hz (Default mode for VGA Nyancat)
// ============================================================================
// Pixel clock: 31.5 MHz
// Horizontal frequency: 37.9 kHz
// Vertical frequency: 72.8 Hz
// VESA standard timing

`ifdef VIDEO_MODE_VGA_640x480_72
  localparam H_ACTIVE = 640;
  localparam H_FP     = 24;
  localparam H_SYNC   = 40;
  localparam H_BP     = 128;

  localparam V_ACTIVE = 480;
  localparam V_FP     = 9;
  localparam V_SYNC   = 3;
  localparam V_BP     = 28;
`endif

// ============================================================================
// VGA 640×480 @ 60Hz
// ============================================================================
// Pixel clock: 25.175 MHz
// Horizontal frequency: 31.5 kHz
// Vertical frequency: 59.9 Hz
// Original VGA standard timing

`ifdef VIDEO_MODE_VGA_640x480_60
  localparam H_ACTIVE = 640;
  localparam H_FP     = 16;
  localparam H_SYNC   = 96;
  localparam H_BP     = 48;

  localparam V_ACTIVE = 480;
  localparam V_FP     = 10;
  localparam V_SYNC   = 2;
  localparam V_BP     = 33;
`endif

// ============================================================================
// SVGA 800×600 @ 60Hz
// ============================================================================
// Pixel clock: 40 MHz
// Horizontal frequency: 37.9 kHz
// Vertical frequency: 60.3 Hz
// VESA standard timing

`ifdef VIDEO_MODE_VGA_800x600_60
  localparam H_ACTIVE = 800;
  localparam H_FP     = 40;
  localparam H_SYNC   = 128;
  localparam H_BP     = 88;

  localparam V_ACTIVE = 600;
  localparam V_FP     = 1;
  localparam V_SYNC   = 4;
  localparam V_BP     = 23;
`endif

// ============================================================================
// SVGA 800×600 @ 72Hz
// ============================================================================
// Pixel clock: 50 MHz
// Horizontal frequency: 48.1 kHz
// Vertical frequency: 72.2 Hz
// VESA standard timing

`ifdef VIDEO_MODE_SVGA_800x600_72
  localparam H_ACTIVE = 800;
  localparam H_FP     = 56;
  localparam H_SYNC   = 120;
  localparam H_BP     = 64;

  localparam V_ACTIVE = 600;
  localparam V_FP     = 37;
  localparam V_SYNC   = 6;
  localparam V_BP     = 23;
`endif

// ============================================================================
// XGA 1024×768 @ 60Hz
// ============================================================================
// Pixel clock: 65 MHz
// Horizontal frequency: 48.4 kHz
// Vertical frequency: 60.0 Hz
// VESA standard timing

`ifdef VIDEO_MODE_XGA_1024x768_60
  localparam H_ACTIVE = 1024;
  localparam H_FP     = 24;
  localparam H_SYNC   = 136;
  localparam H_BP     = 160;

  localparam V_ACTIVE = 768;
  localparam V_FP     = 3;
  localparam V_SYNC   = 6;
  localparam V_BP     = 29;
`endif

// ============================================================================
// Computed Parameters (automatically derived from above)
// ============================================================================

localparam H_BLANK = H_FP + H_SYNC + H_BP;
localparam V_BLANK = V_FP + V_SYNC + V_BP;
localparam H_TOTAL = H_ACTIVE + H_BLANK;
localparam V_TOTAL = V_ACTIVE + V_BLANK;

// Bit widths for counters (computed at elaboration time)
localparam H_COUNTER_WIDTH = $clog2(H_TOTAL);
localparam V_COUNTER_WIDTH = $clog2(V_TOTAL);
localparam X_COORD_WIDTH = $clog2(H_ACTIVE);
localparam Y_COORD_WIDTH = $clog2(V_ACTIVE);

`endif // VIDEOMODE_VH
