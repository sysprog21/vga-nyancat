// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.
//
// This simulates Nyancat animation on VGA display using Verilator for RTL
// simulation and SDL2 for graphics rendering. Supports both interactive
// mode and single-frame PNG export for automated testing.
//
// Architecture:
//   1. Verilator simulates RTL at pixel clock rate (31.5MHz)
//   2. VGA timing generates sync signals and pixel coordinates
//   3. SDL framebuffer updated during active display regions
//   4. SDL texture refreshed once per frame for display

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "Vvga_nyancat.h"
#include "verilated.h"
#include "verilated_vcd_c.h"  // For VCD waveform tracing

// Video mode configuration (must match RTL videomode.vh settings)
// Default: VGA 640×480 @ 72Hz
// To use different modes, define VIDEO_MODE_* in Makefile and recompile

#if !defined(VIDEO_MODE_VGA_640x480_72) &&  \
    !defined(VIDEO_MODE_VGA_640x480_60) &&  \
    !defined(VIDEO_MODE_VGA_800x600_60) &&  \
    !defined(VIDEO_MODE_SVGA_800x600_72) && \
    !defined(VIDEO_MODE_XGA_1024x768_60)
// Default to VGA 640×480 @ 72Hz if no mode specified
#define VIDEO_MODE_VGA_640x480_72
#endif

// Video mode timing parameters (must match videomode.vh)
#if defined(VIDEO_MODE_VGA_640x480_72)
constexpr int H_RES = 640, V_RES = 480;
constexpr int H_FP = 24, H_SYNC = 40, H_BP = 128;
constexpr int V_FP = 9, V_SYNC = 3, V_BP = 28;
constexpr const char *MODE_NAME = "VGA 640x480 @ 72Hz";
#elif defined(VIDEO_MODE_VGA_640x480_60)
constexpr int H_RES = 640, V_RES = 480;
constexpr int H_FP = 16, H_SYNC = 96, H_BP = 48;
constexpr int V_FP = 10, V_SYNC = 2, V_BP = 33;
constexpr const char *MODE_NAME = "VGA 640x480 @ 60Hz";
#elif defined(VIDEO_MODE_VGA_800x600_60)
constexpr int H_RES = 800, V_RES = 600;
constexpr int H_FP = 40, H_SYNC = 128, H_BP = 88;
constexpr int V_FP = 1, V_SYNC = 4, V_BP = 23;
constexpr const char *MODE_NAME = "SVGA 800x600 @ 60Hz";
#elif defined(VIDEO_MODE_SVGA_800x600_72)
constexpr int H_RES = 800, V_RES = 600;
constexpr int H_FP = 56, H_SYNC = 120, H_BP = 64;
constexpr int V_FP = 37, V_SYNC = 6, V_BP = 23;
constexpr const char *MODE_NAME = "SVGA 800x600 @ 72Hz";
#elif defined(VIDEO_MODE_XGA_1024x768_60)
constexpr int H_RES = 1024, V_RES = 768;
constexpr int H_FP = 24, H_SYNC = 136, H_BP = 160;
constexpr int V_FP = 3, V_SYNC = 6, V_BP = 29;
constexpr const char *MODE_NAME = "XGA 1024x768 @ 60Hz";
#endif

// Computed timing values
constexpr int H_BLANKING = H_FP + H_SYNC + H_BP;
constexpr int V_BLANKING = V_FP + V_SYNC + V_BP;
constexpr int H_TOTAL = H_RES + H_BLANKING;
constexpr int V_TOTAL = V_RES + V_BLANKING;
constexpr int CLOCKS_PER_FRAME = H_TOTAL * V_TOTAL;

// Color conversion: 2-bit VGA channel → 8-bit RGB
// Maps 2-bit color values to 8-bit with even spacing:
//   0b00 → 0   (0%)
//   0b01 → 85  (33%)
//   0b10 → 170 (67%)
//   0b11 → 255 (100%)
// This provides better color fidelity than simple left-shift (×64)
constexpr uint8_t vga2bit_to_8bit(uint8_t val)
{
    return val * 85;  // Compiler optimizes to shift+add
}

// VGA Timing Monitor: Real-time validation of sync signals and frame dimensions
//
// Edge-detection based measurement:
//   - Hsync pulse width measured in clocks (falling edge to rising edge)
//   - Vsync pulse width measured in lines (counted via hsync falling edges)
//   - H_TOTAL/V_TOTAL measured from falling edge to falling edge
//   - Active video dimensions tracked separately
//
// Design principles:
//   - Skip first incomplete periods (use hsync_seen/vsync_seen flags)
//   - Tolerance: ±1 clock/line for jitter handling
//   - Validate at edge boundaries (avoid continuous counting errors)
//   - Silent mode: only report first frame errors (avoid spam)
class TimingMonitor
{
private:
    const int expected_h_sync = H_SYNC, expected_v_sync = V_SYNC;
    const int expected_h_total = H_TOTAL, expected_v_total = V_TOTAL;
    const int expected_h_active = H_RES, expected_v_active = V_RES;

    int h_counter = 0, v_counter = 0;
    int hsync_pulse_width = 0, vsync_pulse_lines = 0;
    int h_active_width = 0, v_active_lines = 0;
    bool prev_hsync = true, prev_vsync = true;
    bool in_hsync_pulse = false, in_vsync_pulse = false;
    bool hsync_seen = false, vsync_seen = false;
    bool frame_complete = false, first_sample = true;

    int hsync_errors = 0, vsync_errors = 0;
    int h_total_errors = 0, v_total_errors = 0;
    int h_active_errors = 0, v_active_errors = 0;
    bool silent_mode = false;

    static constexpr int TOLERANCE = 1;

    bool within_tolerance(int measured, int expected)
    {
        return (measured >= expected - TOLERANCE &&
                measured <= expected + TOLERANCE);
    }

public:
    TimingMonitor() = default;

