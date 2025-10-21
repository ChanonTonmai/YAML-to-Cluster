#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <yaml-cpp/yaml.h>
#include <optional>
#include <cmath>
#include <bitset>
#include <iomanip>
#include <sstream>
#include <algorithm>

struct HardwareLoop {
    int loop_id;
    int pc_start;
    int pc_stop;
    int hwl_index;
    int iterations;
};

struct Instruction {
    std::string operation;
    std::string ra1;
    std::string ra2;
    std::string rd;      // Add destination register field
    std::string base_address;  // Changed to string for register-based addressing
    std::string format;
    std::map<std::string, int> coefficients;  // Now using c0-c5
    std::optional<int> var;                   // Used for register offset calculation
    std::map<std::string, int> psrf_var;      // Now using v0-v5 with integer values
    std::optional<HardwareLoop> hwl;  // New field for hardware loop info
    int imm;                  // Immediate value for I-type instructions
    std::string target;       // Added for JAL target
    int address;              // Added for JAL target address
    int offset;               // Added for memory offset
};

struct PEAssignment {
    int pe_id;
    std::vector<Instruction> instructions;
    bool has_psrf_mem_type;  // Flag to indicate if PE needs preload section
    bool has_mem_type;      // Flag to indicate if PE needs preload section
    std::set<std::string> required_base_registers;  // Track which base registers are needed
    bool has_hwl;  // New flag for hardware loop
};

class DFGProcessor {
private:
    std::vector<PEAssignment> pe_assignments;
    std::map<std::string, int> mem_config;  // Memory configuration
    std::map<std::string, int> mem_offsets; // Memory offsets for each register
    std::map<std::string, int> function_addresses;  // Store function addresses
    std::map<std::string, std::map<int, PEAssignment>> function_pe_assignments;  // Store function PE assignments
    int total_pes;
    int clusters_count;
    int pes_per_cluster;
    int minimum_pes_required;
    int data_dup;
    std::map<int, int> hwl_imm_values;  // Map to store hardware loop immediate values
    std::string output_folder;
    std::vector<int> delay_start;  // Array to store delay values for each PE

    // Helper function to get cluster number from PE ID
    int getClusterNumber(int pe_id) {
        return pe_id / pes_per_cluster;
    }

    // Helper function to calculate base address for a specific cluster
    int calculateClusterBaseAddress(const std::string& reg, int cluster_num, int data_dup, int pe_id) {
        int base_addr = mem_config[reg];
        std::string offset_key = reg + "_offset";
        
        if (mem_offsets.count(offset_key) > 0 && mem_offsets[offset_key] != 0) {
            if (data_dup == 2) {
                if (pe_id > 15) {
                    return base_addr + (mem_offsets[offset_key] * (cluster_num)) +  100000;
                } else {
                    return base_addr + (mem_offsets[offset_key] * cluster_num);
                }
            } else if (data_dup == 4) {
                if (pe_id > 15 && pe_id < 31) {
                    return base_addr + (mem_offsets[offset_key] * (cluster_num)) +  100000;
                } else if (pe_id > 31 && pe_id < 47) {
                    return base_addr + (mem_offsets[offset_key] * (cluster_num)) +  200000;
                } else if (pe_id > 47 && pe_id < 63) {
                    return base_addr + (mem_offsets[offset_key] * (cluster_num)) +  300000;
                } else {
                    return base_addr + (mem_offsets[offset_key] * cluster_num);
                }
            } else {
                return base_addr + (mem_offsets[offset_key] * cluster_num);
            }
        }
        return base_addr;   
    }

    // Helper function to generate LUI and ADDI for large immediates
    std::pair<int, int> calculateLuiAddiValues(int value) {
        // RISC-V ADDI range is -2048 to 2047 (12-bit signed immediate)
        // RISC-V LUI loads the immediate value into the upper 20 bits (bits 31:12)
        
        // If the value fits within ADDI range, use 0 for LUI and the value for ADDI
        if (value >= -2048 && value <= 2047) {
            return {0, value};
        }
        
        // Extract lower 12 bits preserving the sign
        int lower12 = value & 0xFFF;
        
        // If the lower 12 bits represent a negative value (bit 11 is set)
        // we need to adjust the upper bits
        int upper20;
        if (lower12 & 0x800) {
            // Sign extension is happening, need to add 1 to upper bits
            // and keep the lower bits as they are
            upper20 = ((value >> 12) & 0xFFFFF) + 1;
        } else {
            // No sign extension, just use the upper 20 bits as is
            upper20 = (value >> 12) & 0xFFFFF;
        }
        
        return {upper20, lower12};
    }

