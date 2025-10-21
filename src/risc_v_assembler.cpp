#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <cstdint>
#include <bitset>
#include <iomanip>
#include <sstream>

struct AssembledInstruction {
    std::string op;
    std::string binary;
    std::string hex;
    bool is_execution;
};

class RISC_V_Assembler {
private:
    std::map<std::string, int> registers;
    std::map<std::string, int> registers_c;
    std::map<std::string, int> registers_p;
    std::map<std::string, int> hwl_registers; // Hardware loop registers (L1-L7)
    std::map<std::string, std::string> instructions;
    std::map<std::string, std::string> funct3;
    std::map<std::string, std::string> funct7;

    // Helper function to trim whitespace from start and end of string
    std::string trim_string(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r\f\v");
        if (std::string::npos == first) {
            return "";
        }
        size_t last = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(first, (last - first + 1));
    }

public:
    RISC_V_Assembler() {
        // Initialize registers
        for (int i = 0; i < 32; i++) {
            registers["x" + std::to_string(i)] = i;
            registers_c["c" + std::to_string(i)] = i;  // c0-c31 map to register numbers 0-31
            registers_p["v" + std::to_string(i)] = i;  // v0-v31 map to register numbers 0-31
        }

        // Initialize hardware loop registers (L1-L7)
        for (int i = 1; i <= 7; i++) {
            hwl_registers["L" + std::to_string(i)] = i;
        }

        // Initialize instructions with their opcodes
        instructions = {
            {"lb", "0000011"}, {"lh", "0000011"}, {"lw", "0000011"}, 
            {"lbu", "0000011"}, {"lhu", "0000011"},

            {"addi", "0010011"}, {"slli", "0010011"}, {"slti", "0010011"}, {"sltiu", "0010011"},
            {"xori", "0010011"}, {"srli", "0010011"}, {"srai", "0010011"}, {"ori", "0010011"}, 
            {"andi", "0010011"}, {"auipc", "0010111"}, 

            {"sb", "0100011"}, {"sh", "0100011"}, {"sw", "0100011"},

            {"add", "0110011"}, {"sub", "0110011"}, {"sll", "0110011"}, {"slt", "0110011"},
            {"sltu", "0110011"}, {"xor", "0110011"}, {"srl", "0110011"}, {"sra", "0110011"},
            {"or", "0110011"}, {"and", "0110011"}, {"mul", "0110011"}, {"lui", "0110111"}, 
             
            {"beq", "1100011"}, {"bne", "1100011"}, {"blt", "1100011"}, 
            {"bge", "1100011"}, {"bltu", "1100011"}, {"bgeu", "1100011"},

            {"jalr", "1100111"}, {"jal", "1101111"},

            {"psrf.lw", "0000100"}, {"psrf.sw", "0100100"},
            {"psrf.lb", "0000100"}, {"psrf.sb", "0100100"},
            {"psrf.zd.lw", "0000100"}, 
            {"ppsrf.addi", "0010100"}, // opcode 0x14 for ppsrf.addi
            {"corf.addi", "0010100"}, // opcode 0x14 for corf.addi
            {"corf.lui", "0111011"}, // opcode 0x3B for corf.lui
            {"hwlrf.lui", "0111100"}, // Updated to 0x3C
            {"hwlrf.addi", "0010100"}, // Updated to 0x14
            {"ret", "0000000"},
        };

        // Initialize function 3 values for I-type instructions
        funct3 = {
            {"lb", "000"}, {"lh", "001"}, {"lw", "010"}, 
            {"lbu", "100"}, {"lhu", "101"},

            {"addi", "000"}, {"slli", "001"}, {"slti", "010"}, {"sltiu", "011"},
            {"xori", "100"}, {"srli", "101"}, {"srai", "101"}, {"ori", "110"}, 
            {"andi", "111"}, 

            {"sb", "000"}, {"sh", "001"}, {"sw", "010"},

            {"add", "000"}, {"sub", "000"}, {"sll", "001"}, {"slt", "010"},
            {"sltu", "011"}, {"xor", "100"}, {"srl", "101"}, {"sra", "101"},
            {"or", "110"}, {"and", "111"}, {"mul", "000"}, 
             
            {"beq", "000"}, {"bne", "001"}, {"blt", "100"}, 
            {"bge", "101"}, {"bltu", "110"}, {"bgeu", "111"},

            {"jalr", "000"}, 

            {"psrf.lw", "111"}, {"psrf.lb", "000"}, {"mul", "000"},
            {"psrf.zd.lw", "110"}, 
            {"psrf.sw", "100"}, {"psrf.sb", "000"}, 
            {"ppsrf.addi", "001"}, // func3=1 for ppsrf.addi
            {"corf.addi", "000"},  // func3=0 for corf.addi
            {"hwlrf.addi", "010"}, // Updated to func3=2
        };

        // Initialize funct7
        funct7 = {
            {"slli", "0000000"}, {"srli", "0000000"}, {"srai", "0100000"},
            {"add", "0000000"}, {"sub", "0100000"}, {"sll", "0000000"}, {"slt", "0000000"},
            {"sltu", "0000000"}, {"xor", "0000000"}, {"srl", "0000000"}, {"sra", "0100000"},
            {"or", "0000000"}, {"and", "0000000"}, {"mul", "0000001"}, 
        };
    }

