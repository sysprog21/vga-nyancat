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
//   - Real-time 8× nearest-neighbor scaling to 512×512 display resolution
//   - Centered positioning on 640×480 VGA display (64px left margin, top-aligned)
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
    localparam FRAME_W = 64, FRAME_H = 64, SCALE = 8;  // Source size and scale factor
    localparam SCALED_W = FRAME_W * SCALE, SCALED_H = FRAME_H * SCALE;  // 512×512
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
    // Transform input pixel coordinates [0,639]×[0,479] to ROM addresses:
    //   1. Remove centering offset → relative coordinates
    //   2. Descale by factor of 8 → source frame coordinates [0,63]×[0,63]
    //   3. Calculate ROM address from frame index and source coordinates

    // Step 1: Remove centering offset to get coordinates relative to animation area
    // Use signed arithmetic with sufficient width to handle all video modes
    wire signed [X_COORD_WIDTH:0] rel_x_signed = $signed({1'b0, x_px}) - OFFSET_X;
    wire signed [Y_COORD_WIDTH:0] rel_y_signed = $signed({1'b0, y_px}) - OFFSET_Y;
    wire [X_COORD_WIDTH-1:0] rel_x = rel_x_signed[X_COORD_WIDTH-1:0];
    wire [Y_COORD_WIDTH-1:0] rel_y = rel_y_signed[Y_COORD_WIDTH-1:0];

    // Check if current pixel falls within the scaled animation display area
    /* verilator lint_off UNSIGNED */
    wire in_display = (x_px >= OFFSET_X) && (x_px < OFFSET_X + SCALED_W) &&
        (y_px < OFFSET_Y + SCALED_H);  // y_px >= 0 always true
    /* verilator lint_on UNSIGNED */

    // Step 2: Descale coordinates (divide by 8) to map to source frame [0,63]
    // Shift right by 3 bits (divide by 8) and take lower 6 bits for 64×64 frame
    /* verilator lint_off WIDTHTRUNC */
    wire [5:0] src_x = rel_x[8:3], src_y = rel_y[8:3];  // Right shift 3 = divide by 8
    /* verilator lint_on WIDTHTRUNC */

    // Step 3: Calculate ROM address using frame index and source coordinates
    // Formula: addr = (frame_index * 4096) + (src_y * 64) + src_x
    // Optimized with bit concatenation and OR instead of multiply/add:
    //   frame_index * 4096 = frame_index << 12
    //   src_y * 64 = src_y << 6
    /* verilator lint_off WIDTHEXPAND */
    wire [FRAME_ADDR_W-1:0] frame_addr = {frame_index, 12'b0} | {src_y, 6'b0} | src_x;
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
    always @(posedge px_clk) begin
        if (reset) begin
            char_idx_q <= 0;
            in_display_q <= 0;
            color_q <= 0;
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
endmodule
`default_nettype wire