    void tick(bool hsync, bool vsync, bool activevideo)
    {
        // Handle first sample to initialize prev_* from actual signals.
        // Avoids spurious falling edge detection if signals start low
        if (first_sample) {
            prev_hsync = hsync;
            prev_vsync = vsync;
            first_sample = false;
            return;
        }

        // Detect edges up front for clear logic flow
        bool h_fall = !hsync && prev_hsync;
        bool h_rise = hsync && !prev_hsync;
        bool v_fall = !vsync && prev_vsync;
        bool v_rise = vsync && !prev_vsync;

        // Process vsync edges FIRST (before hsync to avoid off-by-one)
        if (v_fall) {
            if (vsync_seen) {
                // Validate previous complete frame
                if (!within_tolerance(v_counter, expected_v_total)) {
                    if (!silent_mode) {
                        fprintf(
                            stderr,
                            "WARNING: Vertical total error: measured %d lines, "
                            "expected %d (+-%d)\n",
                            v_counter, expected_v_total, TOLERANCE);
                    }
                    v_total_errors++;
                }

                // Check active lines count
                if (v_active_lines > 0) {
                    if (!within_tolerance(v_active_lines, expected_v_active)) {
                        if (!silent_mode) {
                            fprintf(stderr,
                                    "WARNING: Active video lines error: "
                                    "measured %d "
                                    "lines, expected %d (+-%d)\n",
                                    v_active_lines, expected_v_active,
                                    TOLERANCE);
                        }
                        v_active_errors++;
                    }
                }

                frame_complete = true;
                silent_mode = true;  // Only report errors from first frame
            } else {
                vsync_seen = true;
            }

            // Reset frame counters
            v_counter = 0;
            v_active_lines = 0;
            in_vsync_pulse = true;
            vsync_pulse_lines = 0;
        }

        if (v_rise) {
            in_vsync_pulse = false;
            if (vsync_seen &&
                !within_tolerance(vsync_pulse_lines, expected_v_sync)) {
                if (!silent_mode) {
                    fprintf(
                        stderr,
                        "WARNING: Vsync pulse width error: measured %d lines, "
                        "expected %d (+-%d)\n",
                        vsync_pulse_lines, expected_v_sync, TOLERANCE);
                }
                vsync_errors++;
            }
        }

        // Process hsync edges SECOND (after vsync validation)
        if (h_fall) {
            if (hsync_seen) {
                // Validate previous complete line
                if (!within_tolerance(h_counter, expected_h_total)) {
                    if (!silent_mode) {
                        fprintf(stderr,
                                "WARNING: Horizontal total error: measured %d "
                                "clocks, expected %d (+-%d)\n",
                                h_counter, expected_h_total, TOLERANCE);
                    }
                    h_total_errors++;
                }

                // Check active video width
                if (h_active_width > 0) {
                    if (!within_tolerance(h_active_width, expected_h_active)) {
                        if (!silent_mode) {
                            fprintf(stderr,
                                    "WARNING: Active video width error: "
                                    "measured %d "
                                    "pixels, expected %d (+-%d)\n",
                                    h_active_width, expected_h_active,
                                    TOLERANCE);
                        }
                        h_active_errors++;
                    }
                }

                // Increment line counters (safe now that vsync validated first)
                v_counter++;
                if (h_active_width > 0)
                    v_active_lines++;

                // Count vsync pulse width using current vsync state
                if (!vsync)
                    vsync_pulse_lines++;
            } else {
                hsync_seen = true;
            }

            // Reset line counters
            h_counter = 0;
            h_active_width = 0;
            in_hsync_pulse = true;
            hsync_pulse_width = 0;
        }

        if (h_rise) {
            in_hsync_pulse = false;
            if (hsync_seen &&
                !within_tolerance(hsync_pulse_width, expected_h_sync)) {
                if (!silent_mode) {
                    fprintf(
                        stderr,
                        "WARNING: Hsync pulse width error: measured %d clocks, "
                        "expected %d (+-%d)\n",
                        hsync_pulse_width, expected_h_sync, TOLERANCE);
                }
                hsync_errors++;
            }
        }

        // Increment per-cycle counters LAST (after edge processing)
        h_counter++;
        if (!hsync)
            hsync_pulse_width++;
        if (activevideo)
            h_active_width++;

        // Update previous states
        prev_hsync = hsync;
        prev_vsync = vsync;
    }

    void report()
    {
        if (!frame_complete) {
            std::cout << "WARNING: Timing validation incomplete (no full frame "
                         "measured)\n";
            return;
        }

        // Include active video errors in report
        if (hsync_errors == 0 && vsync_errors == 0 && h_total_errors == 0 &&
            v_total_errors == 0 && h_active_errors == 0 &&
            v_active_errors == 0) {
            std::cout << "PASS: VGA timing validation\n";
            std::cout
                << "   All sync pulse widths and frame dimensions correct\n";
        } else {
            std::cout << "FAIL: VGA timing validation\n";
            if (hsync_errors > 0)
                std::cout << "   Hsync errors: " << hsync_errors << "\n";
            if (vsync_errors > 0)
                std::cout << "   Vsync errors: " << vsync_errors << "\n";
            if (h_total_errors > 0)
                std::cout << "   H_TOTAL errors: " << h_total_errors << "\n";
            if (v_total_errors > 0)
                std::cout << "   V_TOTAL errors: " << v_total_errors << "\n";
            if (h_active_errors > 0)
                std::cout << "   H_ACTIVE errors: " << h_active_errors << "\n";
            if (v_active_errors > 0)
                std::cout << "   V_ACTIVE errors: " << v_active_errors << "\n";
        }
    }

    bool has_errors() const
    {
        // Include active video errors in failure detection
        return (hsync_errors > 0 || vsync_errors > 0 || h_total_errors > 0 ||
                v_total_errors > 0 || h_active_errors > 0 ||
                v_active_errors > 0);
    }

    bool is_complete() const { return frame_complete; }
};

// Sync Signal State Validator: Glitch detection and phase-aware diagnostics
//
// Complements TimingMonitor by detecting single-cycle glitches and providing
// detailed phase context for sync signal errors.
//
// Design principles:
//   - Track estimated hc/vc position based on edge counts
//   - Detect unexpected edges (glitches) between valid pulse boundaries
//   - Report errors with phase context (active/blanking region)
//   - Silent after first frame to avoid spam
class SyncValidator
{
private:
    struct PulseTracker {
        int pulse_width;      // Current pulse width (clocks or lines)
        int since_last_edge;  // Clocks/lines since last edge
        int expected_width;   // Expected pulse width
        int error_count;      // Accumulated errors
        bool in_pulse;        // Currently in pulse (low state)
    };

    PulseTracker hsync_state, vsync_state;

    // Position estimation
    int est_hc = 0;  // Estimated horizontal counter
    int est_vc = 0;  // Estimated vertical counter (line count)
    bool first_tick = true;
    bool silent_mode = false;

    // Previous signal states for edge detection
    bool prev_hsync = true, prev_vsync = true;

    // Track if we've seen first edge (to avoid false positives)
    bool hsync_seen = false, vsync_seen = false;

    static constexpr int TOLERANCE = 2;  // Allow ±2 for glitch detection

public:
    SyncValidator()
    {
        // VGA_640x480_72: H_SYNC=40 clocks, V_SYNC=3 lines
        hsync_state = {0, 0, H_SYNC, 0, false};
        vsync_state = {0, 0, V_SYNC, 0, false};
    }