    std::string to_binary(int num, int length) {
        if (num < 0) {
            num = (1 << length) + num;
        }
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(length) << std::bitset<32>(num).to_string().substr(32 - length);
        return ss.str();
    }

    std::string to_hex(const std::string& binary) {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(8) << std::hex << std::stoul(binary, nullptr, 2);
        return ss.str();
    }

    std::string assemble_r_type(const std::string& instruction, const std::string& rd, 
                               const std::string& rs1, const std::string& rs2) {
        std::string opcode = instructions[instruction];
        std::string func3 = funct3[instruction];
        std::string func7 = funct7[instruction];
        std::string rd_bin = to_binary(registers[rd], 5);
        std::string rs1_bin = to_binary(registers[rs1], 5);
        std::string rs2_bin = "";
        if (instruction == "slli" || instruction == "srli" || instruction == "srai") {
            int imm = std::stoi(rs2);
            std::string rs2_bin = to_binary(imm, 5); 
            return func7 + rs2_bin + rs1_bin + func3 + rd_bin + opcode;

        } else {
            std::string rs2_bin = to_binary(registers[rs2], 5);
            return func7 + rs2_bin + rs1_bin + func3 + rd_bin + opcode;
        }
    }

    std::string assemble_i_type(const std::string& instruction, const std::string& rd, 
                               const std::string& rs1, int imm) {
        std::string opcode = instructions[instruction];
        std::string func3 = funct3[instruction];
        std::string rd_bin = to_binary(registers[rd], 5);
        std::string rs1_bin = to_binary(registers[rs1], 5);
        std::string imm_bin = to_binary(imm, 12);
        std::cout << "instruction: " << instruction << std::endl;
        std::cout << "opcode: " << opcode << std::endl; 
        std::cout << "rd: " << rd << std::endl; 
        std::cout << "rs1: " << rs1 << std::endl;
        std::cout << "imm: " << imm << std::endl;
        std::cout << "imm_bin: " << imm_bin << std::endl;
        std::cout << "rs1_bin: " << rs1_bin << std::endl;
        std::cout << "func3: " << func3 << std::endl;
        std::cout << "rd_bin: " << rd_bin << std::endl;
        std::cout << "opcode: " << opcode << std::endl;
        std::cout << "imm_bin + rs1_bin + func3 + rd_bin + opcode: " << imm_bin + rs1_bin + func3 + rd_bin + opcode << std::endl;   
        return imm_bin + rs1_bin + func3 + rd_bin + opcode;
    }

    std::string assemble_s_type(const std::string& instruction, const std::string& rs1, 
                               const std::string& rs2, int imm) {
        std::string opcode = instructions[instruction];
        std::string func3 = funct3[instruction];
        std::string rs1_bin = to_binary(registers[rs1], 5);
        std::string rs2_bin = to_binary(registers[rs2], 5);
        std::string imm_bin = to_binary(imm, 12);
        std::string imm_high = imm_bin.substr(0, 7);
        std::string imm_low = imm_bin.substr(7, 5);
        return imm_high + rs2_bin + rs1_bin + func3 + imm_low + opcode;
    }