    std::string generateBaseAddressLoading(int pe_id, int data_dup) {
        std::string result = "    # Base address loading section for cluster " + 
                            std::to_string(getClusterNumber(pe_id)) + "\n";
        
        int cluster_num = getClusterNumber(pe_id);
        
        // For each required base register in memory config
        for (const auto& [reg, base_value] : mem_config) {
            if (base_value != 0) {  // Only process non-null values
                int cluster_addr = calculateClusterBaseAddress(reg, cluster_num, data_dup, pe_id);
                auto [lui_val, addi_val] = calculateLuiAddiValues(cluster_addr);
                
                // Convert addi_val to signed 12-bit value if it exceeds range
                if (addi_val & 0x800) {
                    // Sign extend to print as negative number
                    addi_val = addi_val | 0xFFFFF000;
                }
                
                std::stringstream ss;
                ss << "    # Loading " << reg << " with address 0x" 
                   << std::hex << std::uppercase << cluster_addr 
                   << std::dec << " (" << cluster_addr << ")\n";
                
                // Add explanation of the LUI+ADDI sequence for large values
                if (lui_val != 0) {
                    if (addi_val < 0) {
                        ss << "    # Using lui " << lui_val << " and addi " << addi_val 
                           << " to create " << (lui_val << 12) + addi_val << "\n";
                    } else {
                        ss << "    # Using lui " << lui_val << " and addi " << addi_val 
                           << " to create " << (lui_val << 12) + addi_val << "\n";
                    }
                }
                
                result += ss.str();
                
                if (lui_val != 0) {
                    result += "    lui " + reg + ", " + std::to_string(lui_val) + "\n";
                }
                if (addi_val != 0 || lui_val != 0) {  // Always include ADDI after LUI
                    result += "    addi " + reg + ", " + reg + ", " + std::to_string(addi_val) + "\n";
                }
                result += "\n";
            }
        }
        
        return result;
    }

    std::string generatePreloadSection(const PEAssignment& pe_assignment) {
        std::string preload;
        bool has_psrf = false;
        bool has_mem_type = false;
        preload += "    # Preload section for PSRF variables and coefficients\n";
        
        // Generate PSRF variable loads
        for (const auto& instr : pe_assignment.instructions) {
            if (instr.format == "psrf-mem-type") {
                has_psrf = true;
                int var_value = 0;
                
                // Get the var value for this instruction
                if (instr.var.has_value()) {
                    var_value = instr.var.value();
                }
                
                // Calculate register base for this var value
                int reg_base = var_value * 6;  // var=0: 0-5, var=1: 6-11, var=2: 12-17
                
                // Add a comment indicating which var group we're using
                preload += "    # Using var=" + std::to_string(var_value) + 
                          " (registers " + std::to_string(reg_base) + "-" + 
                          std::to_string(reg_base+5) + ")\n";
                
                for (const auto& [var_key, value] : instr.psrf_var) {
                    if (value != 0) {  // Only generate for non-zero values
                        // Extract the register number from the key (e.g., v0 -> 0)
                        int base_reg = std::stoi(var_key.substr(1));
                        // Calculate the actual register number based on var value
                        int reg_num = reg_base + base_reg;
                        
          
                        // Use the first register of the group as source
                        preload += "    ppsrf.addi v" + std::to_string(reg_num) + 
                                ", v" + std::to_string(reg_base) + 
                                ", " + std::to_string(value) + "\n";
                    }
                }
                
                // Generate coefficient loads with corf.addi
                for (const auto& [coef_key, value] : instr.coefficients) {
                    if (value != 0) {  // Only generate for non-zero values
                        // Extract the register number from the key (e.g., c0 -> 0)
                        int base_reg = std::stoi(coef_key.substr(1));
                        // Calculate the actual register number based on var value
                        int reg_num = reg_base + base_reg;
                        
                        if (value > 4095) { 
                            // corf.addi range is 0 to 4095. 
                            // If negative, we need to sign extend the value
                            // Use the first register of the group as source
                            preload += "    corf.lui c" + std::to_string(reg_num) + 
                                    ", " + std::to_string(value >> 12) + "\n";
                            preload += "    corf.addi c" + std::to_string(reg_num) + 
                                    ", c" + std::to_string(reg_base) + 
                                    ", " + std::to_string(value & 0xFFF) + "\n";
                        } else {
                        // Use the first register of the group as source
                        preload += "    corf.addi c" + std::to_string(reg_num) + 
                                  ", c" + std::to_string(reg_base) + 
                                  ", " + std::to_string(value) + "\n";
                        }
                    }
                }
                
            } 
            // else if (instr.format == "mem-type") {
            //     has_mem_type = true;
            //     preload += "    # Memory offset: " + std::to_string(instr.offset) + "\n";
            //     preload += "    addi " + instr.ra1 + ", " + instr.base_address + ", " + std::to_string(instr.offset) + "\n";
            // }   
        }
        
        if (!has_psrf && !has_mem_type) return "";
        
        preload += "\n";
        std::cout << "Preload section: " << preload << std::endl;
        return preload;
    }