    void tick(bool hsync, bool vsync)
    {
        // Initialize on first tick
        if (first_tick) {
            prev_hsync = hsync;
            prev_vsync = vsync;
            first_tick = false;
            return;
        }

        // Detect edges
        bool h_fall = !hsync && prev_hsync;
        bool h_rise = hsync && !prev_hsync;
        bool v_fall = !vsync && prev_vsync;
        bool v_rise = vsync && !prev_vsync;

        // Process vsync edges first
        if (v_fall) {
            vsync_state.in_pulse = true;
            vsync_state.pulse_width = 0;

            // Check for unexpected edge (glitch detection)
            // Only check after we've seen the first complete vsync
            if (vsync_seen &&
                vsync_state.since_last_edge < V_SYNC * H_TOTAL - TOLERANCE) {
                if (!silent_mode) {
                    fprintf(stderr,
                            "[VSYNC GLITCH] Falling edge too soon at "
                            "est_line=%d (expected ~%d lines between edges)\n",
                            est_vc, V_TOTAL);
                }
                vsync_state.error_count++;
            }

            vsync_seen = true;
            vsync_state.since_last_edge = 0;
            est_vc = 0;  // Reset line counter at vsync
        }

        if (v_rise) {
            vsync_state.in_pulse = false;

            // Validate pulse width (measured in lines, approximated by hsyncs)
            int pulse_lines = vsync_state.pulse_width / H_TOTAL;
            if (pulse_lines < V_SYNC - TOLERANCE ||
                pulse_lines > V_SYNC + TOLERANCE) {
                if (!silent_mode) {
                    fprintf(stderr,
                            "[VSYNC WIDTH ERROR] Pulse width ~%d lines "
                            "(expected %d +-%d)\n",
                            pulse_lines, V_SYNC, TOLERANCE);
                }
                vsync_state.error_count++;
            }

            silent_mode = true;  // Only report first frame
        }

        // Process hsync edges second
        if (h_fall) {
            hsync_state.in_pulse = true;
            hsync_state.pulse_width = 0;

            // Check for unexpected edge (should occur every H_TOTAL clocks)
            // Only check after we've seen the first complete hsync
            if (hsync_seen &&
                hsync_state.since_last_edge < H_TOTAL - TOLERANCE &&
                hsync_state.since_last_edge > 0) {
                const char *phase = (est_hc >= H_FP && est_hc < H_FP + H_SYNC)
                                        ? "SYNC"
                                    : (est_hc >= H_BLANKING) ? "ACTIVE"
                                                             : "BLANK";

                if (!silent_mode) {
                    fprintf(stderr,
                            "[HSYNC GLITCH] Falling edge at est_hc=%d phase=%s "
                            "(expected ~%d clocks between edges)\n",
                            est_hc, phase, H_TOTAL);
                }
                hsync_state.error_count++;
            }

            hsync_seen = true;
            hsync_state.since_last_edge = 0;
            est_hc = 0;  // Reset horizontal counter
            est_vc++;    // Increment line count
        }

        if (h_rise) {
            hsync_state.in_pulse = false;

            // Validate pulse width
            if (hsync_state.pulse_width < H_SYNC - TOLERANCE ||
                hsync_state.pulse_width > H_SYNC + TOLERANCE) {
                const char *phase = (est_hc < H_FP + H_SYNC) ? "FP+SYNC"
                                    : (est_hc < H_BLANKING)  ? "BP"
                                    : (est_hc >= H_BLANKING) ? "ACTIVE"
                                                             : "UNKNOWN";

                if (!silent_mode) {
                    fprintf(stderr,
                            "[HSYNC WIDTH ERROR] Pulse width %d clocks at "
                            "phase=%s (expected %d +-%d)\n",
                            hsync_state.pulse_width, phase, H_SYNC, TOLERANCE);
                }
                hsync_state.error_count++;
            }
        }

        // Update counters
        est_hc++;
        hsync_state.since_last_edge++;

        if (hsync_state.in_pulse)
            hsync_state.pulse_width++;
        if (vsync_state.in_pulse)
            vsync_state.pulse_width++;

        // Wraparound estimation
        if (est_hc >= H_TOTAL)
            est_hc = 0;
        if (est_vc >= V_TOTAL)
            est_vc = 0;

        // Update previous states
        prev_hsync = hsync;
        prev_vsync = vsync;
    }

    void report() const
    {
        if (hsync_state.error_count == 0 && vsync_state.error_count == 0) {
            std::cout
                << "PASS: Sync signal validation (no glitches detected)\n";
        } else {
            std::cout << "FAIL: Sync signal validation\n";
            if (hsync_state.error_count > 0)
                std::cout << "   Hsync glitches/errors: "
                          << hsync_state.error_count << "\n";
            if (vsync_state.error_count > 0)
                std::cout << "   Vsync glitches/errors: "
                          << vsync_state.error_count << "\n";
        }
    }

    bool has_errors() const
    {
        return (hsync_state.error_count > 0 || vsync_state.error_count > 0);
    }

    int get_total_errors() const
    {
        return hsync_state.error_count + vsync_state.error_count;
    }
};

// Coordinate Validator: Defense-in-depth bounds checking for framebuffer access
//
// Validates coordinates before every framebuffer write to prevent wild pointer
// crashes. Complements RTL assertions with C++ side validation.
//
// Design principles:
//   - Validate hpos/vpos against screen resolution before framebuffer access
//   - Accumulate error count and auto-stop at threshold (10 errors)
//   - Report errors with coordinate context for debugging
//   - Silent after first frame to avoid spam
class CoordinateValidator
{
private:
    int error_count = 0;
    bool silent_mode = false;
    bool frame_complete = false;
    static constexpr int ERROR_THRESHOLD = 10;

public:
    CoordinateValidator() = default;

    // Validate coordinates before framebuffer access
    // Returns true if coordinates are valid, false otherwise
    bool validate(int hpos, int vpos, int row_base)
    {
        bool valid = true;

        // Check horizontal bounds
        if (hpos < 0 || hpos >= H_RES) {
            if (!silent_mode && error_count < ERROR_THRESHOLD) {
                fprintf(stderr,
                        "[COORDINATE ERROR] hpos=%d out of bounds [0, %d)\n",
                        hpos, H_RES);
                error_count++;
            }
            valid = false;
        }

        // Check vertical bounds
        if (vpos < 0 || vpos >= V_RES) {
            if (!silent_mode && error_count < ERROR_THRESHOLD) {
                fprintf(stderr,
                        "[COORDINATE ERROR] vpos=%d out of bounds [0, %d)\n",
                        vpos, V_RES);
                error_count++;
            }
            valid = false;
        }

        // Check row_base consistency
        // row_base should match (vpos * H_RES) << 2 when in valid range
        if (vpos >= 0 && vpos < V_RES) {
            int expected_row_base = (vpos * H_RES) << 2;
            if (row_base != expected_row_base) {
                if (!silent_mode && error_count < ERROR_THRESHOLD) {
                    fprintf(stderr,
                            "[COORDINATE ERROR] row_base mismatch: got %d, "
                            "expected %d (vpos=%d)\n",
                            row_base, expected_row_base, vpos);
                    error_count++;
                }
                valid = false;
            }
        }

        // Check if threshold exceeded
        if (error_count >= ERROR_THRESHOLD) {
            if (!silent_mode) {
                fprintf(stderr,
                        "[COORDINATE VALIDATOR] Error threshold reached (%d "
                        "errors), stopping validation\n",
                        ERROR_THRESHOLD);
                silent_mode = true;
            }
        }

        return valid;
    }

    // Mark frame completion (called on vsync)
    void mark_frame_complete()
    {
        if (!frame_complete) {
            frame_complete = true;
            silent_mode = true;  // Only report errors from first frame
        }
    }

