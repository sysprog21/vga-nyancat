// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

// Memory Interface Definitions
//
// This file defines abstract memory interface types that can be used
// for accessing frame buffer and palette data. The abstraction allows
// for future extension to support standard bus protocols like Wishbone
// or AXI without modifying the core rendering logic.
//
// Current implementation: Direct ROM access
// Future options: Wishbone, AXI-Lite, custom SRAM interface

`ifndef MEMORY_IF_VH
`define MEMORY_IF_VH

// ============================================================================
// Memory Interface Types
// ============================================================================

// Define memory interface type (can be extended in future)
// Currently only ROM is implemented, but architecture supports:
//   - ROM: Direct embedded memory (current implementation)
//   - WISHBONE: Wishbone Classic bus protocol
//   - AXI: AXI4-Lite protocol
//   - SRAM: External SRAM interface

`ifndef MEM_IF_TYPE_WISHBONE
`ifndef MEM_IF_TYPE_AXI
`ifndef MEM_IF_TYPE_SRAM
  `define MEM_IF_TYPE_ROM  // Default to ROM
`endif
`endif
`endif

// ============================================================================
// ROM Interface (Current Implementation)
// ============================================================================
// Direct synchronous read access with 1-cycle latency
// Optimized for FPGA block RAM inference

`ifdef MEM_IF_TYPE_ROM
  // No additional signals needed - uses reg arrays with synchronous read
  // Read latency: 1 clock cycle
  // Address width: Determined by memory size
  // Data width: 4 bits for frame memory, 6 bits for palette
`endif

// ============================================================================
// Wishbone Interface (Future Extension)
// ============================================================================
// Wishbone Classic single-cycle read protocol
// Reference: OpenCores Wishbone B4 specification

`ifdef MEM_IF_TYPE_WISHBONE
  // Master signals (from renderer to memory)
  // wire wb_cyc;           // Cycle active
  // wire wb_stb;           // Strobe (valid transaction)
  // wire wb_we;            // Write enable (0=read, 1=write)
  // wire [ADDR_WIDTH-1:0] wb_adr;  // Address
  // wire [DATA_WIDTH-1:0] wb_dat_o; // Data output (write data)

  // Slave signals (from memory to renderer)
  // wire wb_ack;           // Acknowledge
  // wire [DATA_WIDTH-1:0] wb_dat_i; // Data input (read data)

  // Read latency: 1 clock cycle (same as ROM)
  // Supports pipelined access for higher throughput
`endif

// ============================================================================
// AXI4-Lite Interface (Future Extension)
// ============================================================================
// AXI4-Lite read-only protocol for memory-mapped access
// Reference: ARM AMBA AXI4 specification

`ifdef MEM_IF_TYPE_AXI
  // Read address channel
  // wire                   axi_arvalid;  // Address valid
  // wire [ADDR_WIDTH-1:0]  axi_araddr;   // Read address
  // wire                   axi_arready;  // Address ready (from slave)

  // Read data channel
  // wire                   axi_rvalid;   // Read data valid (from slave)
  // wire [DATA_WIDTH-1:0]  axi_rdata;    // Read data
  // wire [1:0]             axi_rresp;    // Response status
  // wire                   axi_rready;   // Read data ready

  // Read latency: 1-2 clock cycles
  // More complex but industry-standard protocol
`endif

// ============================================================================
// Helper Macros for Memory Access
// ============================================================================

// Memory read operation (abstracts underlying protocol)
// Usage: `MEM_READ(data, memory, address)
`ifdef MEM_IF_TYPE_ROM
  `define MEM_READ(data, memory, address) \
    data <= memory[address]
`elsif MEM_IF_TYPE_WISHBONE
  // Future: Implement Wishbone read transaction
  `define MEM_READ(data, memory, address) \
    /* TODO: Wishbone read transaction */ \
    data <= memory[address]
`elsif MEM_IF_TYPE_AXI
  // Future: Implement AXI read transaction
  `define MEM_READ(data, memory, address) \
    /* TODO: AXI read transaction */ \
    data <= memory[address]
`else
  `define MEM_READ(data, memory, address) \
    data <= memory[address]
`endif

// Memory initialization (abstracts loading mechanism)
// Usage: `MEM_INIT(memory, filename)
`ifdef MEM_IF_TYPE_ROM
  `define MEM_INIT(memory, filename) \
    initial begin \
      $readmemh(filename, memory); \
    end
`elsif MEM_IF_TYPE_WISHBONE
  // Future: External initialization through bus
  `define MEM_INIT(memory, filename) \
    /* Initialized externally via Wishbone */
`elsif MEM_IF_TYPE_AXI
  // Future: External initialization through bus
  `define MEM_INIT(memory, filename) \
    /* Initialized externally via AXI */
`else
  `define MEM_INIT(memory, filename) \
    initial begin \
      $readmemh(filename, memory); \
    end
`endif

// ============================================================================
// Memory Sizing Parameters
// ============================================================================

// These can be overridden for different memory configurations
`ifndef FRAME_MEM_ADDR_WIDTH
  `define FRAME_MEM_ADDR_WIDTH 16  // 64K addresses (49,152 used)
`endif

`ifndef FRAME_MEM_DATA_WIDTH
  `define FRAME_MEM_DATA_WIDTH 4   // 4-bit character indices
`endif

`ifndef PALETTE_MEM_ADDR_WIDTH
  `define PALETTE_MEM_ADDR_WIDTH 4  // 16 palette entries
`endif

`ifndef PALETTE_MEM_DATA_WIDTH
  `define PALETTE_MEM_DATA_WIDTH 6  // 6-bit RGB (2R2G2B)
`endif

`endif // MEMORY_IF_VH