    std::string generateHWLInstructions(const Instruction& instr, int hwl_count, int pe_id) {
        if (!instr.hwl.has_value()) return "";

        const auto& hwl = instr.hwl.value();
        
        // Get the delay value for this PE
        int delay = 0;
        if (pe_id < delay_start.size()) {
            delay = delay_start[pe_id];
        }
        
        // Adjust pc_start and pc_stop by adding the delay
        int adjusted_pc_start = hwl.pc_start + delay;
        int adjusted_pc_stop = hwl.pc_stop - adjusted_pc_start;
        
        uint32_t imm = calculateHWLImmediate(hwl, delay);
        auto [upper, lower] = splitHWLImmediate(imm);

        std::string result = "";
        
        // Add comment showing the immediate value calculation with delay adjustment
        result += "    # hwl_imm_" + std::to_string(hwl_count) + " = ";
        result += "((" + std::to_string(adjusted_pc_start) + " << 23) + ";
        result += "(" + std::to_string(adjusted_pc_stop) + " << 17) + ";
        result += "(" + std::to_string(hwl.hwl_index) + " << 12) + ";
        result += std::to_string(hwl.iterations) + "\n";
        result += "    # Original pc_start=" + std::to_string(hwl.pc_start) + 
                 ", pc_stop=" + std::to_string(hwl.pc_stop) + 
                 ", delay=" + std::to_string(delay) + "\n";

        // Generate HWL instructions with adjusted immediate values
        result += "    hwlrf.lui L" + std::to_string(hwl.loop_id) + ", " + std::to_string(upper) + "\n";
        result += "    hwlrf.addi L" + std::to_string(hwl.loop_id) + ", L" + std::to_string(hwl.loop_id);
        result += ", " + std::to_string(lower) + "\n";

        return result;
    }