    void report() const
    {
        if (error_count == 0) {
            std::cout << "PASS: Coordinate validation (no bounds errors)\n";
        } else {
            std::cout << "FAIL: Coordinate validation\n";
            std::cout << "   Total coordinate errors: " << error_count << "\n";
            if (error_count >= ERROR_THRESHOLD) {
                std::cout << "   (validation stopped at threshold)\n";
            }
        }
    }

    bool has_errors() const { return error_count > 0; }

    int get_error_count() const { return error_count; }
};

// Change Tracker: Frame-to-frame difference detection for rendering
// optimization
//
// Tracks pixel changes between consecutive frames to identify dirty regions.
// Useful for optimized incremental rendering and bandwidth analysis.
//
// Design principles:
//   - Compare current frame against previous frame (full BGRA pixel comparison)
//   - Maintain per-pixel change bitmap for spatial analysis
//   - Tile-based tracking for efficient region updates (configurable tile size)
//   - Heat map tracking for temporal analysis of change patterns
//   - Track statistics: changed pixels, change rate, hotspots
//   - Provide bounding box calculation for minimal update regions
class ChangeTracker
{
private:
    // Tile-based tracking configuration
    static constexpr int TILE_SIZE = 32;  // 32×32 pixel tiles
    static constexpr int TILES_X = (H_RES + TILE_SIZE - 1) / TILE_SIZE;
    static constexpr int TILES_Y = (V_RES + TILE_SIZE - 1) / TILE_SIZE;
    static constexpr int TOTAL_TILES = TILES_X * TILES_Y;

    std::vector<uint8_t> prev_framebuffer;
    std::vector<bool> change_map;
    std::vector<bool> dirty_tiles;   // Per-tile dirty flags
    std::vector<uint32_t> heat_map;  // Change frequency per pixel
    int total_pixels = H_RES * V_RES;
    int changed_pixels = 0;
    int dirty_tile_count = 0;
    int frames_tracked = 0;
    bool first_frame = true;

    // Statistics accumulation
    uint64_t total_changed_pixels = 0;
    int min_changed = total_pixels, max_changed = 0;

    // Bounding box of changes (for dirty rectangle optimization)
    int min_x, max_x, min_y, max_y;

public:
    ChangeTracker()
        : prev_framebuffer(H_RES * V_RES * 4, 0),
          change_map(H_RES * V_RES, false),
          dirty_tiles(TOTAL_TILES, false),
          heat_map(H_RES * V_RES, 0)
    {
    }

    // Track changes between current and previous frame
    // Called once per frame after framebuffer is fully updated
    void track(const uint8_t *current_fb)
    {
        if (first_frame) {
            // Copy initial framebuffer as baseline
            memcpy(prev_framebuffer.data(), current_fb, H_RES * V_RES * 4);
            first_frame = false;
            return;
        }

        // Reset bounding box and tile dirty flags
        min_x = H_RES, max_x = -1;
        min_y = V_RES, max_y = -1;
        changed_pixels = 0;
        dirty_tile_count = 0;
        std::fill(dirty_tiles.begin(), dirty_tiles.end(), false);

        // Per-pixel comparison with BGRA color equality check
        for (int y = 0; y < V_RES; ++y) {
            for (int x = 0; x < H_RES; ++x) {
                int pixel_idx = y * H_RES + x;
                int byte_idx = pixel_idx * 4;

                // Compare all 4 channels (BGRA)
                bool changed =
                    (current_fb[byte_idx] != prev_framebuffer[byte_idx] ||
                     current_fb[byte_idx + 1] !=
                         prev_framebuffer[byte_idx + 1] ||
                     current_fb[byte_idx + 2] !=
                         prev_framebuffer[byte_idx + 2] ||
                     current_fb[byte_idx + 3] !=
                         prev_framebuffer[byte_idx + 3]);

                change_map[pixel_idx] = changed;

                if (changed) {
                    changed_pixels++;

                    // Update heat map (temporal analysis)
                    if (heat_map[pixel_idx] < UINT32_MAX)
                        heat_map[pixel_idx]++;

                    // Update bounding box
                    if (x < min_x)
                        min_x = x;
                    if (x > max_x)
                        max_x = x;
                    if (y < min_y)
                        min_y = y;
                    if (y > max_y)
                        max_y = y;

                    // Mark tile as dirty
                    int tile_x = x / TILE_SIZE;
                    int tile_y = y / TILE_SIZE;
                    int tile_idx = tile_y * TILES_X + tile_x;
                    if (!dirty_tiles[tile_idx]) {
                        dirty_tiles[tile_idx] = true;
                        dirty_tile_count++;
                    }
                }
            }
        }

        // Update statistics
        total_changed_pixels += changed_pixels;
        if (changed_pixels < min_changed)
            min_changed = changed_pixels;
        if (changed_pixels > max_changed)
            max_changed = changed_pixels;

        // Copy current frame as new baseline
        memcpy(prev_framebuffer.data(), current_fb, H_RES * V_RES * 4);
        frames_tracked++;
    }

    void report() const
    {
        if (frames_tracked == 0) {
            std::cout << "Change tracking: No frames tracked\n";
            return;
        }

        double avg_changed =
            frames_tracked > 0
                ? static_cast<double>(total_changed_pixels) / frames_tracked
                : 0.0;
        double avg_change_rate = (100.0 * avg_changed) / total_pixels;

        std::cout << "Change Tracking Report:\n";
        std::cout << "  Frames tracked: " << frames_tracked << "\n";
        std::cout << "  Last frame changes: " << changed_pixels << "/"
                  << total_pixels << " pixels ("
                  << (100.0 * changed_pixels / total_pixels) << "%)\n";
        std::cout << "  Average change rate: " << avg_change_rate << "% ("
                  << static_cast<int>(avg_changed) << " pixels/frame)\n";
        std::cout << "  Change range: [" << min_changed << ", " << max_changed
                  << "] pixels\n";

        // Tile-based statistics
        std::cout << "\nTile-based Analysis (tile size: " << TILE_SIZE << "×"
                  << TILE_SIZE << "):\n";
        std::cout << "  Dirty tiles: " << dirty_tile_count << "/" << TOTAL_TILES
                  << " (" << (100.0 * dirty_tile_count / TOTAL_TILES) << "%)\n";
        std::cout << "  Tile grid: " << TILES_X << "×" << TILES_Y << "\n";

        // Calculate tile update efficiency
        if (dirty_tile_count > 0) {
            int tile_area = dirty_tile_count * TILE_SIZE * TILE_SIZE;
            double tile_efficiency = (100.0 * changed_pixels) / tile_area;
            std::cout << "  Tile update area: " << tile_area << " pixels ("
                      << tile_efficiency << "% utilized)\n";
        }

        // Report bounding box if there were changes in last frame
        if (changed_pixels > 0) {
            int bbox_w = max_x - min_x + 1;
            int bbox_h = max_y - min_y + 1;
            int bbox_area = bbox_w * bbox_h;
            double bbox_efficiency =
                (100.0 * changed_pixels) / bbox_area;  // Fill ratio

            std::cout << "\nDirty Rectangle (bounding box):\n";
            std::cout << "  Position: (" << min_x << ", " << min_y << ") to ("
                      << max_x << ", " << max_y << ")\n";
            std::cout << "  Size: " << bbox_w << "×" << bbox_h << " ("
                      << bbox_area << " pixels, " << bbox_efficiency
                      << "% fill)\n";
        }

        // Heat map analysis (find hottest regions)
        if (frames_tracked > 1) {
            std::cout << "\nHeat Map Analysis:\n";

            // Find top 5 hottest pixels
            std::vector<std::pair<uint32_t, int>> hot_pixels;
            for (int i = 0; i < total_pixels; ++i) {
                if (heat_map[i] > 0) {
                    hot_pixels.push_back({heat_map[i], i});
                }
            }
            std::sort(hot_pixels.rbegin(), hot_pixels.rend());

            int num_changed_pixels = hot_pixels.size();
            std::cout << "  Pixels changed at least once: "
                      << num_changed_pixels << " ("
                      << (100.0 * num_changed_pixels / total_pixels)
                      << "% of total)\n";

            if (!hot_pixels.empty()) {
                int top_n = std::min(5, static_cast<int>(hot_pixels.size()));
                std::cout << "  Top " << top_n << " hottest pixels:\n";
                for (int i = 0; i < top_n; ++i) {
                    int idx = hot_pixels[i].second;
                    int x = idx % H_RES;
                    int y = idx / H_RES;
                    double change_freq =
                        (100.0 * hot_pixels[i].first) / frames_tracked;
                    std::cout << "    " << (i + 1) << ". (" << x << ", " << y
                              << "): " << hot_pixels[i].first << " changes ("
                              << change_freq << "%)\n";
                }
            }
        }
    }

