// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

`default_nettype none

// Include parameterized video mode definitions
// Define VIDEO_MODE_* before synthesis to select different timing
`include "videomode.vh"

// VGA Sync Generator (Parameterized)
//
// Generates horizontal and vertical sync pulses plus pixel coordinates for
// VESA standard VGA timing. Supports multiple video modes through the
// videomode.vh include file. Outputs are synchronized to the pixel clock.
//
// Current mode timing (from videomode.vh):
//   Horizontal: FP + SYNC + BP + ACTIVE = H_TOTAL px/line
//   Vertical:   FP + SYNC + BP + ACTIVE = V_TOTAL lines/frame
//
// Sync signals are active-low. Pixel coordinates (x_px, y_px) are valid
// only when activevideo is high.
module vga_sync_gen (
    input  wire                     px_clk,      // Pixel clock (mode-dependent)
    input  wire                     reset,       // Synchronous reset
    output wire                     hsync,       // Horizontal sync (active low)
    output wire                     vsync,       // Vertical sync (active low)
    output reg  [X_COORD_WIDTH-1:0] x_px,        // Pixel X coordinate [0, H_ACTIVE-1]
    output reg  [Y_COORD_WIDTH-1:0] y_px,        // Pixel Y coordinate [0, V_ACTIVE-1]
    output wire                     activevideo  // High during visible display region
);
    // Video mode parameters imported from videomode.vh:
    //   H_ACTIVE, H_FP, H_SYNC, H_BP, H_BLANK, H_TOTAL, H_COUNTER_WIDTH
    //   V_ACTIVE, V_FP, V_SYNC, V_BP, V_BLANK, V_TOTAL, V_COUNTER_WIDTH
    //   X_COORD_WIDTH, Y_COORD_WIDTH

    // Scanning position counters (include blanking intervals)
    reg [H_COUNTER_WIDTH-1:0] hc;  // Horizontal counter: [0, H_TOTAL-1]
    reg [V_COUNTER_WIDTH-1:0] vc;  // Vertical counter: [0, V_TOTAL-1]

    // Raster scanning: left-to-right, top-to-bottom with wraparound
    always @(posedge px_clk) begin
        if (reset) begin
            hc   <= 0;
            vc   <= 0;
            x_px <= 0;
            y_px <= 0;
        end else begin
            // Horizontal counter: increment each clock, wrap at end of line
            if (hc < H_TOTAL - 1) begin
                hc <= hc + 1;
            end else begin
                hc <= 0;
                // Vertical counter: increment at end of each line
                vc <= (vc < V_TOTAL - 1) ? vc + 1 : 0;
            end
            // Pixel coordinates: subtract blanking offset (valid when activevideo=1)
            /* verilator lint_off WIDTHTRUNC */
            x_px <= hc - H_BLANK;  // Valid range: [0, H_ACTIVE-1]
            y_px <= vc - V_BLANK;  // Valid range: [0, V_ACTIVE-1]
            /* verilator lint_on WIDTHTRUNC */
        end
    end

    // Sync pulse generation (active low during sync periods)
    assign hsync = (hc >= H_FP && hc < H_FP + H_SYNC) ? 0 : 1;
    assign vsync = (vc >= V_FP && vc < V_FP + V_SYNC) ? 0 : 1;

    // Active video flag: high only when both counters are in visible region
    assign activevideo = (hc >= H_BLANK && vc >= V_BLANK);
endmodule
`default_nettype wire