    std::string assemble_b_type(const std::string& instruction, const std::string& rs1, 
                               const std::string& rs2, int imm) {
        std::string opcode = instructions[instruction];
        std::string func3 = funct3[instruction];
        std::string rs1_bin = to_binary(registers[rs1], 5);
        std::string rs2_bin = to_binary(registers[rs2], 5);
        std::string imm_bin = to_binary(imm, 13);
        std::string imm_12 = imm_bin.substr(0, 1);
        std::string imm_10_5 = imm_bin.substr(2, 6);
        std::string imm_4_1 = imm_bin.substr(8, 4);
        std::string imm_11 = imm_bin.substr(1, 1);
        return imm_12 + imm_10_5 + rs2_bin + rs1_bin + func3 + imm_4_1 + imm_11 + opcode;
    }

    std::string assemble_psrf_lw_sw(const std::string& instruction, const std::string& rd, 
                                   const std::string& rs1, int imm) {
        std::string opcode = instructions[instruction];
        std::string func3 = funct3[instruction];
        std::string rd_bin = to_binary(registers[rd], 5);
        std::string rs1_bin = to_binary(registers[rs1], 5);
        std::string imm_bin = to_binary(imm, 12);
        std::cout << "instruction: " << instruction << std::endl;
        std::cout << "func3: " << func3 << std::endl;
        std::cout << "opcode: " << opcode << std::endl; 
        std::cout << "rd: " << rd << std::endl; 
        std::cout << "rs1: " << rs1 << std::endl;
        std::cout << "imm: " << imm << std::endl;
        return imm_bin + rs1_bin + func3 + rd_bin + opcode;
    }

    std::string assemble_u_type(const std::string& instruction, const std::string& rd, int imm) {
        std::string opcode = instructions[instruction];
        std::string rd_bin = to_binary(registers[rd], 5);
        std::string imm_bin = to_binary(imm, 20);
        return imm_bin + rd_bin + opcode;
    }

    std::string assemble_lui(const std::string& op, const std::string& rd, int imm) {
        std::string opcode = instructions[op];
        std::string rd_bin = to_binary(registers[rd], 5);
        std::string imm_bin = to_binary(imm & 0xFFFFF, 20); // Upper 20 bits of immediate
        return imm_bin + rd_bin + opcode;
    }

    std::string assemble_corf_lui(const std::string& op, const std::string& rd, int imm) {
        std::string opcode = instructions[op];
        std::string rd_bin = to_binary(registers_c[rd], 5);
        std::string imm_bin = to_binary(imm & 0xFFFFF, 20); // Upper 20 bits of immediate
        return imm_bin + rd_bin + opcode;
    }
    std::string assemble_corf_addi(const std::string& op, const std::string& rd, const std::string& rs1, int imm) {
        std::string opcode = instructions[op];
        std::string func3 = funct3[op]; // This should be "000"
        std::string rd_bin = to_binary(registers_c[rd], 5);  // Use registers_c for c-registers
        std::string rs1_bin = to_binary(registers_c[rs1], 5); // Use registers_c for c-registers
        std::string imm_bin = to_binary(imm, 12);
        return imm_bin + rs1_bin + func3 + rd_bin + opcode;
    }

    std::string assemble_ppsrf_addi(const std::string& op, const std::string& rd, const std::string& rs1, int imm) {
        std::cout << "op: " << op << std::endl; 
        std::cout << "rd: " << rd << std::endl;
        std::cout << "rs1: " << rs1 << std::endl;
        std::cout << "imm: " << imm << std::endl;
        std::string opcode = instructions[op];
        std::string func3 = funct3[op]; // This should be "001"
        std::string rd_bin = to_binary(registers_p[rd], 5);  // Use registers_p for v-registers
        std::string rs1_bin = to_binary(registers_p[rs1], 5); // Use registers_p for v-registers
        std::string imm_bin = to_binary(imm, 12);
        return imm_bin + rs1_bin + func3 + rd_bin + opcode;
    }

    std::string assemble_hwlrf_lui(const std::string& rd, int imm) {
        std::string opcode = instructions["hwlrf.lui"];
        std::string rd_bin = to_binary(hwl_registers[rd], 5);
        std::string imm_bin = to_binary(imm, 20);
        return imm_bin + rd_bin + opcode;
    }

