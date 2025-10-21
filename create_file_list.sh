#!/bin/bash

# YAC File List Generator
# Creates a file list of assembly files for the RISC-V assembler

# Default values
INPUT_DIR="build"
OUTPUT_DIR="./test"
OUTPUT_FILE="assembly_files.txt"
PATTERN="pe*_assembly.s"

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -d, --dir DIRECTORY    Input directory to search (default: build)"
    echo "  -o, --output FILE      Output file list name (default: assembly_files.txt)"
    echo "  -p, --pattern PATTERN File pattern to match (default: pe*_assembly.s)"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Use defaults"
    echo "  $0 -d build -o my_files.txt          # Custom directory and output"
    echo "  $0 -p \"pe*_assembly.s\" -o pe_list.txt # Custom pattern and output"
    echo ""
    echo "This script creates a file list for the RISC-V assembler by finding"
    echo "all assembly files matching the specified pattern in the input directory."
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--dir)
            INPUT_DIR="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -p|--pattern)
            PATTERN="$2"
            shift 2
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Check if input directory exists
if [[ ! -d "$INPUT_DIR" ]]; then
    echo "Error: Input directory '$INPUT_DIR' does not exist."
    echo "Please run Stage 1 (dfg_processor) first to generate assembly files."
    exit 1
fi

# Get absolute path of input directory
ABS_INPUT_DIR=$(realpath "$INPUT_DIR")

# Find files matching the pattern with full paths
echo "Searching for files matching pattern: $PATTERN"
echo "In directory: $ABS_INPUT_DIR"

# Create the file list with full paths
find "$ABS_INPUT_DIR" -name "$PATTERN" -type f | sort > "$OUTPUT_FILE"

# Check if any files were found
if [[ ! -s "$OUTPUT_FILE" ]]; then
    echo "Warning: No files found matching pattern '$PATTERN' in directory '$INPUT_DIR'"
    echo "Make sure you have run Stage 1 (dfg_processor) to generate assembly files."
    rm -f "$OUTPUT_FILE"
    exit 1
fi

# Count files found
FILE_COUNT=$(wc -l < "$OUTPUT_FILE")
echo "Found $FILE_COUNT assembly files:"

# Display the files that will be in the list
while IFS= read -r file; do
    echo "  - $file"
done < "$OUTPUT_FILE"

echo ""
echo "File list created: $OUTPUT_FILE"
echo ""
echo "Next step: Run the RISC-V assembler with this file list:"
echo "  ./build/risc_v_assembler $OUTPUT_FILE build/"
echo ""
echo "Or run the complete pipeline:"
echo "  make test"