    std::string generateInstructionCode(const Instruction& instr, int& hwl_count, int pe_id) {
        // Handle hardware loop instructions
        if (instr.format == "hwl-type") {
            return generateHWLInstructions(instr, ++hwl_count, pe_id);
        }

        // Handle memory operations (both PSRF and normal)
        if (instr.operation == "LW" || instr.operation == "lw" || 
            instr.operation == "SW" || instr.operation == "sw" || 
            instr.operation == "LB" || instr.operation == "lb" || 
            instr.operation == "LH" || instr.operation == "lh" || 
            instr.operation == "LBU" || instr.operation == "lbu" ||
            instr.operation == "LHU" || instr.operation == "lhu" ||
            instr.operation == "SB" || instr.operation == "sb" ||
            instr.operation == "SH" || instr.operation == "sh" || 
            instr.operation == "psrf.lw" || 
            instr.operation == "psrf.lb" || 
            instr.operation == "psrf.sw" ||
            instr.operation == "psrf.sb" || 
            instr.operation == "psrf.zd.lw" ) {
        
            // Generate appropriate instruction based on format and operation
            if (instr.format == "psrf-mem-type") {
                std::string var_str = "";
                if (instr.var.has_value()) {
                    var_str = ", " + std::to_string(instr.var.value());
                }
                
                if (instr.operation == "psrf.lw") {
                    return "    psrf.lw " + instr.ra1 + var_str + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "psrf.sw") {
                    return "    psrf.sw " + instr.ra1 + var_str + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "psrf.lb") {
                    return "    psrf.lb " + instr.ra1 + var_str + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "psrf.sb") {
                    return "    psrf.sb " + instr.ra1 + var_str + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "psrf.zd.lw") {
                    return "    psrf.zd.lw " + instr.ra1 + var_str + "(" + instr.base_address + ")\n";
                }
            } else {
                // Normal memory operations
                if (instr.operation == "LW" || instr.operation == "lw") {
                    return "    lw " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "SW" || instr.operation == "sw") {
                    return "    sw " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "LB" || instr.operation == "lb") {
                    return "    lb " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "LH" || instr.operation == "lh") {
                    return "    lh " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "LBU" || instr.operation == "lbu") {
                    return "    lbu " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "LHU" || instr.operation == "lhu") {
                    return "    lhu " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "SB" || instr.operation == "sb") {
                    return "    sb " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                } else if (instr.operation == "SH" || instr.operation == "sh") {
                    return "    sh " + instr.ra1 + ", " + std::to_string(instr.offset) + "(" + instr.base_address + ")\n";
                }
            }
        } 
        // Handle I-type instructions
        else if (instr.format == "i-type"  || 
                instr.operation == "ADDI"  || instr.operation == "addi"  || 
                instr.operation == "SLTI"  || instr.operation == "slti"  || 
                instr.operation == "XORI"  || instr.operation == "xori"  || 
                instr.operation == "SLTIU" || instr.operation == "sltiu" || 
                instr.operation == "SLTI"  || instr.operation == "slti"  || 
                instr.operation == "ORI"   || instr.operation == "ori"   || 
                instr.operation == "ANDI"  || instr.operation == "andi"  ||
                instr.operation == "SLLI"  || instr.operation == "slli"  || 
                instr.operation == "SRLI"  || instr.operation == "srli"  || 
                instr.operation == "SRAI"  || instr.operation == "srai"  || 
                instr.operation == "JALR"  || instr.operation == "jalr") 
            {
            
            // For ADDI instructions
            if (instr.operation == "ADDI") {
                if (instr.imm > 2047 || instr.imm < -2048) {
                    // For large immediates, we need to use LUI + ADDI
                    auto [lui_val, addi_val] = calculateLuiAddiValues(instr.imm);
                    
                    // Convert addi_val to signed 12-bit value if it exceeds range
                    if (addi_val & 0x800) {
                        // Sign extend to print as negative number
                        addi_val = addi_val | 0xFFFFF000;
                    }
                    
                    std::string result = "";
                    // Add comment explaining the LUI+ADDI sequence
                    result += "    # Loading immediate " + std::to_string(instr.imm) + 
                             " using LUI+ADDI: " + std::to_string(lui_val) + " << 12 + " + 
                             std::to_string(addi_val) + " = " + 
                             std::to_string((lui_val << 12) + addi_val) + "\n";
                    
                    if (lui_val != 0) {
                        result += "    lui " + instr.rd + ", " + std::to_string(lui_val) + "\n";
                        result += "    addi " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(addi_val) + "\n";
                    } else {
                        result += "    addi " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
                    }
                    return result;
                } else {
                    return "    addi " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
                }
            }
            // Handle JALR instruction
            else if (instr.operation == "JALR") {
                return "    jalr " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
            }
            else if (instr.operation == "SLLI"  || instr.operation == "slli"  || 
                     instr.operation == "SRLI"  || instr.operation == "srli"  || 
                     instr.operation == "SRAI"  || instr.operation == "srai"  ) {
                std::string op = instr.operation;
                std::transform(op.begin(), op.end(), op.begin(), ::tolower);
                return "    " + op + " " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
            }
            // Handle other I-type instructions
            else {
                std::string op = instr.operation;
                std::transform(op.begin(), op.end(), op.begin(), ::tolower);
                return "    " + op + " " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
            }
        }
        // Handle R-type operations
        else if (instr.format == "r-type" || 
                instr.operation == "ADD" || instr.operation == "SUB" || 
                instr.operation == "SLL" || instr.operation == "SLT" || 
                instr.operation == "SLTU" || instr.operation == "XOR" || 
                instr.operation == "SRL" || instr.operation == "SRA" || 
                instr.operation == "OR" || instr.operation == "AND" || 
                instr.operation == "MUL") {
            
            std::string op = instr.operation;
            std::transform(op.begin(), op.end(), op.begin(), ::tolower);
            return "    " + op + " " + instr.rd + ", " + instr.ra1 + ", " + instr.ra2 + "\n";
        }
        // Handle B-type instructions
        else if (instr.operation == "BEQ" || instr.operation == "BNE" || 
                instr.operation == "BLT" || instr.operation == "BGE" || 
                instr.operation == "BLTU" || instr.operation == "BGEU") {
            
            std::string op = instr.operation;
            std::transform(op.begin(), op.end(), op.begin(), ::tolower);
            return "    " + op + " " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        }
        // Handle U-type instructions
        else if (instr.operation == "LUI" || instr.operation == "AUIPC") {
            std::string op = instr.operation;
            std::transform(op.begin(), op.end(), op.begin(), ::tolower);
            return "    " + op + " " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        }
        // Handle J-type instructions (JAL)
        else if (instr.operation == "JAL" || instr.operation == "jal") {
            // For function calls, use the provided address
            std::cout << "instr.target: " << instr.target << std::endl;
            if (!instr.target.empty()) {
                return "    jal " + instr.rd + ", " + std::to_string(instr.address) + "  # Call " + instr.target + "\n";
            }
            // For regular jumps, use the immediate
            return "    jal " + instr.rd + ", " + std::to_string(instr.imm) + "  # Call somewhere\n";
        }

        // Handle B-type instructions  
        else if (instr.operation == "BGE" || instr.operation == "bge") {
            return "    bge " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        }
        else if (instr.operation == "BLT" || instr.operation == "blt") {
            return "    blt " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        }   
        else if (instr.operation == "BLTU" || instr.operation == "bltu") {
            return "    bltu " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        }
        else if (instr.operation == "BNE" || instr.operation == "bne") {
            return "    bne " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        } 
        else if (instr.operation == "BEQ" || instr.operation == "beq") {
            return "    beq " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        }
        else if (instr.operation == "BGEU" || instr.operation == "bgeu") {
            return "    bgeu " + instr.rd + ", " + instr.ra1 + ", " + std::to_string(instr.imm) + "\n";
        }

        

        // Handle special instructions
        else if (instr.operation == "RET") {
            return "    ret\n";
        }
        else if (instr.operation == "NOP" || instr.operation == "nop") {
            return "    nop\n";
        }
        
        return "    # Unknown instruction: " + instr.operation + " (format: " + instr.format + ")\n";
    }