    std::string assemble_hwlrf_addi(const std::string& rd, const std::string& rs1, int imm) {
        std::string opcode = instructions["hwlrf.addi"];
        std::string func3 = funct3["hwlrf.addi"];
        std::string rd_bin = to_binary(hwl_registers[rd], 5);
        std::string rs1_bin = to_binary(hwl_registers[rs1], 5);
        std::string imm_bin = to_binary(imm, 12);
        return imm_bin + rs1_bin + func3 + rd_bin + opcode;
    }

    std::string assemble_j_type(const std::string& rd, int imm) {
        std::string opcode = instructions["jal"];
        std::string rd_bin = to_binary(registers[rd], 5);
        
        // J-type immediate format: [20|10:1|11|19:12]
        std::string imm_bin = to_binary(imm, 21);
        std::string imm_20 = imm_bin.substr(0, 1);
        std::string imm_10_1 = imm_bin.substr(11, 10);
        std::string imm_11 = imm_bin.substr(10, 1);
        std::string imm_19_12 = imm_bin.substr(1, 8);
        
        return imm_20 + imm_10_1 + imm_11 + imm_19_12 + rd_bin + opcode;
    }

    // Parse a single assembly instruction line
    AssembledInstruction parse_instruction(const std::string& line) {
        AssembledInstruction result;
        
        // Split the line into tokens (opcode and arguments)
        std::istringstream iss(line);
        std::string op, args_str;
        iss >> op; // Extract opcode
        
        std::getline(iss, args_str); // Get the rest
        args_str = trim_string(args_str);
        
        // Store original operation
        result.op = op;
        
        // Parse arguments
        std::vector<std::string> args;
        size_t pos = 0;
        std::string token;
        bool in_parentheses = false;
        
        for (size_t i = 0; i < args_str.length(); i++) {
            char c = args_str[i];
            if (c == '(') {
                in_parentheses = true;
            } else if (c == ')') {
                in_parentheses = false;
            }
            
            if (c == ',' && !in_parentheses) {
                // Found argument separator outside parentheses
                token = trim_string(args_str.substr(pos, i - pos));
                if (!token.empty()) {
                    args.push_back(token);
                }
                pos = i + 1;
            }
        }
        
        // Add the last argument
        if (pos < args_str.length()) {
            token = trim_string(args_str.substr(pos));
            if (!token.empty()) {
                args.push_back(token);
            }
        }

        // Handle HWLRF instructions
        if (op == "hwlrf.lui") {
            if (args.size() >= 2) {
                result.binary = assemble_hwlrf_lui(args[0], std::stoi(args[1]));
            }
        }
        else if (op == "hwlrf.addi") {
            if (args.size() >= 3) {
                result.binary = assemble_hwlrf_addi(args[0], args[1], std::stoi(args[2]));
            }
        }
        // Handle standard LUI and AUIPC instructions
        else if (op == "lui" || op == "auipc") {
            if (args.size() >= 2) {
                result.binary = assemble_u_type(op, args[0], std::stoi(args[1]));
            }
        }
        // Handle R-type instructions
        else if (op == "add" || op == "sub" || op == "sll" || op == "slt" || op == "sltu" || 
                op == "xor" || op == "srl" || op == "sra" || op == "or" || op == "and" ||
                op == "mul" || op == "slli" || op == "srli" || op == "srai") {
            if (args.size() >= 3) {
                result.binary = assemble_r_type(op, args[0], args[1], args[2]);
            }
        }
        // Handle I-type instructions
        else if (op == "addi" || op == "slti" || op == "sltiu" || op == "xori" || op == "ori" || 
                op == "andi"  || op == "jalr") {
            if (args.size() >= 3) {
                result.binary = assemble_i_type(op, args[0], args[1], std::stoi(args[2]));
            }
        }
        // Handle S-type instructions
        else if (op == "sb" || op == "sh" || op == "sw") {
            if (args.size() >= 2) {
                std::string rs2 = args[0];
                std::string offset_base = args[1];
                
                // Parse offset and base register
                size_t open_paren = offset_base.find('(');
                size_t close_paren = offset_base.find(')', open_paren);
                
                if (open_paren != std::string::npos && close_paren != std::string::npos) {
                    std::string offset_str = trim_string(offset_base.substr(0, open_paren));
                    std::string base_reg = trim_string(offset_base.substr(open_paren + 1, close_paren - open_paren - 1));
                    
                    int offset = 0;
                    if (!offset_str.empty()) {
                        offset = std::stoi(offset_str);
                    }
                    
                    result.binary = assemble_s_type(op, base_reg, rs2, offset);
                }
            }
        }
        // Handle B-type instructions
        else if (op == "beq" || op == "bne" || op == "blt" || op == "bge" || 
                op == "bltu" || op == "bgeu") {
            if (args.size() >= 3) {
                result.binary = assemble_b_type(op, args[0], args[1], std::stoi(args[2]));
            }
        }
        // Handle J-type instructions
        else if (op == "jal") {
            if (args.size() >= 2) {
                std::string rd = args[0];
                int offset = std::stoi(args[1]);
                result.binary = assemble_j_type(rd, offset);
            }
        }

        // Handle PPSRF instructions
        else if (op == "ppsrf.addi") {
            std::cout << "ppsrf.addiop: " << op << std::endl;
            if (args.size() >= 3) {
                result.binary = assemble_ppsrf_addi(op, args[0], args[1], std::stoi(args[2]));
            }
        }
        // Handle CORF instructions
        else if (op == "corf.addi") {
            if (args.size() >= 3) {
                result.binary = assemble_corf_addi(op, args[0], args[1], std::stoi(args[2]));
            }
        }
        else if (op == "corf.lui") {
            if (args.size() >= 2) {
                result.binary = assemble_corf_lui(op, args[0], std::stoi(args[1]));
            }
        }
        // Handle special instructions
        else if (op == "ret") {
            // result.binary = assemble_i_type("jalr", "x0", "x1", 0);
            result.binary = assemble_i_type("addi", "x0", "x0", 0);
        }
        else if (op == "nop") {
            result.binary = assemble_i_type("addi", "x0", "x0", 0);
        }
        

        // Handle PSRF instructions
        else if (op.find("psrf.") != std::string::npos) {
            if (op == "psrf.lw" || op == "psrf.lb" || op == "psrf.zd.lw") {
                if (args.size() >= 2) {
                    std::string rd = args[0];
                    std::string offset_base = args[1];
                    
                    size_t open_paren = offset_base.find('(');
                    size_t close_paren = offset_base.find(')', open_paren);
                    
                    if (open_paren != std::string::npos && close_paren != std::string::npos) {
                        std::string offset_str = trim_string(offset_base.substr(0, open_paren));
                        std::string base_reg = trim_string(offset_base.substr(open_paren + 1, close_paren - open_paren - 1));
                        
                        int offset = 0;
                        if (!offset_str.empty()) {
                            offset = std::stoi(offset_str);
                        }
                        
                        result.binary = assemble_psrf_lw_sw(op, rd, base_reg, offset);
                    }
                }
            }
            else if (op == "psrf.sw" || op == "psrf.sb") {
                if (args.size() >= 2) {
                    std::string rs2 = args[0];
                    std::string offset_base = args[1];
                    
                    size_t open_paren = offset_base.find('(');
                    size_t close_paren = offset_base.find(')', open_paren);
                    
                    if (open_paren != std::string::npos && close_paren != std::string::npos) {
                        std::string offset_str = trim_string(offset_base.substr(0, open_paren));
                        std::string base_reg = trim_string(offset_base.substr(open_paren + 1, close_paren - open_paren - 1));
                        
                        int offset = 0;
                        if (!offset_str.empty()) {
                            offset = std::stoi(offset_str);
                        }
                        
                        result.binary = assemble_psrf_lw_sw(op, rs2, base_reg, offset);
                    }
                }
            }
        }

        // Convert binary to hex
        if (!result.binary.empty()) {
            result.hex = to_hex(result.binary);
        }
        
        return result;
    }

