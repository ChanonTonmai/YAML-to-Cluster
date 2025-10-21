# YAC (YAML to Cluster)

A C++ project that takes **YAML configuration files** as input and produces **RISC-V assembly code** for each Processing Element (PE) in a cluster-based architecture. The project includes a two-stage pipeline: YAML-to-assembly conversion followed by assembly-to-binary compilation with binary combination.

## Overview

YAC is a C++ toolchain consisting of two main components:

1. **YAML Processor**: Converts YAML configuration files into RISC-V assembly code for each PE
2. **RISC-V Assembler**: Converts assembly code to binary machine code and combines all PE binaries

The system supports custom instruction set extensions including PSRF (Processing Element Special Register File), CORF (Coefficient Register File), and HWLRF (Hardware Loop Register File) operations, making it ideal for high-performance computing applications like matrix operations.


## Project Structure

```
YAC/
├── src/
│   ├── dfg_processor.cpp      # YAML-to-assembly converter (Stage 1)
│   └── risc_v_assembler.cpp  # Assembly-to-binary converter (Stage 2)
├── examples/
│   └── dfg_gemm.yaml         # Example YAML configuration
├── build/                    # Generated executables and output files
└── Makefile                  # Build system
```

## Workflow Pipeline

The YAC project follows a two-stage pipeline:

### Stage 1: YAML → Assembly
- **Input**: YAML configuration file
- **Tool**: `dfg_processor.cpp`
- **Output**: Individual assembly files for each PE (`pe0_assembly.s`, `pe1_assembly.s`, etc.)

### Stage 2: Assembly → Binary
- **Input**: List of assembly files
- **Tool**: `risc_v_assembler.cpp`
- **Output**: 
  - Individual binary files (`pe0_binary.bin`, `pe1_binary.bin`, etc.)
  - Individual memory files (`pe0_binary.mem`, `pe1_binary.mem`, etc.)
  - Combined memory file (`combined_memory.mem`)

## Installation & Dependencies

### Prerequisites
- **C++17** compatible compiler (GCC 7+ or Clang 5+)
- **yaml-cpp** library for YAML configuration parsing

### Installing Dependencies (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y libyaml-cpp-dev
```

### Building the Project
```bash
# Clone or navigate to the project directory
cd YAC

# Build all components
make all

# Or build individual components
make dfg_processor
make risc_v_assembler

# Run tests with example configuration
make test

# Clean build artifacts
make clean
```

Running `make test` queries YAML files from the `examples` folder and converts them into assembly files, saving them in the `test` folder. This process automatically creates the test folder if it doesn't already exist. It also generates an assembly file list used for combining the assembly files, preparing them to be packed to the cluster.


## Usage

### Complete Pipeline Example

Here's how to run the complete YAML-to-binary pipeline:

```bash
# Stage 1: Convert YAML to Assembly
./build/dfg_processor examples/dfg_gemm.yaml build/

# Then convert to binary and combine
./build/risc_v_assembler assembly_files.txt build/
```

### Individual Stage Usage

#### Stage 1: YAML Processor
Converts YAML configuration files into RISC-V assembly code for each PE:

```bash
./build/dfg_processor <yaml_config> [output_directory]
```

**Example:**
```bash
./build/dfg_processor examples/dfg_gemm.yaml build/
```

**Output**: Generates assembly files for each PE:
- `pe0_assembly.s` through `pe15_assembly.s`
- Each file contains PE-specific instructions with proper addressing

#### Stage 2: RISC-V Assembler
Converts assembly files to binary machine code and combines them:

```bash
./build/risc_v_assembler <file_list> [output_directory]
```

**Example:**
```bash
# Create a file list
echo "build/pe0_assembly.s" > file_list.txt
echo "build/pe1_assembly.s" >> file_list.txt
# ... add more files

# Convert to binary and combine
./build/risc_v_assembler file_list.txt build/
```

**Output**: Generates:
- Binary files (`pe0_binary.bin`, `pe1_binary.bin`, etc.)
- Memory initialization files (`pe0_binary.mem`, `pe1_binary.mem`, etc.)
- Combined memory file (`combined_memory.mem`)

## Configuration Format

The YAML configuration file defines the hardware architecture and PE assignments:

```yaml
mem_config:
  x18: 200        # Base address for register x18
  x19: 20000      # Base address for register x19
  x20: 40004      # Base address for register x20

hardware_config:
  total_pes: 16
  data_dup: 1
  clusters:
    count: 16
    pes_per_cluster: 1
  psrf_mem_offset:
    x18_offset: 1024
    x20_offset: 1024

scheduling:
  minimum_pes_required: 1
  pe_assignments:
  - pe_id: 0
    instructions:
    - operation: HWL
      format: hwl-type
      loop_id: 1
      pc_start: 2
      pc_stop: 12
      hwl_index: 10
      iterations: 4
    - operation: psrf.lw
      ra1: x1
      base_address: x18
      format: psrf-mem-type
      var: 0
      psrf_var:
        v0: 10
        v1: 12
      coefficients:
        c0: 256
        c1: 4
```

## Generated Assembly Structure

Each generated assembly file follows this structure:

```assembly
# Assembly for PE0 (Cluster 0)
# Generated with PSRF, HWL and function support
.text
.global _start

_start:
    # Base address loading section
    lui x18, 0
    addi x18, x18, 200
    
    # Preload section for PSRF variables and coefficients
    ppsrf.addi v0, v0, 10
    ppsrf.addi v1, v0, 12
    corf.addi c0, c0, 256
    corf.addi c1, c0, 4
    
    # ========== Execution Section Begin ==========
    # Hardware loop instructions
    hwlrf.lui L1, 0x00000
    hwlrf.addi L1, L1, 0x0040A
    
    # Main computation instructions
    psrf.lw x1, 0(x18)
    mul x1, x1, x2
    add x3, x3, x1
    psrf.sw x3, 0(x20)
    
    # End of program
    ret
```

## Build System Commands

```bash
make all          # Build both executables (default)
make test         # Build and test with example configuration
make clean        # Remove build artifacts
make install-deps # Install required dependencies (Ubuntu/Debian)
make check-deps   # Check if dependencies are available
make help         # Show help message
```

## Development

### Compiler Warnings
The build system includes comprehensive warnings (`-Wall -Wextra`) to ensure code quality. Minor warnings about sign comparisons and parentheses are present but don't affect functionality.

### Code Organization
- **dfg_processor.cpp**: YAML parsing, PE assignment processing, assembly generation
- **risc_v_assembler.cpp**: Instruction encoding, binary generation, memory file creation, binary combination
