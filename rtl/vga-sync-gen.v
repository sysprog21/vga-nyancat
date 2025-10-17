// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

`default_nettype none
// VGA Sync Generator: 640×480 @ 72Hz
//
// Generates horizontal and vertical sync pulses plus pixel coordinates for
// VESA standard VGA timing. Outputs are synchronized to the pixel clock.
//
// Timing specification:
//   Horizontal: FP(24) + SYNC(40) + BP(128) + ACTIVE(640) = 832 px/line
//   Vertical:   FP(9)  + SYNC(3)  + BP(28)  + ACTIVE(480) = 520 lines/frame
//   Frame rate: 31.5 MHz / (832 × 520) = 72.016 Hz
//
// Sync signals are active-low. Pixel coordinates (x_px, y_px) are valid
// only when activevideo is high.
module vga_sync_gen (
    input  wire       px_clk,      // Pixel clock (31.5 MHz)
    input  wire       reset,       // Synchronous reset
    output wire       hsync,       // Horizontal sync (active low)
    output wire       vsync,       // Vertical sync (active low)
    output reg  [9:0] x_px,        // Pixel X coordinate [0, 639]
    output reg  [9:0] y_px,        // Pixel Y coordinate [0, 479]
    output wire       activevideo  // High during visible display region
);
    // VGA timing parameters (VESA 640×480 @ 72Hz standard)
    localparam H_FP = 24, H_SYNC = 40, H_BP = 128, H_ACTIVE = 640;  // Horizontal
    localparam V_FP = 9, V_SYNC = 3, V_BP = 28, V_ACTIVE = 480;  // Vertical
    localparam H_BLANK = H_FP + H_SYNC + H_BP;  // 192 px
    localparam V_BLANK = V_FP + V_SYNC + V_BP;  // 40 lines
    localparam H_TOTAL = H_BLANK + H_ACTIVE;  // 832 px
    localparam V_TOTAL = V_BLANK + V_ACTIVE;  // 520 lines

    // Scanning position counters (include blanking intervals)
    reg [9:0] hc, vc;  // hc: [0, 831], vc: [0, 519]

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
            x_px <= hc - H_BLANK;  // Valid range: [0, 639]
            y_px <= vc - V_BLANK;  // Valid range: [0, 479]
        end
    end

    // Sync pulse generation (active low during sync periods)
    assign hsync = (hc >= H_FP && hc < H_FP + H_SYNC) ? 0 : 1;
    assign vsync = (vc >= V_FP && vc < V_FP + V_SYNC) ? 0 : 1;

    // Active video flag: high only when both counters are in visible region
    assign activevideo = (hc >= H_BLANK && vc >= V_BLANK);
endmodule
`default_nettype wire