    // Helper function to calculate hardware loop immediate value
    uint32_t calculateHWLImmediate(const HardwareLoop& hwl, int delay) {
        uint32_t imm = 0;
        imm |= (static_cast<uint32_t>(hwl.pc_start + delay & 0x1FF) << 23);  //  9 bits pc_start
        imm |= (static_cast<uint32_t>((hwl.pc_stop - hwl.pc_start) & 0x3F) << 17);   // 6 bits pc_stop
        imm |= (static_cast<uint32_t>(hwl.hwl_index & 0x1F) << 12); // 5 bits hwl_index
        imm |= (static_cast<uint32_t>(hwl.iterations & 0xFFF));     // 12 bits iterations
        return imm;
    }

    // Helper function to split immediate into upper and lower parts
    std::pair<uint32_t, uint32_t> splitHWLImmediate(uint32_t imm) {
        uint32_t upper = (imm >> 12) & 0xFFFFF;  // Upper 20 bits
        uint32_t lower = imm & 0xFFF;            // Lower 12 bits
        if (lower & 0x800) {  // If the highest bit of lower part is 1
            upper += 1;       // Add 1 to upper to handle sign extension
        }
        return {upper, lower};
    }

public:
    DFGProcessor() : output_folder("build/") {}
    DFGProcessor(const std::string& output_folder) : output_folder(output_folder) {}