    // Main assembly function - reads input file, writes output files
    int assemble(const std::string& input_file, const std::string& output_file, 
                int pe_number = 0, const std::string& mem_file_path = "",
                std::vector<std::string>* memory_entries = nullptr) {
        // Read each line from input file
        std::ifstream file(input_file);
        if (!file) {
            std::cerr << "Error: Cannot open input file" << std::endl;
            return 1;
        }
        
        std::cout << "Input file: " << input_file << std::endl;
        std::cout << "Output file: " << output_file << std::endl;
        std::cout << "PE number: " << pe_number << " (will be encoded in bits [13:10])" << std::endl;
        
        // Use provided mem file path or create one based on output file
        std::string actual_mem_file_path = mem_file_path.empty() ? 
                                          output_file + ".mem" : mem_file_path;
        
        // Vectors to store opcodes and assembly instructions
        std::vector<AssembledInstruction> assembled;
        
        std::string line;
        bool in_execution_section = false;
        
        while (std::getline(file, line)) {
            // Skip empty lines, comments, and labels
            std::string trimmed = trim_string(line);
            if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == '.' || 
                trimmed[0] == '_' || trimmed.find(':') != std::string::npos) {
                
                // Check for execution section marker
                if (trimmed.find("Execution Section Begin") != std::string::npos) {
                    in_execution_section = true;
                }
                continue;
            }
            
            // Parse the instruction
            AssembledInstruction instr = parse_instruction(trimmed);
            instr.is_execution = in_execution_section;
            assembled.push_back(instr);
        }
        