    int get_changed_pixels() const { return changed_pixels; }
    int get_dirty_tile_count() const { return dirty_tile_count; }

    // Get change map for spatial analysis or optimized rendering
    const std::vector<bool> &get_change_map() const { return change_map; }

    // Get dirty tiles bitmap (tile-based update optimization)
    const std::vector<bool> &get_dirty_tiles() const { return dirty_tiles; }

    // Get heat map for temporal analysis
    const std::vector<uint32_t> &get_heat_map() const { return heat_map; }

    // Get bounding box of changes (returns true if valid)
    bool get_dirty_rect(int &x, int &y, int &w, int &h) const
    {
        if (changed_pixels == 0 || max_x < 0)
            return false;

        x = min_x;
        y = min_y;
        w = max_x - min_x + 1;
        h = max_y - min_y + 1;
        return true;
    }

    // Check if a specific tile is dirty
    bool is_tile_dirty(int tile_x, int tile_y) const
    {
        if (tile_x < 0 || tile_x >= TILES_X || tile_y < 0 || tile_y >= TILES_Y)
            return false;
        int tile_idx = tile_y * TILES_X + tile_x;
        return dirty_tiles[tile_idx];
    }

    // Get tile bounds in pixel coordinates
    void get_tile_bounds(int tile_x, int tile_y, int &x, int &y, int &w, int &h)
        const
    {
        x = tile_x * TILE_SIZE;
        y = tile_y * TILE_SIZE;
        w = std::min(TILE_SIZE, H_RES - x);
        h = std::min(TILE_SIZE, V_RES - y);
    }

    // Tile configuration getters
    static constexpr int get_tile_size() { return TILE_SIZE; }
    static constexpr int get_tiles_x() { return TILES_X; }
    static constexpr int get_tiles_y() { return TILES_Y; }
};

// Render Profiler: Quantify rendering efficiency and establish performance
// baseline
//
// Tracks clock-level utilization to answer "How efficient is my design?"
// Inspired by tt08-vga-donut's performance-oriented approach.
//
// Design principles:
//   - Track every clock cycle during simulation
//   - Classify clocks: blanking vs active vs rendered
//   - Calculate utilization rates for performance analysis
//   - Provide data-driven baseline for optimization decisions
class RenderProfiler
{
private:
    uint64_t total_clocks = 0;
    uint64_t blank_clocks = 0;         // !activevideo
    uint64_t active_black_clocks = 0;  // activevideo && (rrggbb == 0)
    uint64_t rendered_clocks = 0;      // activevideo && (rrggbb != 0)
    bool frame_complete = false;

public:
    RenderProfiler() = default;

    // Track one clock cycle
    // Call this for every pixel clock in the simulation
    void tick(bool activevideo, uint8_t rrggbb)
    {
        total_clocks++;

        if (!activevideo) {
            blank_clocks++;
        } else {
            if (rrggbb == 0) {
                active_black_clocks++;
            } else {
                rendered_clocks++;
            }
        }
    }

    // Mark frame completion (optional, for multi-frame statistics)
    void mark_frame_complete() { frame_complete = true; }

    void report() const
    {
        if (total_clocks == 0) {
            std::cout << "Render Profiler: No clocks profiled\n";
            return;
        }

        double blank_pct = (100.0 * blank_clocks) / total_clocks;
        double active_black_pct = (100.0 * active_black_clocks) / total_clocks;
        double rendered_pct = (100.0 * rendered_clocks) / total_clocks;
        double total_active_pct = active_black_pct + rendered_pct;

        std::cout << "\n========================================\n";
        std::cout << "Render Performance Profile\n";
        std::cout << "========================================\n\n";

        std::cout << "Total clocks simulated: " << total_clocks << "\n\n";

        std::cout << "Clock utilization breakdown:\n";
        std::cout << "  Blanking:        " << blank_clocks << " clocks ("
                  << blank_pct << "%)\n";
        std::cout << "  Active (black):  " << active_black_clocks << " clocks ("
                  << active_black_pct << "%)\n";
        std::cout << "  Rendered pixels: " << rendered_clocks << " clocks ("
                  << rendered_pct << "%)\n";
        std::cout << "  ---\n";
        std::cout << "  Total active:    "
                  << (active_black_clocks + rendered_clocks) << " clocks ("
                  << total_active_pct << "%)\n\n";

        // Efficiency analysis
        std::cout << "Efficiency metrics:\n";
        std::cout << "  Render utilization:  " << rendered_pct
                  << "% (pixels with content)\n";
        std::cout << "  Active utilization:  " << total_active_pct
                  << "% (activevideo=1)\n";
        std::cout << "  Blanking overhead:   " << blank_pct
                  << "% (sync + porches)\n\n";

        // Expected vs measured for VGA 640×480 @ 72Hz
        uint64_t expected_active = H_RES * V_RES;     // 640 × 480 = 307,200
        uint64_t expected_total = H_TOTAL * V_TOTAL;  // 832 × 520 = 432,640
        double theoretical_active_pct =
            (100.0 * expected_active) / expected_total;

        std::cout << "Theoretical limits (VGA " << H_RES << "×" << V_RES
                  << "):\n";
        std::cout << "  Max active: " << theoretical_active_pct << "% ("
                  << expected_active << "/" << expected_total << " pixels)\n";
        std::cout << "  Nyancat display area: 512×512 = 262,144 pixels ("
                  << (100.0 * 262144 / expected_active) << "% of active)\n";
        std::cout << "  Expected render rate: ~"
                  << (100.0 * 262144 / expected_total)
                  << "% of total clocks\n\n";

        // Performance comparison
        double actual_vs_theoretical = rendered_pct / theoretical_active_pct;
        std::cout << "Performance vs theoretical:\n";
        std::cout << "  Actual render / Max active: "
                  << (actual_vs_theoretical * 100.0) << "%\n";

        std::cout << "========================================\n";
    }

