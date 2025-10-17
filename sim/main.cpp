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

// Color conversion lookup table: 2-bit VGA channel → 8-bit RGB
// Maps 2-bit color values to 8-bit with even spacing:
//   0b00 → 0   (0%)
//   0b01 → 85  (33%)
//   0b10 → 170 (67%)
//   0b11 → 255 (100%)
// This provides better color fidelity than simple left-shift (×64)
static const uint8_t color_lut[4] = {0, 85, 170, 255};

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

    // ADLER32 checksum
    uint32_t adler = 1;
    for (size_t i = 0; i < raw_size; i++) {
        adler = (adler + raw_data[i]) % 65521 +
                ((((adler >> 16) + raw_data[i]) % 65521) << 16);
    }
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
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --save-png <file>  Save single frame to PNG and exit\n"
              << "  --help             Show this help\n\n"
              << "Interactive keys:\n"
              << "  p     - Save frame to test.png\n"
              << "  ESC   - Reset animation\n"
              << "  q     - Quit\n";
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
inline void simulate_frame(Vvga_nyancat *top,
                           uint8_t *fb,
                           int &hpos,
                           int &vpos,
                           int clocks)
{
    // Precompute row base address for current row
    int row_base = (vpos >= 0 && vpos < V_RES) ? (vpos * H_RES) << 2 : -1;

    for (int i = 0; i < clocks; ++i) {
        // Clock cycle: proper edge evaluation for Verilator
        // Both edges need eval() for correct state propagation
        top->clk = 0;
        top->eval();
        top->clk = 1;
        top->eval();

        // Detect frame start: both syncs go low simultaneously during vsync
        if (!top->hsync && !top->vsync) {
            hpos = -H_BP;
            vpos = -V_BP;
            row_base = -1;  // Reset row base (in blanking)
        }

        // Fast path: skip processing during blanking intervals
        // Only process when in active display region
        if (row_base >= 0) {
            if (hpos >= 0 && hpos < H_RES) {
                // Direct framebuffer write using precomputed row base
                int idx = row_base + (hpos << 2);
                uint8_t color = top->rrggbb;
                fb[idx] = color_lut[color & 0b11];             // B
                fb[idx + 1] = color_lut[(color >> 2) & 0b11];  // G
                fb[idx + 2] = color_lut[(color >> 4) & 0b11];  // R
                fb[idx + 3] = 255;                             // A
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
    const char *output_file = "test.png";

    // Command line argument parsing
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--save-png") == 0 && i + 1 < argc) {
            save_and_exit = true;
            output_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    // Initialize Verilator
    Verilated::commandArgs(argc, argv);
    Vvga_nyancat *top = new Vvga_nyancat;

    // Perform reset sequence: hold reset for multiple cycles to ensure
    // complete initialization of all sequential elements
    top->reset_n = 0;
    for (int i = 0; i < 8; ++i) {
        top->clk = 0;
        top->eval();
        top->clk = 1;
        top->eval();
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

    bool quit = false;

    // Batch mode: generate one frame and exit
    if (save_and_exit) {
        // Simulate exactly one complete frame (432,640 clocks)
        simulate_frame(top, fb_ptr, hpos, vpos, CLOCKS_PER_FRAME);

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

        // Simulate in smaller chunks for responsive input (50,000 clocks)
        // ~9 iterations per frame provides good balance of performance and
        // responsiveness
        simulate_frame(top, fb_ptr, hpos, vpos, 50000);

        // Update display after each simulation chunk
        SDL_UpdateTexture(texture, nullptr, fb_ptr, H_RES * 4);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    // Cleanup
    top->final();
    delete top;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
