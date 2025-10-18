// VGA Nyancat is freely redistributable under the MIT License. See the file
// "LICENSE" for information on usage and redistribution of this file.

// Memory Interface Definitions
//
// Defines abstract memory interface macros for ROM access with 1-cycle latency.
// Optimized for FPGA block RAM inference.

`ifndef MEMORY_IF_VH
`define MEMORY_IF_VH

// Memory read operation: synchronous read with 1-cycle latency
// Usage: `MEM_READ(data, memory, address)
`define MEM_READ(data, memory, address) \
    data <= memory[address]

// Memory initialization from hex file
// Usage: `MEM_INIT(memory, filename)
`define MEM_INIT(memory, filename) \
    initial begin \
      $readmemh(filename, memory); \
    end

// Memory sizing parameters (can be overridden if needed)
`ifndef FRAME_MEM_DATA_WIDTH
  `define FRAME_MEM_DATA_WIDTH 4   // 4-bit character indices
`endif

`ifndef PALETTE_MEM_DATA_WIDTH
  `define PALETTE_MEM_DATA_WIDTH 6  // 6-bit RGB (2R2G2B)
`endif

`endif // MEMORY_IF_VH