        file.close();
        
        // Open output files
        std::ofstream hex_file(output_file);
        std::ofstream mem_file(actual_mem_file_path);
        if (!hex_file || !mem_file) {
            std::cerr << "Error: Cannot open output files" << std::endl;
            return 1;
        }
        
        // Print and save each assembled instruction
        int preload_count = 0, execution_count = 0;
        for (size_t i = 0; i < assembled.size(); i++) {
            const auto& instr = assembled[i];
            
            // Calculate memory address based on section
            int address;
            if (instr.is_execution) {
                // Execution section: bit 10 = 0, PE number in bits [13:10]
                address = ((pe_number & 0xFF) << 10) | execution_count;
                execution_count++;
            } else {
                // Preload section: bit 10 = 1, PE number in bits [13:10]
                // PE number occupies bits [13:10], bit 10 is set to 1 for preload
                address = ((pe_number & 0xFF) << 10) | (1 << 9) | preload_count;
                preload_count++;
            }
            
            std::cout << std::setw(5) << i << ": " << instr.op 
                      << " -> 0x" << instr.hex 
                      << " (addr: 0x" << std::hex << address << std::dec << ")"
                      << (instr.is_execution ? " [EXEC]" : " [PRELOAD]")
                      << std::endl;
            
            // Write hex to file
            hex_file << instr.hex << std::endl;
            
            // Create memory entry
            std::stringstream mem_entry;
            mem_entry << "@" << std::hex << std::setw(8) << std::setfill('0') << address << " " 
                     << instr.hex;
            
            // Write to individual mem file
            mem_file << mem_entry.str() << std::endl;
            
            // Store for combined file if requested
            if (memory_entries != nullptr) {
                memory_entries->push_back(mem_entry.str());
            }
        }
        
        hex_file.close();
        mem_file.close();
        
        std::cout << "Assembly conversion complete." << std::endl;
        std::cout << "Hex code written to: " << output_file << std::endl;
        std::cout << "Memory initialization written to: " << actual_mem_file_path << std::endl;
        std::cout << "Preload instructions: " << preload_count << ", Execution instructions: " << execution_count << std::endl;
        
        return 0;
    }
};

