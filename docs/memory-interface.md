# Memory Interface Abstraction

The VGA Nyancat project uses an abstract memory interface layer to separate the rendering logic from the underlying memory access mechanism.
This design allows for future extension to support standard bus protocols without modifying the core rendering code.

## Current Implementation

The current implementation uses direct ROM access with synchronous read operations:
- Interface Type: ROM (embedded block RAM)
- Read Latency: 1 clock cycle
- Access Pattern: Synchronous, read-only
- Memory Sizes:
  - Frame memory: 49,152 × 4 bits (24 KB)
  - Palette memory: 16 × 6 bits (12 bytes)

## Architecture

```
┌───────────────────────────────────────────────────┐
│ Rendering Logic (nyancat.v)                       │
│  ┌────────────────────────────────────────────┐   │
│  │Coordinate Transform & Address Calculation  │   │
│  └────────────────┬───────────────────────────┘   │
│                   │ frame_addr                    │
│                   ▼                               │
│  ┌────────────────────────────────────────────┐   │
│  │  Abstract Memory Interface Layer           │   │
│  │  (`MEM_READ`, `MEM_INIT` macros)           │   │
│  └────────────────┬───────────────────────────┘   │
└───────────────────┼───────────────────────────────┘
                    │
        ┌───────────┴───────────┐
        │                       │
   ┌────▼─────┐          ┌──────▼──────┐
   │ Frame    │          │  Palette    │
   │ Memory   │          │  Memory     │
   │ (49KB)   │          │  (12 bytes) │
   └──────────┘          └─────────────┘
```

## Memory Interface Macros

### `MEM_READ(data, memory, address)`

Performs a memory read operation, abstracting the underlying protocol.

Current behavior (ROM):
```verilog
data <= memory[address];  // Synchronous read, 1-cycle latency
```

Future Wishbone:
```verilog
// Generate Wishbone read transaction
// wb_cyc, wb_stb, wb_adr signals
```

Future AXI:
```verilog
// Generate AXI read transaction
// axi_arvalid, axi_araddr, axi_rready signals
```

### `MEM_INIT(memory, filename)`

Initializes memory contents from a hex file.

Current behavior (ROM):
```verilog
initial begin
  $readmemh(filename, memory);
end
```

Future bus protocols:
```verilog
// Memory initialized externally via bus transactions
// (No initial block needed)
```

## Memory Access Pattern

The rendering pipeline uses a 2-stage memory access pattern:

```
Clock Cycle N:
  - Address calculation: frame_addr = f(frame_index, src_x, src_y)

Clock Cycle N+1:
  - Stage 1 read: char_idx <= frame_mem[frame_addr]
  - Data available at end of cycle

Clock Cycle N+2:
  - Stage 2 read: color <= color_mem[char_idx]
  - Data available at end of cycle
  - Output: rrggbb = color
```

This 2-cycle latency is inherent to the two-level lookup (frame → character → color) and remains constant regardless of the underlying memory interface type.

## Future Extension: Wishbone Interface

The abstraction layer is designed to support Wishbone Classic bus protocol:

Signals (to be added when implementing Wishbone):
```verilog
// Master outputs (from nyancat to memory controller)
output wire                   wb_cyc_o;    // Cycle valid
output wire                   wb_stb_o;    // Strobe
output wire                   wb_we_o;     // Write enable (always 0 for read-only)
output wire [ADDR_WIDTH-1:0]  wb_adr_o;    // Address
input  wire [DATA_WIDTH-1:0]  wb_dat_i;    // Read data
input  wire                   wb_ack_i;    // Acknowledge
```

Benefits:
- Standard interface for connecting to external memory controllers
- Supports burst transfers for improved throughput
- Compatible with OpenCores IP cores
- Industry-standard protocol

## Future Extension: AXI4-Lite Interface

For integration with ARM-based SoCs or Xilinx Zynq platforms:

Signals (to be added when implementing AXI):
```verilog
// Read address channel
output wire                   axi_arvalid;  // Address valid
output wire [ADDR_WIDTH-1:0]  axi_araddr;   // Read address
input  wire                   axi_arready;  // Address ready

// Read data channel
input  wire                   axi_rvalid;   // Read data valid
input  wire [DATA_WIDTH-1:0]  axi_rdata;    // Read data
input  wire [1:0]             axi_rresp;    // Response status
output wire                   axi_rready;   // Master ready
```

Benefits:
- Industry-standard ARM AMBA protocol
- Native support in Xilinx and Intel FPGAs
- Efficient for memory-mapped peripherals
- Advanced features like out-of-order transactions

## Implementation Details

### Memory Sizing

Defined in `memory_if.vh`:

```verilog
`define FRAME_MEM_ADDR_WIDTH 16      // 64K addresses (49,152 used)
`define FRAME_MEM_DATA_WIDTH 4       // 4-bit character indices
`define PALETTE_MEM_ADDR_WIDTH 4     // 16 palette entries
`define PALETTE_MEM_DATA_WIDTH 6     // 6-bit RGB (2R2G2B)
```

### Selecting Interface Type

Currently only ROM is implemented. Future extensions can be selected via:

```verilog
`define MEM_IF_TYPE_ROM        // Current implementation
// `define MEM_IF_TYPE_WISHBONE   // Future
// `define MEM_IF_TYPE_AXI        // Future
```

## Performance Characteristics

### ROM Interface (Current)
- Read Latency: 1 cycle (deterministic)
- Throughput: 1 read per cycle
- Resource Usage: ~24 KB block RAM
- Power: Low (embedded memory)

### Wishbone Interface (Future)
- Read Latency: 1-2 cycles (depends on memory controller)
- Throughput: 1 read per cycle (pipelined)
- Resource Usage: Interface logic + external memory
- Power: Medium (external memory access)

### AXI Interface (Future)
- Read Latency: 1-3 cycles (depends on interconnect)
- Throughput: High (out-of-order capable)
- Resource Usage: Interface logic + external memory
- Power: Medium-High (complex protocol)

## Migration Path

To migrate from ROM to bus-based memory:

1. Define the target interface type in `memory_if.vh`
2. Implement the `MEM_READ` macro for the new protocol
3. Add necessary port declarations to `nyancat.v`
4. Connect memory controller in top-level module
5. Update testbench to simulate bus transactions

The rendering logic in `nyancat.v` requires no modifications thanks to the abstraction layer.

## References

- Wishbone: [OpenCores Wishbone B4 Specification](https://opencores.org/howto/wishbone)
- AXI4: [ARM AMBA AXI Protocol Specification](https://developer.arm.com/documentation/ihi0022/latest/)
