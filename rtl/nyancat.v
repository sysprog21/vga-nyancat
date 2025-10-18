// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

`default_nettype none

// Include parameterized video mode definitions
`include "videomode.vh"

// Include memory interface definitions
`include "memory_if.vh"

// Nyancat Animation Display Module
//
// Hardware-accelerated Nyancat (Pop-Tart Cat) animation renderer with real-time
// scaling and frame sequencing. Reads pre-compressed animation data from ROM
// and outputs VGA-compatible color signals synchronized to pixel clock.
//
// Architecture:
//   - 12-frame animation stored as 64×64 4-bit character indices
//   - Auto-scaling based on vertical resolution (SCALE = V_ACTIVE / FRAME_H)
//   - Nearest-neighbor upscaling with horizontal centering
//   - 2-stage pipeline: ROM address → frame ROM → palette ROM → color output
//   - Frame sequencing: ~11 fps (90ms/frame at 31.5MHz pixel clock)
//
// Memory layout:
//   frame_mem[49,152×4b]: Character indices for all 12 frames (2 chars/byte)
//   color_mem[16×6b]:     14-color palette (6-bit VGA: RRGGBB)
//   Total ROM: ~24KB (230× compression vs. full 24-bit RGB storage)
//
// Data flow:
//   {x_px, y_px} → coord transform → ROM address → char_idx → color → rrggbb
module nyancat (
    input  wire                     px_clk,       // Pixel clock (mode-dependent)
    input  wire                     reset,        // Synchronous reset
    input  wire [X_COORD_WIDTH-1:0] x_px,         // Current pixel X [0, H_ACTIVE-1]
    input  wire [Y_COORD_WIDTH-1:0] y_px,         // Current pixel Y [0, V_ACTIVE-1]
    input  wire                     activevideo,  // High during active display region
    output wire [              5:0] rrggbb        // 6-bit VGA color output (2R2G2B)
);
    // =========================================================================
    // Configuration Parameters
    // =========================================================================

    // Display geometry (uses video mode parameters from videomode.vh)
    localparam FRAME_W = 64, FRAME_H = 64;  // Source frame size

    // Auto-scale factor based on vertical resolution (maximize display size)
    // Target: use full vertical height while maintaining integer scaling
    localparam SCALE = V_ACTIVE / FRAME_H;  // Integer division for pixel-perfect scaling
    localparam SCALE_SHIFT = $clog2(SCALE);  // Log2 of scale for bit shifting

    localparam SCALED_W = FRAME_W * SCALE, SCALED_H = FRAME_H * SCALE;
    // Use H_ACTIVE and V_ACTIVE from videomode.vh instead of hardcoded values
    localparam OFFSET_X = (H_ACTIVE - SCALED_W) / 2, OFFSET_Y = 0;  // Centering offsets

    // Animation timing
    localparam NUM_FRAMES = 12;  // Total animation frames
    localparam FRAME_ADDR_W = $clog2(NUM_FRAMES * FRAME_W * FRAME_H);  // 16-bit ROM address
    localparam FRAME_PERIOD = 2_835_000;  // Clocks per frame (~90ms)

    // =========================================================================
    // Frame Sequencing
    // =========================================================================

    reg [21:0] frame_counter;  // Counts clocks within current frame
    reg [ 3:0] frame_index;  // Current frame number [0, 11]

    // Advance to next frame every FRAME_PERIOD clocks (creates ~11 fps animation)
    always @(posedge px_clk) begin
        if (reset) begin
            frame_counter <= 0;
            frame_index   <= 0;
        end else begin
            if (frame_counter >= FRAME_PERIOD - 1) begin
                frame_counter <= 0;
                frame_index   <= (frame_index == NUM_FRAMES - 1) ? 0 : frame_index + 1;
            end else begin
                frame_counter <= frame_counter + 1;
            end
        end
    end

    // =========================================================================
    // Coordinate Transformation and ROM Addressing
    // =========================================================================
    // Transform input pixel coordinates to ROM addresses:
    //   1. Remove centering offset → relative coordinates [0, SCALED_W-1]
    //   2. Descale by SCALE factor → source frame coordinates [0,63]×[0,63]
    //   3. Calculate ROM address from frame index and source coordinates

    // Step 1: Remove centering offset to get coordinates relative to animation area
    // Bounds check first to avoid underflow, then compute relative coordinates
    /* verilator lint_off WIDTHEXPAND */
    wire                     in_display_x = (x_px >= OFFSET_X) && (x_px < OFFSET_X + SCALED_W);
    /* verilator lint_on WIDTHEXPAND */
    /* verilator lint_off UNSIGNED */
    wire                     in_display_y = (y_px >= OFFSET_Y) && (y_px < OFFSET_Y + SCALED_H);
    /* verilator lint_on UNSIGNED */
    wire                     in_display = in_display_x && in_display_y;

    // Relative coordinates (valid only when in_display is high)
    /* verilator lint_off WIDTHTRUNC */
    wire [X_COORD_WIDTH-1:0] rel_x = x_px - OFFSET_X;
    wire [Y_COORD_WIDTH-1:0] rel_y = y_px - OFFSET_Y;
    /* verilator lint_on WIDTHTRUNC */

    // Step 2: Descale coordinates by SCALE factor to map to source frame [0,63]
    // Division by constant SCALE - synthesis tools optimize to efficient hardware
    // (bit-shifts for power-of-2, shift-add sequences for other values)
    /* verilator lint_off WIDTHTRUNC */
    wire [              5:0] src_x = rel_x / SCALE, src_y = rel_y / SCALE;
    /* verilator lint_on WIDTHTRUNC */

    // Step 3: Calculate ROM address using frame index and source coordinates
    // Formula: addr = (frame_index * FRAME_W * FRAME_H) + (src_y * FRAME_W) + src_x
    // Synthesis tools automatically optimize multiplies by power-of-2 constants
    // into shift operations, so explicit bit manipulation is unnecessary
    localparam FRAME_SIZE = FRAME_W * FRAME_H;  // 4096 4-bit entries (2048 bytes)
    /* verilator lint_off WIDTHEXPAND */
    wire [FRAME_ADDR_W-1:0] frame_addr = (frame_index * FRAME_SIZE) + (src_y * FRAME_W) + src_x;
    /* verilator lint_on WIDTHEXPAND */

    // =========================================================================
    // Memory Storage (abstracted interface for future bus protocol support)
    // =========================================================================

    // Frame data: 4-bit character indices (0-13) for all animation frames
    // Organized as: frame[0] (4096 entries), frame[1] (4096 entries), ..., frame[11]
    // Memory interface: ROM (current), future: Wishbone/AXI
    reg [`FRAME_MEM_DATA_WIDTH-1:0] frame_mem[0:(NUM_FRAMES * FRAME_W * FRAME_H)-1];

    // Color palette: 14 VGA colors encoded as 6-bit RRGGBB
    // Memory interface: ROM (current), future: Wishbone/AXI
    reg [`PALETTE_MEM_DATA_WIDTH-1:0] color_mem[0:15];

    // Load pre-generated animation data using abstract memory interface
    `MEM_INIT(frame_mem, "nyancat-frames.hex")
    `MEM_INIT(color_mem, "nyancat-colors.hex")

    // =========================================================================
    // 2-Stage Pipeline for Memory Read Latency
    // =========================================================================
    // Pipeline is necessary because memory reads require 1 clock cycle latency.
    // Stage 1: Read character index from frame memory using computed address
    // Stage 2: Read final color from palette memory using character index
    // Both stages must propagate the in_display flag to maintain sync with data.
    //
    // Note: This pipeline structure remains compatible with future bus protocols
    // (Wishbone/AXI) that also have 1-cycle read latency.

    reg [`FRAME_MEM_DATA_WIDTH-1:0] char_idx_q;  // Stage 1 output: Character index
    reg in_display_q, in_display_q2;  // Display area flag pipelined through both stages
    reg [`PALETTE_MEM_DATA_WIDTH-1:0] color_q;  // Stage 2 output: Final color value

    // Pipeline datapath: Memory addressing → char lookup → color lookup
    // Note: Reset values omitted for datapath registers - in_display flags
    // ensure correct output gating regardless of pipeline register contents
    always @(posedge px_clk) begin
        if (reset) begin
            in_display_q  <= 0;
            in_display_q2 <= 0;
        end else begin
            // Stage 1: Fetch character index using abstract memory interface
            `MEM_READ(char_idx_q, frame_mem, frame_addr);
            in_display_q <= in_display;
            // Stage 2: Fetch final color using abstract memory interface
            `MEM_READ(color_q, color_mem, char_idx_q);
            in_display_q2 <= in_display_q;
        end
    end

    // =========================================================================
    // Output Generation
    // =========================================================================
    // Output color only during active video region and when pixel is within
    // the animation display area. Output black (6'b0) for all other pixels.

    assign rrggbb = (activevideo && in_display_q2) ? color_q : 6'b0;

    // =========================================================================
    // Verification Assertions (Verilator simulation only)
    // =========================================================================
    // These assertions validate critical design invariants during simulation.
    // They are excluded from synthesis to avoid any hardware impact.

`ifndef SYNTHESIS
    // Track valid signal history for $past() function
    reg past_valid = 0;
    always @(posedge px_clk) past_valid <= 1;

    // Assertion 1: Pipeline stage 1 propagates in_display correctly
    // in_display_q should always match the previous cycle's in_display
    // Guard with !$past(reset) to avoid false positives during reset release
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset)) begin
            if (in_display_q !== $past(in_display))
                $error("[ASSERTION FAILED] Pipeline stage 1: in_display_q mismatch");
        end

    // Assertion 2: Pipeline stage 2 propagates in_display_q correctly
    // in_display_q2 should always match the previous cycle's in_display_q
    // Guard with !$past(reset) to avoid false positives during reset release
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset)) begin
            if (in_display_q2 !== $past(in_display_q))
                $error("[ASSERTION FAILED] Pipeline stage 2: in_display_q2 mismatch");
        end

    // Assertion 3: Coordinate bounds check during active display
    // Note: activevideo is combinational from vga_sync_gen, while x_px/y_px are registered
    // Check against previous cycle's activevideo to account for timing skew
    /* verilator lint_off WIDTHEXPAND */
    always @(posedge px_clk)
        if (past_valid && !reset && $past(activevideo)) begin
            if (x_px >= H_ACTIVE)
                $error("[ASSERTION FAILED] x_px=%0d exceeds H_ACTIVE=%0d", x_px, H_ACTIVE);
            if (y_px >= V_ACTIVE)
                $error("[ASSERTION FAILED] y_px=%0d exceeds V_ACTIVE=%0d", y_px, V_ACTIVE);
        end
    /* verilator lint_on WIDTHEXPAND */

    // Assertion 4: Frame index must stay within valid range [0, 11]
    always @(posedge px_clk)
        if (!reset) begin
            if (frame_index >= NUM_FRAMES)
                $error(
                    "[ASSERTION FAILED] frame_index=%0d exceeds NUM_FRAMES=%0d",
                    frame_index,
                    NUM_FRAMES
                );
        end

    // Assertion 5: Character index should be valid (0-13) when in display
    // Note: Index 14-15 are unused but not errors (palette has 16 entries)
    // Guard with !$past(reset) and !$isunknown to avoid spurious warnings
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(
                reset
            ) && $past(
                in_display
            ) && !$isunknown(
                char_idx_q
            )) begin
            if (char_idx_q > 13)
                $warning(
                    "[ASSERTION WARNING] char_idx_q=%0d uses reserved palette entry", char_idx_q
                );
        end

    // Assertion 6: ROM address must stay within allocated memory bounds
    // Check both current cycle (defensive) and previous cycle (actual ROM read timing)
    always @(posedge px_clk)
        if (!reset && in_display) begin
            if (frame_addr >= NUM_FRAMES * FRAME_SIZE)
                $error(
                    "[ASSERTION FAILED] frame_addr=%0d exceeds ROM size=%0d",
                    frame_addr,
                    NUM_FRAMES * FRAME_SIZE
                );
        end

    // Assertion 7: ROM address timing alignment (check at actual cycle of use)
    // frame_addr from previous cycle is what gets read into char_idx_q this cycle
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset) && $past(in_display)) begin
            if ($past(frame_addr) >= NUM_FRAMES * FRAME_SIZE)
                $error(
                    "[ASSERTION FAILED] ROM read with frame_addr=%0d exceeds size=%0d",
                    $past(
                        frame_addr
                    ),
                    NUM_FRAMES * FRAME_SIZE
                );
        end

    // Assertion 8: Output alignment - verify rrggbb matches pipeline correctly
    // rrggbb is combinational: assign rrggbb = (activevideo && in_display_q2) ? color_q : 6'b0
    // Check current cycle's activevideo and in_display_q2 against current rrggbb
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset)) begin
            if (activevideo && in_display_q2) begin
                if (rrggbb !== color_q)
                    $error(
                        "[ASSERTION FAILED] Output misalignment: rrggbb=%h should match color_q=%h",
                        rrggbb,
                        color_q
                    );
            end else begin
                if (rrggbb !== 6'b0)
                    $error(
                        "[ASSERTION FAILED] Output should be zero outside display, got rrggbb=%h",
                        rrggbb
                    );
            end
        end

    // Assertion 9: Coordinate transformation validity (defense-in-depth)
    // When in_display=1, verify that coordinate transformations are correct:
    // - src_x and src_y must be within [0, FRAME_W-1] and [0, FRAME_H-1]
    // - rel_x and rel_y must be within [0, SCALED_W-1] and [0, SCALED_H-1]
    // This prevents wild pointer crashes in framebuffer updates
    /* verilator lint_off WIDTHEXPAND */
    always @(posedge px_clk)
        if (past_valid && !reset && !$past(reset) && in_display) begin
            if (src_x >= FRAME_W)
                $error(
                    "[ASSERTION FAILED] src_x=%0d exceeds FRAME_W=%0d (in_display=1)",
                    src_x,
                    FRAME_W
                );
            if (src_y >= FRAME_H)
                $error(
                    "[ASSERTION FAILED] src_y=%0d exceeds FRAME_H=%0d (in_display=1)",
                    src_y,
                    FRAME_H
                );
            if (rel_x >= SCALED_W)
                $error(
                    "[ASSERTION FAILED] rel_x=%0d exceeds SCALED_W=%0d (in_display=1)",
                    rel_x,
                    SCALED_W
                );
            if (rel_y >= SCALED_H)
                $error(
                    "[ASSERTION FAILED] rel_y=%0d exceeds SCALED_H=%0d (in_display=1)",
                    rel_y,
                    SCALED_H
                );
        end
    /* verilator lint_on WIDTHEXPAND */
`endif

endmodule
`default_nettype wire
