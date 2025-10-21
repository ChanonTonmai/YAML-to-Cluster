# Makefile for YAC (Yet Another Compiler) Project
# Builds DFG Processor and RISC-V Assembler

# Compiler settings
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
INCLUDES = -I/usr/include/yaml-cpp
LIBS = -lyaml-cpp

# Directories
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = test
EXAMPLES_DIR = examples

# Source files
DFG_PROCESSOR_SRC = $(SRC_DIR)/dfg_processor.cpp
RISC_V_ASSEMBLER_SRC = $(SRC_DIR)/risc_v_assembler.cpp

# Executables
DFG_PROCESSOR_EXE = $(BUILD_DIR)/dfg_processor
RISC_V_ASSEMBLER_EXE = $(BUILD_DIR)/risc_v_assembler

# Default target
all: $(DFG_PROCESSOR_EXE) $(RISC_V_ASSEMBLER_EXE)

# Build DFG Processor
$(DFG_PROCESSOR_EXE): $(DFG_PROCESSOR_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS)

# Build RISC-V Assembler
$(RISC_V_ASSEMBLER_EXE): $(RISC_V_ASSEMBLER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Create file list for assembly files
file-list: $(DFG_PROCESSOR_EXE)
	@echo "Creating file list for assembly files..."
	./create_file_list.sh -d $(TEST_DIR) -o assembly_files.txt

# Test with example configuration (complete pipeline)
test: $(DFG_PROCESSOR_EXE) $(RISC_V_ASSEMBLER_EXE)
	mkdir -p $(TEST_DIR)
	@echo "Testing complete pipeline with example configuration..."
	@echo "Stage 1: Converting YAML to Assembly..."
	$(DFG_PROCESSOR_EXE) $(EXAMPLES_DIR)/dfg_gemm.yaml $(TEST_DIR)/
	@echo "Stage 2: Creating file list..."
	./create_file_list.sh -d $(TEST_DIR) -o assembly_files.txt
	@echo "Stage 3: Converting Assembly to Binary..."
	$(RISC_V_ASSEMBLER_EXE) assembly_files.txt $(TEST_DIR)/
	@echo "Complete pipeline test finished!"

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(TEST_DIR)
	
# Install dependencies (Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install -y libyaml-cpp-dev

# Check if yaml-cpp is available
check-deps:
	@echo "Checking for yaml-cpp library..."
	@pkg-config --exists yaml-cpp && echo "yaml-cpp found" || echo "yaml-cpp not found - run 'make install-deps'"

# Show help
help:
	@echo "YAC Project Build System"
	@echo "======================="
	@echo "Available targets:"
	@echo "  all          - Build both executables (default)"
	@echo "  dfg_processor - Build only DFG processor"
	@echo "  risc_v_assembler - Build only RISC-V assembler"
	@echo "  file-list    - Create file list for assembly files"
	@echo "  test         - Build and test complete pipeline"
	@echo "  clean        - Remove build artifacts"
	@echo "  install-deps - Install required dependencies (Ubuntu/Debian)"
	@echo "  check-deps   - Check if dependencies are available"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  make                    # Build everything"
	@echo "  make test               # Build and test"
	@echo "  make clean              # Clean build directory"
	@echo "  make install-deps       # Install dependencies"

.PHONY: all test clean install-deps check-deps help file-list
