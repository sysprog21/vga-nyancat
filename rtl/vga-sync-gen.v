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

    // =========================================================================
    // Verification Assertions (Verilator simulation only)
    // =========================================================================
    // These assertions validate VGA timing generation correctness.
    // Excluded from synthesis to avoid any hardware impact.

`ifndef SYNTHESIS
    // Track valid signal history for $past() function
    reg past_valid = 0;
    always @(posedge px_clk) past_valid <= 1;

    // Assertion 1: Horizontal counter wraparound at H_TOTAL boundary
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset)) begin
            if ($past(hc) == H_TOTAL - 1 && hc != 0)
                $error("[ASSERTION FAILED] hc should wrap to 0 after H_TOTAL-1, got %0d", hc);
            if ($past(hc) < H_TOTAL - 1 && hc != $past(hc) + 1)
                $error(
                    "[ASSERTION FAILED] hc should increment by 1, was %0d now %0d", $past(hc), hc
                );
        end

    // Assertion 2: Vertical counter wraparound at V_TOTAL boundary
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset) && $past(hc) == H_TOTAL - 1) begin
            if ($past(vc) == V_TOTAL - 1 && vc != 0)
                $error("[ASSERTION FAILED] vc should wrap to 0 after V_TOTAL-1, got %0d", vc);
            if ($past(vc) < V_TOTAL - 1 && vc != $past(vc) + 1)
                $error(
                    "[ASSERTION FAILED] vc should increment by 1, was %0d now %0d", $past(vc), vc
                );
        end

    // Assertion 3: Counter bounds - should never exceed total values
    always @(posedge px_clk)
        if (!reset) begin
            if (hc >= H_TOTAL) $error("[ASSERTION FAILED] hc=%0d exceeds H_TOTAL=%0d", hc, H_TOTAL);
            if (vc >= V_TOTAL) $error("[ASSERTION FAILED] vc=%0d exceeds V_TOTAL=%0d", vc, V_TOTAL);
        end

    // Assertion 4: Hsync timing correctness
    // hsync should be low only during [H_FP, H_FP+H_SYNC) range
    always @(posedge px_clk)
        if (!reset) begin
            if (hc >= H_FP && hc < H_FP + H_SYNC) begin
                if (hsync !== 0) $error("[ASSERTION FAILED] hsync should be low at hc=%0d", hc);
            end else begin
                if (hsync !== 1) $error("[ASSERTION FAILED] hsync should be high at hc=%0d", hc);
            end
        end

    // Assertion 5: Vsync timing correctness
    // vsync should be low only during [V_FP, V_FP+V_SYNC) range
    always @(posedge px_clk)
        if (!reset) begin
            if (vc >= V_FP && vc < V_FP + V_SYNC) begin
                if (vsync !== 0) $error("[ASSERTION FAILED] vsync should be low at vc=%0d", vc);
            end else begin
                if (vsync !== 1) $error("[ASSERTION FAILED] vsync should be high at vc=%0d", vc);
            end
        end

    // Assertion 6: Activevideo timing correctness
    // activevideo should be high only when both hc >= H_BLANK and vc >= V_BLANK
    always @(posedge px_clk)
        if (!reset) begin
            if (hc >= H_BLANK && vc >= V_BLANK) begin
                if (!activevideo)
                    $error(
                        "[ASSERTION FAILED] activevideo should be high at hc=%0d vc=%0d", hc, vc
                    );
            end else begin
                if (activevideo)
                    $error("[ASSERTION FAILED] activevideo should be low at hc=%0d vc=%0d", hc, vc);
            end
        end

    // Assertion 7: Pixel coordinates validity during active video
    // Note: x_px and y_px are registered (1-cycle delayed from hc/vc)
    // Check against previous cycle's activevideo to account for timing
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset) && $past(activevideo)) begin
            if (x_px >= H_ACTIVE)
                $error(
                    "[ASSERTION FAILED] x_px=%0d exceeds H_ACTIVE=%0d (prev activevideo)",
                    x_px,
                    H_ACTIVE
                );
            if (y_px >= V_ACTIVE)
                $error(
                    "[ASSERTION FAILED] y_px=%0d exceeds V_ACTIVE=%0d (prev activevideo)",
                    y_px,
                    V_ACTIVE
                );
        end
`endif

endmodule
`default_nettype wire
