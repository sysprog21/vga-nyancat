PROJECT = vga_nyancat

RTL_DIR = rtl
SIM_DIR = sim
OUT = build

# Nyancat upstream source
NYANCAT_RAW_URL = https://raw.githubusercontent.com/klange/nyancat/master/src/animation.c
NYANCAT_SRC = $(OUT)/animation.c

# Nyancat display sources
SOURCES = $(RTL_DIR)/vga-sync-gen.v $(RTL_DIR)/nyancat.v $(RTL_DIR)/vga-nyancat.v
DATA_FILES = $(OUT)/nyancat-frames.hex $(OUT)/nyancat-colors.hex
SIM_BINARY = $(OUT)/Vvga_nyancat

VERILATOR_ROOT := $(shell verilator --getenv VERILATOR_ROOT)
CFLAGS = -O3 -Iobj_dir -I$(VERILATOR_ROOT)/include $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)

# Formatting tools
# Prefer system installation, fall back to local tools/ directory
VERIBLE_FORMAT ?= $(shell command -v verible-verilog-format 2>/dev/null || \
                           find tools -name verible-verilog-format -type f 2>/dev/null | head -1)
CLANG_FORMAT ?= clang-format

all: $(SIM_BINARY) $(DATA_FILES)

# Download upstream nyancat source
$(NYANCAT_SRC):
	@echo "Downloading nyancat animation source..."
	@mkdir -p $(OUT)
	@if command -v curl >/dev/null 2>&1; then \
		curl -L --progress-bar -o $(NYANCAT_SRC) $(NYANCAT_RAW_URL) || \
		(echo "Error: Failed to download animation.c"; rm -f $(NYANCAT_SRC); exit 1); \
	elif command -v wget >/dev/null 2>&1; then \
		wget --show-progress -O $(NYANCAT_SRC) $(NYANCAT_RAW_URL) || \
		(echo "Error: Failed to download animation.c"; rm -f $(NYANCAT_SRC); exit 1); \
	else \
		echo "Error: Neither curl nor wget found. Please install one of them."; \
		exit 1; \
	fi
	@if [ ! -s $(NYANCAT_SRC) ]; then \
		echo "Error: Downloaded file is empty"; \
		rm -f $(NYANCAT_SRC); \
		exit 1; \
	fi
	@echo "Downloaded: $(NYANCAT_SRC) ($$(stat -f%z $(NYANCAT_SRC) 2>/dev/null || stat -c%s $(NYANCAT_SRC) 2>/dev/null) bytes)"

# Generate Nyancat animation data from source
$(DATA_FILES): scripts/gen-nyancat.py $(NYANCAT_SRC)
	@echo "Generating animation data..."
	@mkdir -p $(OUT)
	@python3 scripts/gen-nyancat.py $(NYANCAT_SRC) $(OUT)
	@if [ ! -f $(OUT)/nyancat-frames.hex ] || [ ! -f $(OUT)/nyancat-colors.hex ]; then \
		echo "Error: Data generation failed"; \
		exit 1; \
	fi
	@echo "Generated $(OUT)/nyancat-frames.hex and $(OUT)/nyancat-colors.hex"

# Verilator compilation
obj_dir/Vvga_nyancat.mk: $(SOURCES) $(SIM_DIR)/main.cpp
	@echo "Running Verilator..."
	@verilator --quiet --cc $(SOURCES) \
	           --exe $(SIM_DIR)/main.cpp \
	           --top-module vga_nyancat \
	           -CFLAGS "$(CFLAGS)" -LDFLAGS "$(LDFLAGS)"

# Build simulation binary
$(SIM_BINARY): obj_dir/Vvga_nyancat.mk $(DATA_FILES)
	@echo "Building simulation..."
	@mkdir -p $(OUT)
	@cd obj_dir && $(MAKE) -f Vvga_nyancat.mk
	@cp obj_dir/Vvga_nyancat $(SIM_BINARY)

# Convenience target for building without running
build: $(SIM_BINARY)
	@echo "Build complete: $(SIM_BINARY)"

# Run interactive simulation
run: $(SIM_BINARY)
	@echo "Starting VGA Nyancat simulation..."
	@cd $(OUT) && ./Vvga_nyancat

# Generate test image
check: $(SIM_BINARY)
	@echo "Generating test image..."
	@cd $(OUT) && ./Vvga_nyancat --save-png test.png
	@echo "Generated $(OUT)/test.png"
	@ls -lh $(OUT)/test.png

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -f *.log
	@rm -rf obj_dir

# Clean everything including downloaded source
distclean: clean
	@echo "Cleaning all generated files..."
	@rm -rf $(OUT)
	@rm -f $(RTL_DIR)/nyancat-frames.hex $(RTL_DIR)/nyancat-colors.hex

# Force regenerate animation data
regen-data:
	@echo "Forcing regeneration of animation data..."
	@rm -f $(DATA_FILES)
	@$(MAKE) $(DATA_FILES)

indent:
	@echo "Formatting Verilog files..."
	@if command -v $(VERIBLE_FORMAT) >/dev/null 2>&1; then \
		$(VERIBLE_FORMAT) --flagfile=.verible-format --inplace $(SOURCES); \
	else \
		echo "Warning: $(VERIBLE_FORMAT) not found."; \
		echo "Download verible from: https://github.com/chipsalliance/verible/releases"; \
		exit 1; \
	fi
	@echo "Formatting C++ files..."
	@if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
		$(CLANG_FORMAT) -i $(SIM_DIR)/*.cpp; \
	else \
		echo "Warning: $(CLANG_FORMAT) not found. Install it first."; \
		exit 1; \
	fi

.PHONY: all build run check clean distclean regen-data indent