    void loadConfig(const std::string& yaml_file) {
        YAML::Node config = YAML::LoadFile(yaml_file);

        // Load memory configuration
        if (config["mem_config"]) {
            auto mem_conf = config["mem_config"];
            for (const auto& entry : mem_conf) {
                std::string reg = entry.first.as<std::string>();
                if (!entry.second.IsNull()) {
                    mem_config[reg] = entry.second.as<int>();
                }
            }
        }

        // Load delay_start array if present
        if (config["delay_start"]) {
            auto delay_array = config["delay_start"];
            delay_start.clear();  // Clear any existing values
            for (const auto& delay : delay_array) {
                delay_start.push_back(delay.as<int>());
            }
            std::cout << "Loaded delay_start values: ";
            for (int delay : delay_start) {
                std::cout << delay << " ";
            }
            std::cout << std::endl;
        } else {
            // Initialize with zeros if not present
            delay_start.resize(64, 0);  // Support up to 64 PEs
        }

        // Load memory offsets
        if (config["hardware_config"]["psrf_mem_offset"]) {
            auto offset_conf = config["hardware_config"]["psrf_mem_offset"];
            for (const auto& entry : offset_conf) {
                std::string offset_key = entry.first.as<std::string>();
                if (!entry.second.IsNull()) {
                    mem_offsets[offset_key] = entry.second.as<int>();
                }
            }
        }

        // Load hardware configuration
        total_pes = config["hardware_config"]["total_pes"].as<int>();
        clusters_count = config["hardware_config"]["clusters"]["count"].as<int>();
        pes_per_cluster = config["hardware_config"]["clusters"]["pes_per_cluster"].as<int>();
        minimum_pes_required = config["scheduling"]["minimum_pes_required"].as<int>();
        data_dup = config["hardware_config"]["data_dup"].as<int>();

        // Load PE assignments
        auto assignments = config["scheduling"]["pe_assignments"];
        for (const auto& assignment : assignments) {
            PEAssignment pe_assignment;
            pe_assignment.pe_id = assignment["pe_id"].as<int>();
            pe_assignment.has_psrf_mem_type = false;
            pe_assignment.has_mem_type = false;
            pe_assignment.has_hwl = false;
            
            for (const auto& instr : assignment["instructions"]) {
                Instruction instruction;
                instruction.operation = instr["operation"].as<std::string>();
                instruction.format = instr["format"].as<std::string>();
                
                // Handle hardware loop instructions
                if (instruction.format == "hwl-type") {
                    pe_assignment.has_hwl = true;
                    HardwareLoop hwl;
                    hwl.loop_id = instr["loop_id"].as<int>();
                    hwl.pc_start = instr["pc_start"].as<int>();
                    hwl.pc_stop = instr["pc_stop"].as<int>();
                    hwl.hwl_index = instr["hwl_index"].as<int>();
                    hwl.iterations = instr["iterations"].as<int>();
                    instruction.hwl = hwl;
                }
                
                // Handle register assignments
                instruction.ra1 = "null";
                instruction.ra2 = "null";
                instruction.rd = "null";
                if (instr["ra1"] && !instr["ra1"].IsNull()) {
                    instruction.ra1 = instr["ra1"].as<std::string>();
                }
                if (instr["ra2"] && !instr["ra2"].IsNull()) {
                    instruction.ra2 = instr["ra2"].as<std::string>();
                }
                if (instr["rd"] && !instr["rd"].IsNull()) {
                    instruction.rd = instr["rd"].as<std::string>();
                }

                // Handle immediate value for I-type instructions
                instruction.imm = 0;  // Default value
                if (instr["imm"] && !instr["imm"].IsNull()) {
                    instruction.imm = instr["imm"].as<int>();
                }
                
                // Set operation to uppercase for standard operations if needed
                if (instruction.format == "i-type" || instruction.format == "r-type") {
                    instruction.operation = instruction.operation;
                    // Make sure operation is uppercase for standard operations
                    if (instruction.operation == "addi" || instruction.operation == "add" ||
                        instruction.operation == "mul" || instruction.operation == "lw" ||
                        instruction.operation == "sw") {
                        // Convert to uppercase for internal processing
                        std::string upper_op = instruction.operation;
                        std::transform(upper_op.begin(), upper_op.end(), upper_op.begin(), ::toupper);
                        instruction.operation = upper_op;
                    }
                }
                
                // Handle base address
                if (instr["base_address"] && !instr["base_address"].IsNull()) {
                    instruction.base_address = instr["base_address"].as<std::string>();
                    if (instruction.format == "psrf-mem-type" || instruction.format == "mem-type") {
                        pe_assignment.required_base_registers.insert(instruction.base_address);
                    }
                }

                // Load var field for psrf-mem-type
                if (instruction.format == "psrf-mem-type") {
                    pe_assignment.has_psrf_mem_type = true;
                    if (instr["var"] && !instr["var"].IsNull()) {
                        instruction.var = instr["var"].as<int>();
                    }

                    // Load psrf_var values
                    if (instr["psrf_var"] && !instr["psrf_var"].IsNull()) {
                        auto psrf_vars = instr["psrf_var"];
                        for (const auto& var : psrf_vars) {
                            instruction.psrf_var[var.first.as<std::string>()] = var.second.as<int>();
                        }
                    }

                    // Load coefficients
                    if (instr["coefficients"] && !instr["coefficients"].IsNull()) {
                        auto coeffs = instr["coefficients"];
                        for (const auto& coeff : coeffs) {
                            instruction.coefficients[coeff.first.as<std::string>()] = coeff.second.as<int>();
                        }
                    }
                }

                if (instruction.format == "mem-type") {
                    pe_assignment.has_mem_type = true;
                }
                
                // Load target field for JAL instructions
                if (instr["target"] && !instr["target"].IsNull()) {
                    instruction.target = instr["target"].as<std::string>();
                }

                // Load address field for JAL instructions
                if (instr["address"] && !instr["address"].IsNull()) {
                    instruction.address = instr["address"].as<int>();
                }

                // Load offset field for memory operations
                if (instr["offset"] && !instr["offset"].IsNull()) {
                    instruction.offset = instr["offset"].as<int>();
                }

                pe_assignment.instructions.push_back(instruction);
            }
            pe_assignments.push_back(pe_assignment);
        }

        // Load function definitions
        if (config["functions"]) {
            auto functions = config["functions"];
            for (const auto& func : functions) {
                std::string func_name = func.first.as<std::string>();
                int func_address = func.second["address"].as<int>();
                
                // Store function address for later use
                function_addresses[func_name] = func_address;
                
                // Process PE assignments for this function
                auto pe_assigns = func.second["pe_assignments"];
                for (const auto& pe_assign : pe_assigns) {
                    int pe_id = pe_assign["pe_id"].as<int>();
                    PEAssignment func_pe_assignment;
                    func_pe_assignment.pe_id = pe_id;
                    func_pe_assignment.has_psrf_mem_type = false;
                    func_pe_assignment.has_hwl = false;
                    
                    for (const auto& instr : pe_assign["instructions"]) {
                        Instruction instruction;
                        instruction.operation = instr["operation"].as<std::string>();
                        instruction.format = instr["format"].as<std::string>();
                        
                        // Handle register assignments
                        instruction.ra1 = "null";
                        instruction.ra2 = "null";
                        instruction.rd = "null";
                        if (instr["ra1"] && !instr["ra1"].IsNull()) {
                            instruction.ra1 = instr["ra1"].as<std::string>();
                        }
                        if (instr["ra2"] && !instr["ra2"].IsNull()) {
                            instruction.ra2 = instr["ra2"].as<std::string>();
                        }
                        if (instr["rd"] && !instr["rd"].IsNull()) {
                            instruction.rd = instr["rd"].as<std::string>();
                        }

                        // Handle immediate value for I-type instructions
                        instruction.imm = 0;
                        if (instr["imm"] && !instr["imm"].IsNull()) {
                            instruction.imm = instr["imm"].as<int>();
                        }
                        
                        // Set operation to uppercase for standard operations
                        if (instruction.format == "i-type" || instruction.format == "r-type") {
                            std::string upper_op = instruction.operation;
                            std::transform(upper_op.begin(), upper_op.end(), upper_op.begin(), ::toupper);
                            instruction.operation = upper_op;
                        }
                        
                        func_pe_assignment.instructions.push_back(instruction);
                    }
                    std::cout << "PE " << pe_id << " Function PE assignment: " << func_pe_assignment.instructions.size() << std::endl;
                    // Store function PE assignment
                    function_pe_assignments[func_name][pe_id] = func_pe_assignment;
                }
            }
        }
    }