    uint64_t get_total_clocks() const { return total_clocks; }
    uint64_t get_rendered_clocks() const { return rendered_clocks; }
    double get_render_utilization() const
    {
        return total_clocks > 0 ? (100.0 * rendered_clocks) / total_clocks
                                : 0.0;
    }
};

// Standalone PNG encoder (no external dependencies)
// Adapted from sysprog21/mado headless-ctl.c

#if defined(__GNUC__) || defined(__clang__)
#define BSWAP32(x) __builtin_bswap32(x)
#else
static inline uint32_t bswap32(uint32_t x)
{
    return ((x & 0x000000ff) << 24) | ((x & 0x0000ff00) << 8) |
           ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24);
}
#define BSWAP32(x) bswap32(x)
#endif

// CRC32 table for PNG chunk verification
static const uint32_t crc32_table[16] = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
    0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c,
};

// Calculate CRC32 checksum
static uint32_t crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        crc = (crc >> 4) ^ crc32_table[crc & 0x0f];
        crc = (crc >> 4) ^ crc32_table[crc & 0x0f];
    }
    return ~crc;
}

// Write PNG file with minimal dependencies
static int save_png(const char *filename,
                    const uint8_t *pixels,
                    int width,
                    int height)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return -1;

    // PNG magic bytes
    static const uint8_t png_sig[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    };
    fwrite(png_sig, 1, 8, fp);

// Helper macros for writing PNG chunks
#define PUT_U32(u)           \
    do {                     \
        uint8_t b[4];        \
        b[0] = (u) >> 24;    \
        b[1] = (u) >> 16;    \
        b[2] = (u) >> 8;     \
        b[3] = (u);          \
        fwrite(b, 1, 4, fp); \
    } while (0)