int main(int argc, char* argv[]) {
    // Check if required arguments are provided
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file_list> [output_directory]" << std::endl;
        std::cerr << "  file_list: File containing a list of assembly files, one per line" << std::endl;
        std::cerr << "  output_directory: Directory to store output files (default: current directory)" << std::endl;
        return 1;
    }
    
    // Parse arguments
    std::string file_list_path = argv[1];
    std::string output_dir = "./";
    
    if (argc >= 3) {
        output_dir = argv[2];
        // Ensure output directory ends with a slash
        if (output_dir.back() != '/' && output_dir.back() != '\\') {
            output_dir += '/';
        }
    }
    
    // Read the file list
    std::ifstream file_list(file_list_path);
    if (!file_list) {
        std::cerr << "Error: Cannot open file list: " << file_list_path << std::endl;
        return 1;
    }
    
    std::cout << "Processing file list: " << file_list_path << std::endl;
    std::cout << "Output directory: " << output_dir << std::endl;
    
    RISC_V_Assembler assembler;
    std::string assembly_file;
    int result = 0;
    
    // For combined memory file
    std::string combined_mem_file_path = output_dir + "combined_memory.mem";
    std::ofstream combined_mem_file(combined_mem_file_path);
    if (!combined_mem_file) {
        std::cerr << "Error: Cannot create combined memory file: " << combined_mem_file_path << std::endl;
        return 1;
    }
    
    // Store memory entries for each PE to maintain order
    std::map<int, std::vector<std::string>> all_memory_entries;
    
    // Process each assembly file in the list
    while (std::getline(file_list, assembly_file)) {
        // Skip empty lines and comments
        if (assembly_file.empty() || assembly_file[0] == '#') {
            continue;
        }
        
        // Extract the PE number from the filename
        std::string output_basename;
        int pe_number = 0xFFFF; // Default if no PE number found
        std::regex pe_pattern("pe(\\d+)_");
        std::smatch matches;
        
        if (std::regex_search(assembly_file, matches, pe_pattern) && matches.size() > 1) {
            std::string pe_num = matches[1].str();
            pe_number = std::stoi(pe_num);
            output_basename = "pe" + pe_num + "_binary";
        } else {
            // Use the basename of the input file if PE number can't be extracted
            size_t last_slash = assembly_file.find_last_of("/\\");
            size_t last_dot = assembly_file.find_last_of(".");
            std::string basename = assembly_file.substr(
                last_slash == std::string::npos ? 0 : last_slash + 1,
                last_dot == std::string::npos ? assembly_file.length() : last_dot - (last_slash == std::string::npos ? 0 : last_slash + 1)
            );
            output_basename = basename + "_binary";
        }
        
        std::string output_file = output_dir + output_basename + ".bin";
        std::string output_mem_file = output_dir + output_basename + ".mem";
        
        std::cout << "\n=== Processing assembly file: " << assembly_file << " ===\n";
        std::cout << "PE number: " << (pe_number == 0xFFFF ? "Unknown (using 0xFFFF)" : std::to_string(pe_number)) << std::endl;
        
        // Store memory entries for this PE
        all_memory_entries[pe_number] = std::vector<std::string>();
        
        // Assemble the file and collect memory entries
        int file_result = assembler.assemble(assembly_file, output_file, pe_number, output_mem_file, &all_memory_entries[pe_number]);
        
        if (file_result != 0) {
            std::cerr << "Error processing file: " << assembly_file << std::endl;
            result = file_result;
        }
    }
    
    // Write all memory entries to the combined file
    combined_mem_file << "// Combined memory initialization file for all PEs" << std::endl;
    combined_mem_file << "// Format: @ADDRESS HEX_INSTRUCTION" << std::endl;
    
    // Count the number of PEs
    int total_pes = 0;
    for (const auto& entry : all_memory_entries) {
        if (entry.first != 0xFFFF) {  // Don't count unknown PEs
            total_pes = std::max(total_pes, entry.first + 1);
        }
    }
    
    // Write the total number of PEs
    combined_mem_file << "// Total PEs: " << total_pes << std::endl;
    
    // Write entries for each PE in order
    for (int pe = 0; pe < total_pes; pe++) {
        if (all_memory_entries.find(pe) != all_memory_entries.end()) {
            combined_mem_file << std::endl << "// PE" << pe << " memory entries" << std::endl;
            for (const auto& entry : all_memory_entries[pe]) {
                combined_mem_file << entry << std::endl;
            }
        }
    }
    
    // Then write entries for unknown PEs (0xFFFF)
    if (all_memory_entries.find(0xFFFF) != all_memory_entries.end()) {
        combined_mem_file << std::endl << "// Unknown PE memory entries" << std::endl;
        for (const auto& entry : all_memory_entries[0xFFFF]) {
            combined_mem_file << entry << std::endl;
        }
    }
    
    combined_mem_file.close();
    file_list.close();
    
    std::cout << "\nAll files processed." << std::endl;
    std::cout << "Total PEs found: " << total_pes << std::endl;
    std::cout << "Combined memory file created: " << combined_mem_file_path << std::endl;
    
    return result;
}