    void generateAssembly() {
        // Generate assembly for each PE
        std::cout << "Generating assembly for " << total_pes << " PEs" << std::endl;
        for (int pe = 0; pe < total_pes; pe++) {

            int base_pe = pe % pes_per_cluster;
            std::cout << "Base PE: " << base_pe << std::endl;
            std::cout << "Minimum PEs required: " << minimum_pes_required << std::endl;
            if (base_pe > minimum_pes_required) {
                std::cout << "Skipping PE " << pe << " due to minimum PEs required" << std::endl;
                continue;
            }
            const PEAssignment& assignment = pe_assignments[base_pe];
            std::cout << "Assignment: " << assignment.instructions.size() << std::endl;
            if (assignment.instructions.size() >= 10000 || assignment.instructions.size() == 0) {
                std::cout << "Skipping PE " << pe << " due to large number of instructions" << std::endl;   
                continue;
            }  
            std::cout << "Assignment: " << assignment.instructions.size() << std::endl;

            std::string filename = output_folder + "pe" + std::to_string(pe) + "_assembly.s";
            std::ofstream outFile(filename);
            
            outFile << "# Assembly for PE" << pe << " (Cluster " << getClusterNumber(pe) << ")\n";
            outFile << "# Generated with PSRF, HWL and function support\n";
            outFile << ".text\n";
            outFile << ".global _start\n\n";
            outFile << "_start:\n";

            // Determine which instruction set to use based on PE number
 

            // Generate base address loading if needed
            if (!assignment.required_base_registers.empty()) {
                outFile << generateBaseAddressLoading(pe, data_dup);
            }

            std::cout << "Assignment has psrf mem type: " << assignment.has_psrf_mem_type << std::endl;
            std::cout << "Assignment has mem type: " << assignment.has_mem_type << std::endl;
            // Generate preload section if needed
            if (assignment.has_psrf_mem_type || assignment.has_mem_type) {
                std::cout << "Generating preload section" << std::endl;
                outFile << generatePreloadSection(assignment);
            }



            // Add comment to mark the beginning of the execution section
            outFile << "    # ========== Execution Section Begin ==========\n";
            // Add delay NOPs before execution section
            if (pe < delay_start.size() && delay_start[pe] > 0) {
                outFile << "    # Adding " << delay_start[pe] << " NOPs for delay\n";
                for (int i = 0; i < delay_start[pe]; i++) {
                    outFile << "    nop\n";
                }
                outFile << "\n";
            }
            // Generate instructions
            int hwl_count = 0;  // Counter for hardware loop immediates
            for (const auto& instr : assignment.instructions) {
                outFile << generateInstructionCode(instr, hwl_count, pe);
            }

            // Generate function sections
            if (!function_pe_assignments.empty()) {
                outFile << "\n    # ========== Function Sections ==========\n";
                for (const auto& func : function_pe_assignments) {
                    const std::string& func_name = func.first;
                    const auto& pe_assigns = func.second;
                    
                    if (pe_assigns.find(pe) != pe_assigns.end()) {
                        const PEAssignment& func_assignment = pe_assigns.at(pe);
                        
                        // Add function label
                        outFile << "\n" << func_name << ":\n";
                        outFile << "    # Function " << func_name << " (address: 0x" 
                               << std::hex << function_addresses[func_name] << std::dec << ")\n";
                        
                        for (const auto& instr : func_assignment.instructions) {
                            outFile << generateInstructionCode(instr, hwl_count, pe);
                        }
                        
                        // Add return instruction if not already present
                        if (func_assignment.instructions.empty() || 
                            func_assignment.instructions.back().operation != "JALR") {
                            outFile << "    jalr x0, x26, 0  # Return from function\n";
                        }
                    }
                }
            }

            outFile << "    # End of program\n";
            outFile << "    ret\n";
            outFile.close();
            
            std::cout << "Generated assembly for PE" << pe << " (Cluster " << 
                     getClusterNumber(pe) << ") in " << filename << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    // Check if correct number of arguments is provided
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <yaml_file> [output_folder]" << std::endl;
        std::cerr << "  yaml_file: Path to the YAML configuration file" << std::endl;
        std::cerr << "  output_folder: Directory to store generated assembly files (default: 'build')" << std::endl;
        return 1;
    }
    
    // Parse arguments
    std::string yaml_file = argv[1];
    std::string output_folder = (argc >= 3) ? argv[2] : "build";
    
    // Ensure output folder ends with a trailing slash
    if (!output_folder.empty() && output_folder.back() != '/') {
        output_folder += '/';
    }
    
    // Create output directory if it doesn't exist
    std::string mkdir_cmd = "mkdir -p " + output_folder;
    int dir_result = system(mkdir_cmd.c_str());
    if (dir_result != 0) {
        std::cerr << "Warning: Failed to create output directory: " << output_folder << std::endl;
    }
    
    std::cout << "Input YAML file: " << yaml_file << std::endl;
    std::cout << "Output folder: " << output_folder << std::endl;
    
    DFGProcessor processor(output_folder);
    
    try {
        processor.loadConfig(yaml_file);
        processor.generateAssembly();
        std::cout << "Assembly generation completed successfully!" << std::endl;
    } catch (const YAML::Exception& e) {
        std::cerr << "Error processing YAML file: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 