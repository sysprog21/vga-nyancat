// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

`default_nettype none

// Include parameterized video mode definitions
`include "videomode.vh"

// VGA Nyancat Display Top Module
//
// Top-level module that combines VGA timing generation with Nyancat animation
// rendering. The VGA sync generator provides pixel coordinates and timing
// signals, which the Nyancat renderer uses to produce color output.
//
// Signal flow:
//   clk → vga_sync_gen → {x_px, y_px, activevideo} → nyancat → rrggbb
//                      ↘ {hsync, vsync}
//
// External interface uses active-low reset (reset_n) but internal modules
// use active-high reset for consistency with typical HDL practice.
module vga_nyancat (
    input  wire       clk,      // Pixel clock (31.5 MHz)
    input  wire       reset_n,  // Active-low reset
    output wire       hsync,    // Horizontal sync to VGA display
    output wire       vsync,    // Vertical sync to VGA display
    output wire [5:0] rrggbb    // 6-bit color output (2R2G2B)
);
    // Internal signals connecting sync generator to animation renderer
    wire [X_COORD_WIDTH-1:0] x_px;  // Current pixel X coordinate from sync generator
    wire [Y_COORD_WIDTH-1:0] y_px;  // Current pixel Y coordinate from sync generator
    wire                     activevideo;  // High when in visible display region

    // VGA timing generator: produces sync signals and pixel coordinates
    vga_sync_gen vga_sync (
        .px_clk     (clk),
        .reset      (!reset_n),
        .hsync      (hsync),
        .vsync      (vsync),
        .x_px       (x_px),
        .y_px       (y_px),
        .activevideo(activevideo)
    );

    // Nyancat animation renderer: generates pixel colors based on coordinates
    nyancat nyan (
        .px_clk     (clk),
        .reset      (!reset_n),
        .x_px       (x_px),
        .y_px       (y_px),
        .activevideo(activevideo),
        .rrggbb     (rrggbb)
    );
endmodule
`default_nettype wire