#define PUT_BYTES(buf, len) fwrite(buf, 1, len, fp)

    // Write IHDR chunk
    uint8_t ihdr[13];
    uint32_t *p32 = (uint32_t *) ihdr;
    p32[0] = BSWAP32(width);
    p32[1] = BSWAP32(height);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 6;   // color type: RGBA
    ihdr[10] = 0;  // compression
    ihdr[11] = 0;  // filter
    ihdr[12] = 0;  // interlace

    PUT_U32(13);  // chunk length
    PUT_BYTES("IHDR", 4);
    PUT_BYTES(ihdr, 13);
    uint32_t crc = crc32(0, (uint8_t *) "IHDR", 4);
    crc = crc32(crc, ihdr, 13);
    PUT_U32(crc);

    // Write IDAT chunk
    size_t raw_size = height * (1 + width * 4);  // filter byte + RGBA per row
    size_t max_deflate_size =
        raw_size + ((raw_size + 7) >> 3) + ((raw_size + 63) >> 6) + 11;

    uint8_t *idat = (uint8_t *) malloc(max_deflate_size);
    if (!idat) {
        fclose(fp);
        return -1;
    }

    // Simple uncompressed DEFLATE block
    size_t idat_size = 0;
    idat[idat_size++] = 0x78;  // ZLIB header
    idat[idat_size++] = 0x01;

    // Write uncompressed blocks
    uint8_t *raw_data = (uint8_t *) malloc(raw_size);
    if (!raw_data) {
        free(idat);
        fclose(fp);
        return -1;
    }

    // Convert BGRA to RGBA with filter bytes
    // Input format: BGRA (4 bytes per pixel)
    // Output format: filter_byte + RGBA per scanline
    size_t raw_pos = 0;
    for (int y = 0; y < height; y++) {
        raw_data[raw_pos++] = 0;  // filter type: none
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            raw_data[raw_pos++] = pixels[idx + 2];  // R (from BGRA byte 2)
            raw_data[raw_pos++] = pixels[idx + 1];  // G (from BGRA byte 1)
            raw_data[raw_pos++] = pixels[idx + 0];  // B (from BGRA byte 0)
            raw_data[raw_pos++] = pixels[idx + 3];  // A (from BGRA byte 3)
        }
    }

    // Write as uncompressed DEFLATE blocks
    size_t pos = 0;
    while (pos < raw_size) {
        size_t chunk = raw_size - pos;
        if (chunk > 65535)
            chunk = 65535;

        // final block flag
        idat[idat_size++] = (pos + chunk >= raw_size) ? 1 : 0;
        idat[idat_size++] = chunk & 0xff;
        idat[idat_size++] = (chunk >> 8) & 0xff;
        idat[idat_size++] = ~chunk & 0xff;
        idat[idat_size++] = (~chunk >> 8) & 0xff;

        memcpy(idat + idat_size, raw_data + pos, chunk);
        idat_size += chunk;
        pos += chunk;
    }

    // ADLER32 checksum (RFC 1950)
    uint32_t s1 = 1, s2 = 0;
    for (size_t i = 0; i < raw_size; i++) {
        s1 = (s1 + raw_data[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    uint32_t adler = (s2 << 16) | s1;
    idat[idat_size++] = (adler >> 24) & 0xff;
    idat[idat_size++] = (adler >> 16) & 0xff;
    idat[idat_size++] = (adler >> 8) & 0xff;
    idat[idat_size++] = adler & 0xff;

    PUT_U32(idat_size);
    PUT_BYTES("IDAT", 4);
    PUT_BYTES(idat, idat_size);
    crc = crc32(0, (uint8_t *) "IDAT", 4);
    crc = crc32(crc, idat, idat_size);
    PUT_U32(crc);

    // Write IEND chunk
    PUT_U32(0);
    PUT_BYTES("IEND", 4);
    PUT_U32(crc32(0, (uint8_t *) "IEND", 4));

#undef PUT_U32
#undef PUT_BYTES

    free(raw_data);
    free(idat);
    fclose(fp);
    return 0;
}

// Save framebuffer to PNG file
void save_framebuffer_png(const char *filename,
                          const std::vector<uint8_t> &fb,
                          int w,
                          int h)
{
    save_png(filename, fb.data(), w, h);
}

void print_usage(const char *prog)
{
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "Options:\n"
        << "  --save-png <file>       Save single frame to PNG and exit\n"
        << "  --trace <file.vcd>      Enable VCD waveform tracing for "
           "debugging\n"
        << "  --trace-clocks <N>      Limit VCD trace to first N clock cycles "
           "(default: 1 frame)\n"
        << "  --validate-timing       Enable real-time VGA timing validation\n"
        << "  --validate-signals      Enable sync signal glitch detection\n"
        << "  --validate-coordinates  Enable coordinate bounds checking\n"
        << "  --track-changes         Enable frame-to-frame change tracking\n"
        << "  --profile-render        Enable rendering performance profiling\n"
        << "  --help                  Show this help\n\n"
        << "Interactive keys:\n"
        << "  p     - Save frame to test.png\n"
        << "  ESC   - Reset animation\n"
        << "  q     - Quit\n\n"
        << "VCD waveform analysis:\n"
        << "  Generate: ./Vvga_nyancat --trace waves.vcd --trace-clocks 10000\n"
        << "  View:     surfer waves.vcd  (or gtkwave waves.vcd)\n\n"
        << "Validation modes:\n"
        << "  --validate-timing       Validates hsync/vsync pulse widths and "
           "frame "
           "dimensions\n"
        << "                          Tolerates ±1 clock/line jitter for "
           "real-world "
           "variations\n"
        << "  --validate-signals      Detects glitches and validates sync "
           "signal "
           "state\n"
        << "                          Phase-aware diagnostics with position "
           "context\n"
        << "  --validate-coordinates  Defense-in-depth coordinate bounds "
           "checking\n"
        << "                          Prevents wild pointer crashes "
           "(auto-stops at 10 errors)\n"
        << "  --track-changes         Tracks pixel changes between frames\n"
        << "                          Reports change rate, dirty rectangles, "
           "and statistics\n"
        << "  --profile-render        Quantifies clock-level rendering "
           "efficiency\n"
        << "                          Provides performance baseline for "
           "optimization "
           "decisions\n";
}

// Simulate VGA frame generation with performance optimizations
//
// Executes the specified number of clock cycles, updating the framebuffer
// during active video periods. Maintains pixel position state across calls
// for interactive mode operation.
//
// Coordinate system:
//   - Active display: hpos ∈ [0, 640), vpos ∈ [0, 480)
//   - Blanking periods: negative coordinates (back porch before active region)
//   - Frame sync: detected when both hsync and vsync are low
//
// Performance optimizations:
//   - Row base address precomputation (eliminates per-pixel multiply)
//   - Sentinel value (-1) for blanking row detection (single bounds check)
//   - Direct pointer arithmetic for framebuffer access
//   - Bit shifts for 4-byte alignment (hpos << 2 instead of hpos * 4)
//
// VCD tracing:
//   - If trace is non-null, records all signal changes to VCD file
//   - trace_time: simulation time counter (incremented per clock edge)
//
// Timing validation:
//   - If monitor is non-null, calls monitor->tick() each clock for validation
//   - If coord_validator is non-null, validates coordinates before framebuffer
//   writes
//   - If change_tracker is non-null, tracks frame changes on vsync falling edge
//   - If profiler is non-null, tracks clock utilization for performance
//   analysis
inline void simulate_frame(Vvga_nyancat *top,
                           uint8_t *fb,
                           int &hpos,
                           int &vpos,
                           int clocks,
                           VerilatedVcdC *trace = nullptr,
                           vluint64_t *trace_time = nullptr,
                           TimingMonitor *monitor = nullptr,
                           SyncValidator *validator = nullptr,
                           CoordinateValidator *coord_validator = nullptr,
                           ChangeTracker *change_tracker = nullptr,
                           RenderProfiler *profiler = nullptr)
{
    // Precompute row base address for current row
    int row_base = (vpos >= 0 && vpos < V_RES) ? (vpos * H_RES) << 2 : -1;

    // Track previous vsync state for edge detection (frame end tracking)
    static bool prev_vsync = true;

    for (int i = 0; i < clocks; ++i) {
        // Clock cycle: proper edge evaluation for Verilator
        // Both edges need eval() for correct state propagation
        top->clk = 0;
        top->eval();
        if (trace && trace_time)
            trace->dump((*trace_time)++);

        top->clk = 1;
        top->eval();
        if (trace && trace_time)
            trace->dump((*trace_time)++);

        // Timing validation on rising edge (after eval)
        if (monitor)
            monitor->tick(top->hsync, top->vsync, top->activevideo);

        // Sync signal validation on rising edge
        if (validator)
            validator->tick(top->hsync, top->vsync);

        // Performance profiling on rising edge
        if (profiler)
            profiler->tick(top->activevideo, top->rrggbb);

        // Detect frame end: vsync rising edge (end of vertical sync pulse)
        // This marks completion of frame rendering, trigger change tracking
        if (change_tracker && top->vsync && !prev_vsync)
            change_tracker->track(fb);
        prev_vsync = top->vsync;

        // Detect frame start: both syncs go low simultaneously during vsync
        if (!top->hsync && !top->vsync) {
            hpos = -H_BP;
            vpos = -V_BP;
            row_base = -1;  // Reset row base (in blanking)
            // Mark frame completion for coordinate validator
            if (coord_validator)
                coord_validator->mark_frame_complete();
        }

        // Fast path: skip processing during blanking intervals
        // Only process when in active display region
        if (row_base >= 0) {
            if (hpos >= 0 && hpos < H_RES) {
                // Coordinate validation before framebuffer write
                // (defense-in-depth)
                bool coords_valid = true;
                if (coord_validator)
                    coords_valid =
                        coord_validator->validate(hpos, vpos, row_base);

                // Only update framebuffer if coordinates pass validation
                if (coords_valid) {
                    // Direct framebuffer write using precomputed row base
                    int idx = row_base + (hpos << 2);
                    uint8_t color = top->rrggbb;
                    fb[idx] = vga2bit_to_8bit(color & 0b11);             // B
                    fb[idx + 1] = vga2bit_to_8bit((color >> 2) & 0b11);  // G
                    fb[idx + 2] = vga2bit_to_8bit((color >> 4) & 0b11);  // R
                    fb[idx + 3] = 255;                                   // A
                }
            }
        }

        // Position tracking with wraparound
        if (++hpos >= H_RES + H_FP + H_SYNC) {
            hpos = -H_BP;
            if (++vpos >= V_RES + V_FP + V_SYNC) {
                vpos = -V_BP;
                row_base = -1;
            } else {
                // Update row base when entering new active row
                row_base =
                    (vpos >= 0 && vpos < V_RES) ? (vpos * H_RES) << 2 : -1;
            }
        }
    }
}

int main(int argc, char **argv)
{
    bool save_and_exit = false;
    bool validate_timing = false;
    bool validate_signals = false;
    bool validate_coordinates = false;
    bool track_changes = false;
    bool profile_render = false;
    const char *output_file = "test.png";
    const char *trace_file = nullptr;
    int trace_clocks = CLOCKS_PER_FRAME;  // Default: 1 complete frame

    // Command line argument parsing
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--save-png") == 0 && i + 1 < argc) {
            save_and_exit = true;
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_file = argv[++i];
        } else if (strcmp(argv[i], "--trace-clocks") == 0 && i + 1 < argc) {
            trace_clocks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--validate-timing") == 0) {
            validate_timing = true;
        } else if (strcmp(argv[i], "--validate-signals") == 0) {
            validate_signals = true;
        } else if (strcmp(argv[i], "--validate-coordinates") == 0) {
            validate_coordinates = true;
        } else if (strcmp(argv[i], "--track-changes") == 0) {
            track_changes = true;
        } else if (strcmp(argv[i], "--profile-render") == 0) {
            profile_render = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    // Initialize Verilator
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);  // Enable tracing for VCD generation
    Vvga_nyancat *top = new Vvga_nyancat;

    // Initialize VCD tracing if requested
    VerilatedVcdC *trace = nullptr;
    vluint64_t trace_time = 0;
    int remaining_trace_clocks = trace_clocks;

    if (trace_file) {
        trace = new VerilatedVcdC;
        top->trace(trace, 99);  // Trace 99 levels of hierarchy
        trace->open(trace_file);
        std::cout << "VCD tracing enabled: " << trace_file << "\n";
        std::cout << "Trace duration: " << trace_clocks << " clock cycles\n";
    }

    // Perform reset sequence: hold reset for multiple cycles to ensure
    // complete initialization of all sequential elements
    top->reset_n = 0;
    for (int i = 0; i < 8; ++i) {
        top->clk = 0;
        top->eval();
        if (trace && remaining_trace_clocks > 0) {
            trace->dump(trace_time++);
            remaining_trace_clocks--;
        }

        top->clk = 1;
        top->eval();
        if (trace && remaining_trace_clocks > 0) {
            trace->dump(trace_time++);
            remaining_trace_clocks--;
        }
    }
    top->reset_n = 1;
    top->clk = 0;
    top->eval();

    // Initialize SDL subsystem
    SDL_Init(SDL_INIT_VIDEO);

    // Create window title with video mode info
    char window_title[128];
    snprintf(window_title, sizeof(window_title), "Nyancat - %s", MODE_NAME);

    SDL_Window *window = SDL_CreateWindow(
        window_title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, H_RES,
        V_RES, save_and_exit ? SDL_WINDOW_HIDDEN : 0);

    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    // Create streaming texture for framebuffer updates
    SDL_Texture *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, H_RES, V_RES);

    // Allocate framebuffer (BGRA format, 4 bytes per pixel)
    std::vector<uint8_t> framebuffer(H_RES * V_RES * 4, 0);
    uint8_t *fb_ptr = framebuffer.data();

    // Position tracking for frame simulation
    // Start from back porch to properly sync with VGA timing
    int hpos = -H_BP;
    int vpos = -V_BP;

    // Initialize timing monitor if requested
    TimingMonitor *monitor = nullptr;
    if (validate_timing) {
        monitor = new TimingMonitor();
        std::cout << "VGA timing validation enabled\n";
        std::cout << "Expected timing: H_TOTAL=" << H_TOTAL
                  << " V_TOTAL=" << V_TOTAL << " H_SYNC=" << H_SYNC
                  << " V_SYNC=" << V_SYNC << "\n";
    }

    // Initialize sync signal validator if requested
    SyncValidator *validator = nullptr;
    if (validate_signals) {
        validator = new SyncValidator();
        std::cout << "Sync signal validation enabled\n";
        std::cout << "Glitch detection with phase-aware diagnostics\n";
    }

    // Initialize coordinate validator if requested
    CoordinateValidator *coord_validator = nullptr;
    if (validate_coordinates) {
        coord_validator = new CoordinateValidator();
        std::cout << "Coordinate validation enabled\n";
        std::cout
            << "Defense-in-depth bounds checking (auto-stops at 10 errors)\n";
    }

    // Initialize change tracker if requested
    ChangeTracker *change_tracker = nullptr;
    if (track_changes) {
        change_tracker = new ChangeTracker();
        std::cout << "Frame change tracking enabled\n";
        std::cout
            << "Tracking pixel-level changes between consecutive frames\n";
    }

    // Initialize render profiler if requested
    RenderProfiler *profiler = nullptr;
    if (profile_render) {
        profiler = new RenderProfiler();
        std::cout << "Render performance profiling enabled\n";
        std::cout
            << "Clock-level utilization tracking for performance analysis\n";
    }

    bool quit = false;

    // Batch mode: generate one frame and exit
    if (save_and_exit) {
        // Simulate exactly one complete frame
        // For timing validation, simulate extra lines to ensure second vsync
        // edge
        int sim_clocks = CLOCKS_PER_FRAME;
        if (validate_timing) {
            // Add extra lines to ensure we see second vsync falling edge
            sim_clocks += H_TOTAL * (V_FP + V_SYNC + 1);
        }
        if (trace && remaining_trace_clocks > 0) {
            sim_clocks = (remaining_trace_clocks < sim_clocks)
                             ? remaining_trace_clocks
                             : sim_clocks;
        }

        simulate_frame(top, fb_ptr, hpos, vpos, sim_clocks, trace, &trace_time,
                       monitor, validator, coord_validator, change_tracker,
                       profiler);
        if (trace) {
            remaining_trace_clocks -= sim_clocks * 2;  // 2 edges per clock
        }

        // Update SDL texture and save PNG
        SDL_UpdateTexture(texture, nullptr, fb_ptr, H_RES * 4);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        save_framebuffer_png(output_file, framebuffer, H_RES, V_RES);
        std::cout << "Saved frame to " << output_file << std::endl;

        quit = true;
    }

    // Interactive mode: continuous simulation with user input
    // Performance: simulate in batches and update display periodically
    while (!quit) {
        // Process SDL events
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_q:
                    quit = true;
                    break;
                case SDLK_p:
                    save_framebuffer_png("test.png", framebuffer, H_RES, V_RES);
                    std::cout << "Saved frame to test.png" << std::endl;
                    break;
                }
            }
        }

        // Read keyboard state for controls
        auto keystate = SDL_GetKeyboardState(nullptr);
        top->reset_n = !keystate[SDL_SCANCODE_ESCAPE];

        // Simulate in smaller chunks for responsive input
        // VCD tracing disabled in interactive mode (too much data)
        simulate_frame(top, fb_ptr, hpos, vpos, 50000, nullptr, nullptr,
                       monitor, validator, coord_validator, change_tracker,
                       profiler);

        // Update display after each simulation chunk
        SDL_UpdateTexture(texture, nullptr, fb_ptr, H_RES * 4);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // Check if timing validation is complete
        if (monitor && monitor->is_complete()) {
            monitor->report();
            delete monitor;
            monitor = nullptr;  // Only report once
        }
    }

    // Cleanup and final reports
    if (monitor) {
        monitor->report();
        delete monitor;
    }

    if (validator) {
        validator->report();
        delete validator;
    }

    if (coord_validator) {
        coord_validator->report();
        delete coord_validator;
    }

    if (change_tracker) {
        change_tracker->report();
        delete change_tracker;
    }

    if (profiler) {
        profiler->report();
        delete profiler;
    }

    if (trace) {
        trace->close();
        delete trace;
        std::cout << "VCD trace saved to " << trace_file << "\n";
        std::cout << "View with: surfer " << trace_file << "\n";
    }

    top->final();
    delete top;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
