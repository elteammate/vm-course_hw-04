#include <algorithm>
#include <chrono>
#include <memory>
#include <numeric>
#include <vector>
#include <bitset>
#include <cassert>
#include <cstring>
#include <unordered_map>


extern "C" {
#include <runtime.h>
#include <gc.h>
    void *__stop_custom_data;
    void *__start_custom_data;
}

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usize = uintptr_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using isize = std::ptrdiff_t;
using f32 = float;
using f64 = double;


#define kase break; case
#define otherwise break; default
#define CONCAT(a, b) a ## b

#define OP_ENUM_DEFINITION \
    X(BINOP_ADD, 0x01) \
    X(BINOP_SUB, 0x02) \
    X(BINOP_MUL, 0x03) \
    X(BINOP_DIV, 0x04) \
    X(BINOP_REM, 0x05) \
    X(BINOP_LT, 0x06) \
    X(BINOP_LE, 0x07) \
    X(BINOP_GT, 0x08) \
    X(BINOP_GE, 0x09) \
    X(BINOP_EQ, 0x0a) \
    X(BINOP_NE, 0x0b) \
    X(BINOP_AND, 0x0c) \
    X(BINOP_OR, 0x0d) \
    \
    X(CONST, 0x10) \
    X(STRING, 0x11) \
    X(SEXP, 0x12) \
    /* X(STI, 0x13) */ \
    X(STA, 0x14) \
    X(JMP, 0x15) \
    X(END, 0x16) \
    /* X(RET, 0x17) */ \
    X(DROP, 0x18) \
    X(DUP, 0x19) \
    /* X(SWAP, 0x1a) */ \
    X(ELEM, 0x1b) \
    \
    X(LD_GLOBAL, 0x20) \
    X(LD_LOCAL, 0x21) \
    X(LD_ARG, 0x22) \
    X(LD_CLOSURE, 0x23) \
    /*
    X(LDA_GLOBAL, 0x30) \
    X(LDA_LOCAL, 0x31) \
    X(LDA_ARG, 0x32) \
    X(LDA_CLOSURE, 0x33) \
    */ \
    X(ST_GLOBAL, 0x40) \
    X(ST_LOCAL, 0x41) \
    X(ST_ARG, 0x42) \
    X(ST_CLOSURE, 0x43) \
    \
    X(CJMPZ, 0x50) \
    X(CJMPNZ, 0x51) \
    X(BEGIN, 0x52) \
    X(CBEGIN, 0x53) \
    X(CLOSURE, 0x54) \
    X(CALLC, 0x55) \
    X(CALL, 0x56) \
    X(TAG, 0x57) \
    X(FAIL, 0x59) \
    X(LINE, 0x5a) \
    \
    X(PATT_STR, 0x60) \
    X(PATT_ARR, 0x58) \
    X(PATT_STRING, 0x61) \
    X(PATT_ARRAY, 0x62) \
    X(PATT_SEXP, 0x63) \
    X(PATT_REF, 0x64) \
    X(PATT_VAL, 0x65) \
    X(PATT_FUN, 0x66) \
    \
    X(BUILTIN_READ, 0x70) \
    X(BUILTIN_WRITE, 0x71) \
    X(BUILTIN_LENGTH, 0x72) \
    X(BUILTIN_STRING, 0x73) \
    X(BUILTIN_ARRAY, 0x74) \
    \
    X(STOP, 0xff)

enum class Op : u8 {
#define X(name, val) name = val,
    OP_ENUM_DEFINITION
#undef X
};

bool is_op(u8 byte) {
    switch (byte) {
#define X(name, val) case (u8)Op::name:
    OP_ENUM_DEFINITION
#undef X
        return true;
    default:
        return false;
    }
}

struct Meta {
    i32 stage;
    std::vector<std::string> errors;
    std::vector<i32> data;
};

struct PublicEntry {
    i32 name_idx;
    i32 offset;
};

struct Bytecode {
    void *buffer;
    char *strings;
    usize strings_size;
    PublicEntry *public_table;
    usize public_table_size;
    u8 *code;
    usize code_size;
    usize globals_size;
    Meta meta;

    [[nodiscard]] char *get_string(i32 offset) const {
        if (offset < 0 || offset >= strings_size) [[unlikely]] {
            throw std::runtime_error(std::format("String offset 0x{:08x} is out of bounds", offset));
        }
        return strings + offset;
    }

    [[nodiscard]] Op get_opcode(i32 offset) const {
        if (offset < 0 || offset >= code_size) [[unlikely]] {
            throw std::runtime_error(std::format("Code offset 0x{:08x} is out of bounds", offset));
        }
        if (!is_op(code[offset])) [[unlikely]] {
            throw std::runtime_error(std::format("Invalid opcode 0x{:02x} at offset 0x{:08x}", code[offset], offset));
        }
        return (Op)code[offset];
    }

    [[nodiscard]] u8 get_byte(i32 offset) const {
        if (offset < 0 || offset + 1 > code_size) [[unlikely]] {
            throw std::runtime_error(std::format("Code offset 0x{:08x} is out of bounds", offset));
        }
        return code[offset];
    }

    [[nodiscard]] i32 get_arg(i32 offset) const {
        if (offset < 0 || offset + 4 > code_size) [[unlikely]] {
            throw std::runtime_error(std::format("Code offset 0x{:08x} is out of bounds", offset));
        }
        i32 arg;
        std::memcpy(&arg, code + offset, sizeof(i32));
        return arg;
    }

    [[nodiscard]] char *get_string_arg(i32 offset) const {
        i32 arg = get_arg(offset);
        return get_string(arg);
    }
} static bc;

void bytecode_from_bytes(void *buffer, usize size) {
    bc.buffer = buffer;

    struct Header {
        u32 strings_size;
        u32 globals_size;
        u32 public_table_size;
    };

    if (size < sizeof(Header)) {
        throw std::runtime_error("Bytecode is too small");
    }

    Header header = *(Header *)buffer;

    bc.public_table = (PublicEntry *)((u8 *)buffer + sizeof(Header));
    bc.public_table_size = header.public_table_size;
    bc.strings = (char *)((u8 *)bc.public_table + sizeof(PublicEntry) * bc.public_table_size);
    bc.strings_size = header.strings_size;
    bc.code = (u8 *)bc.strings + bc.strings_size;

    if (size < (usize)(bc.code - (u8 *)buffer)) {
        throw std::runtime_error("Code section does not fit in bytecode");
    }

    bc.code_size = size - (usize)(bc.code - (u8 *)buffer);
    bc.globals_size = header.globals_size;
    bc.meta = Meta{
        .stage = 0,
    };
}

std::unique_ptr<u8 []> bytecode_from_file(const char *filename) {
    FILE *f;
    if (std::string(filename) == "-") {
        f = stdin;
    } else {
        f = std::fopen(filename, "rb");
        if (!f) {
            throw std::runtime_error("Failed to open file: " + std::string(filename));
        }
    }

    auto close_file = [](FILE *file) { if (file && file != stdin) std::fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file_guard(f, close_file);

    if (std::fseek(f, 0, SEEK_END) != 0) {
        throw std::runtime_error("Failed to seek to end of file: " + std::string(filename));
    }

    long file_size = std::ftell(f);
    if (file_size < 0) {
        throw std::runtime_error("Failed to get file size: " + std::string(filename));
    }

    if (std::fseek(f, 0, SEEK_SET) != 0) {
        throw std::runtime_error("Failed to seek to start of file: " + std::string(filename));
    }

    std::unique_ptr<u8[]> bytecode(new u8[file_size]);
    if (std::fread(bytecode.get(), file_size, 1, f) != 1) {
        throw std::runtime_error("Failed to read file: " + std::string(filename));
    }

    bytecode_from_bytes(bytecode.get(), file_size);
    return bytecode;
}

std::variant<i32, std::string> get_instruction_size(i32 offset) {
    if (offset < 0 || offset >= bc.code_size) {
        return std::format("get_instruction_size: Code offset {} out of range", offset);
    }

    u8 opcode = bc.code[offset];
    if (!is_op(opcode)) {
        return std::format("get_instruction_size: Invalid opcode {} at offset {}", opcode, offset);
    }

    switch ((Op)opcode) {
    case Op::BINOP_ADD:
    case Op::BINOP_SUB:
    case Op::BINOP_MUL:
    case Op::BINOP_DIV:
    case Op::BINOP_REM:
    case Op::BINOP_LT:
    case Op::BINOP_LE:
    case Op::BINOP_GT:
    case Op::BINOP_GE:
    case Op::BINOP_EQ:
    case Op::BINOP_NE:
    case Op::BINOP_AND:
    case Op::BINOP_OR:
        return 1;
    case Op::CONST:
    case Op::STRING:
        return 5;
    case Op::SEXP:
        return 9;
    // case Op::STI:
    case Op::STA:
        return 1;
    case Op::JMP:
        return 5;
    case Op::END:
    // case Op::RET:
    case Op::DROP:
    case Op::DUP:
    // case Op::SWAP:
    case Op::ELEM:
        return 1;
    case Op::LD_GLOBAL:
    case Op::LD_LOCAL:
    case Op::LD_ARG:
    case Op::LD_CLOSURE:
    // case Op::LDA_GLOBAL:
    // case Op::LDA_LOCAL:
    // case Op::LDA_ARG:
    // case Op::LDA_CLOSURE:
    case Op::ST_GLOBAL:
    case Op::ST_LOCAL:
    case Op::ST_ARG:
    case Op::ST_CLOSURE:
        return 5;
    case Op::CJMPZ:
    case Op::CJMPNZ:
        return 5;
    case Op::BEGIN:
    case Op::CBEGIN:
        return 9;
    case Op::CLOSURE: {
        if (offset + 9 > bc.code_size) {
            return std::format(
                "get_instruction_size: code of closure instruction must take at least 9 bytes"
            );
        }
        i32 num_captures = bc.get_arg(offset + 5);
        return 9 + 5 * num_captures;
    }
    case Op::CALLC:
        return 5;
    case Op::CALL:
        return 9;
    case Op::TAG:
        return 9;
    case Op::FAIL:
        return 9;
    case Op::LINE:
        return 5;
    case Op::PATT_ARR:
        return 5;
    case Op::PATT_STR:
    case Op::PATT_STRING:
    case Op::PATT_ARRAY:
    case Op::PATT_SEXP:
    case Op::PATT_REF:
    case Op::PATT_VAL:
    case Op::PATT_FUN:
        return 1;
    case Op::BUILTIN_READ:
    case Op::BUILTIN_WRITE:
    case Op::BUILTIN_LENGTH:
    case Op::BUILTIN_STRING:
        return 1;
    case Op::BUILTIN_ARRAY:
        return 5;
    case Op::STOP:
        return 1;
    default:
        throw std::runtime_error("Invalid opcode: this should be unreachable");
    }
}

constexpr i32 STACK_DEPTH_UNK = -1;
constexpr i32 STACK_DEPTH_CALL = -2;
constexpr i32 NUM_UNK = -1;
constexpr i32 MAX_NUM_CAPTURES = 0xFFFF;
constexpr i32 MAX_STACK_DEPTH = 0xFFFF;
constexpr i32 MAX_LOCALS = 0xFFFF;

struct SymbolicInterpState {
    i32 offset;
    i32 current_fn;
    i32 stack_depth;
};

struct OpInfo {
    i32 new_stack_depth;  // STACK_DEPTH_UNK if execution does not continue
    std::optional<SymbolicInterpState> forks_execution;
};

i32 get_depth(i32 offset) {
    return bc.meta.data[offset];
}

void set_depth(i32 offset, i32 stack_depth) {
    bc.meta.data[offset] = stack_depth;
}

i32 get_num_captures(i32 fn_offset) {
    return bc.meta.data[fn_offset + 1];
}

std::optional<std::string> set_num_captures(i32 fn_offset, i32 num_captures) {
    if (num_captures < 0 || num_captures > MAX_NUM_CAPTURES) {
        return std::format(
            "Number of captures must be between 0 and {}",
            MAX_NUM_CAPTURES
        );
    }
    auto &slot = bc.meta.data[fn_offset + 1];
    if (slot != NUM_UNK && slot != num_captures) {
        return std::format(
            "Number of captures mismatch at offset 0x{:08x}",
            fn_offset
        );
    }
    slot = num_captures;
    return std::nullopt;
}


std::string report_error(const SymbolicInterpState &state, std::string message) {
    return std::format("Error at offset 0x{:08x}: {}\n", state.offset, message)
        + std::format("Current function offset: 0x{:08x}\n", state.current_fn)
        + std::format("Stack depth: {}\n", state.stack_depth);
}

u32 pack_begin_params(i32 num_args, i32 stack_depth) {
    return (u32)num_args | (u32)stack_depth << 16;
}

struct BeginParams {
    i32 num_args;
    i32 stack_depth;
};

BeginParams unpack_begin_params(u32 packed) {
    i32 num_args = (i32)(packed & 0xFFFF);
    i32 stack_depth = (i32)(packed >> 16);
    return {num_args, stack_depth};
}

i32 get_num_locals(i32 fn) {
    i32 packed;
    std::memcpy(&packed, &bc.code[fn + 5], sizeof(packed));
    return packed;
}

i32 get_num_args(i32 fn) {
    u32 packed;
    std::memcpy(&packed, &bc.code[fn + 1], sizeof(packed));
    return unpack_begin_params(packed).num_args;
}

i32 get_max_stack_depth(i32 fn) {
    u32 packed;
    std::memcpy(&packed, &bc.code[fn + 1], sizeof(packed));
    return unpack_begin_params(packed).stack_depth;
}

void update_max_stack_depth(i32 fn, i32 stack_depth) {
    u32 packed;
    std::memcpy(&packed, &bc.code[fn + 1], sizeof(packed));
    auto unpacked = unpack_begin_params(packed);
    if (unpacked.stack_depth < stack_depth) {
        packed = pack_begin_params(unpacked.num_args, stack_depth);
        std::memcpy(&bc.code[fn + 1], &packed, sizeof(packed));
    }
}

std::optional<std::string> check_local_access(const SymbolicInterpState &state, i32 local_idx) {
    if (local_idx < 0 || local_idx >= get_num_locals(state.current_fn)) {
        return report_error(state, "Local index out of bounds");
    }
    return std::nullopt;
}

std::optional<std::string> check_arg_access(const SymbolicInterpState &state, i32 arg_idx) {
    if (arg_idx < 0 || arg_idx >= get_num_args(state.current_fn)) {
        return report_error(state, "Argument index out of bounds");
    }
    return std::nullopt;
}

std::optional<std::string> check_global_access(const SymbolicInterpState &state, i32 global_idx) {
    if (global_idx < 0 || global_idx >= bc.globals_size) {
        return report_error(state, "Global index out of bounds");
    }
    return std::nullopt;
}

std::optional<std::string> check_capture_access(const SymbolicInterpState &state, i32 capture_idx) {
    i32 num_captures = get_num_captures(state.current_fn);
    if (num_captures == 0 || num_captures == NUM_UNK) {
        return report_error(state, "Trying to access captured value outside closure");
    }
    if (capture_idx < 0 || capture_idx >= num_captures) {
        return report_error(state, "Capture index out of bounds");
    }
    return std::nullopt;
}

std::optional<std::string> check_valid_string(const SymbolicInterpState &state, i32 offset) {
    if (offset < 0 || offset >= bc.strings_size) {
        return report_error(state, "String index out of bounds");
    }
    return std::nullopt;
}

std::variant<OpInfo, std::string> get_op_info(const SymbolicInterpState &state, i32 offset) {

#define VALIDATED2_OP(op, num_consume, num_produce, validation1, validation2) \
case op: { \
    i32 _consume = (num_consume); \
    i32 _produce = (num_produce); \
    if (state.stack_depth < _consume) { \
        return report_error(state, "Stack underflow in " #op); \
    } \
    if (std::optional<std::string> err = (validation1); err.has_value()) { \
        return err.value(); \
    } \
    if (std::optional<std::string> err = (validation2); err.has_value()) { \
        return err.value(); \
    } \
    return OpInfo{ \
        .new_stack_depth = state.stack_depth - (_consume - _produce), \
        .forks_execution = std::nullopt \
    }; \
}

#define VALIDATED_OP(op, num_consume, num_produce, validation) \
VALIDATED2_OP(op, num_consume, num_produce, validation, std::nullopt)

#define SIMPLE_OP(op, num_consume, num_produce) \
VALIDATED_OP(op, num_consume, num_produce, std::nullopt)

    switch (bc.get_opcode(offset)) {
    SIMPLE_OP(Op::BINOP_ADD, 2, 1)
    SIMPLE_OP(Op::BINOP_SUB, 2, 1)
    SIMPLE_OP(Op::BINOP_MUL, 2, 1)
    SIMPLE_OP(Op::BINOP_DIV, 2, 1)
    SIMPLE_OP(Op::BINOP_REM, 2, 1)
    SIMPLE_OP(Op::BINOP_LT, 2, 1)
    SIMPLE_OP(Op::BINOP_GT, 2, 1)
    SIMPLE_OP(Op::BINOP_LE, 2, 1)
    SIMPLE_OP(Op::BINOP_GE, 2, 1)
    SIMPLE_OP(Op::BINOP_EQ, 2, 1)
    SIMPLE_OP(Op::BINOP_NE, 2, 1)
    SIMPLE_OP(Op::BINOP_AND, 2, 1)
    SIMPLE_OP(Op::BINOP_OR, 2, 1)

    SIMPLE_OP(Op::CONST, 0, 1)
    VALIDATED_OP(Op::STRING, 0, 1, check_valid_string(state, bc.get_arg(offset + 1)))
    VALIDATED_OP(Op::SEXP, bc.get_arg(offset + 5), 1, check_valid_string(state, bc.get_arg(offset + 1)))
    SIMPLE_OP(Op::STA, 3, 1)

    case Op::JMP: {
        i32 target = bc.get_arg(offset + 1);
        if (target < 0 || target >= bc.code_size) {
            return report_error(state, "Invalid target offset");
        }
        return OpInfo{
            .new_stack_depth = STACK_DEPTH_UNK,
            .forks_execution = SymbolicInterpState{
                .offset = target,
                .current_fn = state.current_fn,
                .stack_depth = state.stack_depth,
            },
        };
    }

    case Op::END:
        if (state.stack_depth != 1) {
            return report_error(state, "Stack depth is not 1 at END instruction");
        }
        return OpInfo{
            .new_stack_depth = STACK_DEPTH_UNK,
            .forks_execution = std::nullopt,
        };

    case Op::STOP:
    case Op::FAIL:
        return OpInfo{
            .new_stack_depth = STACK_DEPTH_UNK,
            .forks_execution = std::nullopt,
        };

    SIMPLE_OP(Op::DROP, 1, 0)
    SIMPLE_OP(Op::DUP, 1, 2)
    SIMPLE_OP(Op::ELEM, 2, 1)

    VALIDATED_OP(Op::LD_GLOBAL, 0, 1, check_global_access(state, bc.get_arg(offset + 1)))
    VALIDATED_OP(Op::LD_LOCAL, 0, 1, check_local_access(state, bc.get_arg(offset + 1)))
    VALIDATED_OP(Op::LD_ARG, 0, 1, check_arg_access(state, bc.get_arg(offset + 1)))
    VALIDATED_OP(Op::LD_CLOSURE, 0, 1, check_capture_access(state, bc.get_arg(offset + 1)))

    VALIDATED_OP(Op::ST_GLOBAL, 1, 1, check_global_access(state, bc.get_arg(offset + 1)))
    VALIDATED_OP(Op::ST_LOCAL, 1, 1, check_local_access(state, bc.get_arg(offset + 1)))
    VALIDATED_OP(Op::ST_ARG, 1, 1, check_arg_access(state, bc.get_arg(offset + 1)))
    VALIDATED_OP(Op::ST_CLOSURE, 1, 1, check_capture_access(state, bc.get_arg(offset + 1)))

    case Op::CJMPZ:
    case Op::CJMPNZ: {
        i32 target = bc.get_arg(offset + 1);
        if (target < 0 || target >= bc.code_size) {
            return report_error(state, "Invalid target offset");
        }
        if (state.stack_depth == 0) {
            return report_error(state, "Stack underflow");
        }
        return OpInfo{
            .new_stack_depth = state.stack_depth - 1,
            .forks_execution = SymbolicInterpState{
                .offset = target,
                .current_fn = state.current_fn,
                .stack_depth = state.stack_depth - 1,
            },
        };
    }

    SIMPLE_OP(Op::BEGIN, 0, 0)
    SIMPLE_OP(Op::CBEGIN, 0, 0)

    case Op::CLOSURE: {
        i32 fn = bc.get_arg(offset + 1);
        if (fn < 0 || fn >= bc.code_size) {
            return report_error(state, "Invalid function offset");
        }
        u8 first_opcode = bc.get_byte(fn);
        if (first_opcode != (u8)Op::BEGIN && first_opcode != (u8)Op::CBEGIN) {
            return report_error(state, "Function must start with BEGIN or CBEGIN");
        }
        i32 num_captures = bc.get_arg(offset + 5);
        if (num_captures < 0 || num_captures > MAX_NUM_CAPTURES) {
            return report_error(state, "Invalid number of captures");
        }
        if (auto err = set_num_captures(fn, num_captures); err.has_value()) {
            return *err;
        }
        update_max_stack_depth(state.current_fn, state.stack_depth + num_captures + 1);
        for (i32 i = 0, designator_offset = offset + 9; i < num_captures; i++, designator_offset += 5) {
            i32 addr = bc.get_arg(designator_offset + 1);
            if (u8 designator = bc.get_byte(designator_offset); designator == 0) {
                if (auto err = check_global_access(state, addr); err.has_value()) {
                    return *err;
                }
            } else if (designator == 1) {
                if (auto err = check_local_access(state, addr); err.has_value()) {
                    return *err;
                }
            } else if (designator == 2) {
                if (auto err = check_arg_access(state, addr); err.has_value()) {
                    return *err;
                }
            } else if (designator == 3) {
                if (auto err = check_capture_access(state, addr); err.has_value()) {
                    return *err;
                }
            } else {
                return report_error(state, "Invalid capture designator");
            }
        }
        return OpInfo{
            .new_stack_depth = state.stack_depth + 1,
            .forks_execution = SymbolicInterpState{
                .offset = fn,
                .current_fn = fn,
                .stack_depth = STACK_DEPTH_CALL,
            },
        };
    }
    case Op::CALLC: {
        i32 num_args = bc.get_arg(offset + 1);
        if (num_args > MAX_LOCALS) {
            return report_error(state, "Too many arguments");
        }
        if (state.stack_depth < num_args + 1) {
            return report_error(state, "Stack underflow");
        }
        update_max_stack_depth(state.current_fn, state.stack_depth - num_args + 2);
        return OpInfo{
            .new_stack_depth = state.stack_depth - num_args,
            .forks_execution = std::nullopt,
        };
    }
    case Op::CALL: {
        i32 fn = bc.get_arg(offset + 1);
        if (fn < 0 || fn >= bc.code_size) {
            return report_error(state, "Invalid function offset");
        }
        i32 num_args = bc.get_arg(offset + 5);
        if (state.stack_depth < num_args) {
            return report_error(state, "Stack underflow");
        }
        if (num_args > MAX_LOCALS) {
            return report_error(state, "Too many arguments");
        }
        if (num_args != get_num_args(fn)) {
            return report_error(state, "Number of arguments mismatch");
        }
        u8 first_opcode = bc.get_byte(fn);
        if (first_opcode != (u8)Op::BEGIN) {
            return report_error(state, "Function must start with BEGIN");
        }
        return OpInfo{
            .new_stack_depth = state.stack_depth - num_args + 1,
            .forks_execution = SymbolicInterpState{
                .offset = fn,
                .current_fn = fn,
                .stack_depth = STACK_DEPTH_CALL,
            },
        };
    }

    VALIDATED_OP(Op::TAG, 1, 1, check_valid_string(state, bc.get_arg(offset + 1)))
    SIMPLE_OP(Op::LINE, 0, 0)

    SIMPLE_OP(Op::PATT_STR, 2, 1)
    SIMPLE_OP(Op::PATT_STRING, 1, 1)
    SIMPLE_OP(Op::PATT_ARRAY, 1, 1)
    SIMPLE_OP(Op::PATT_ARR, 1, 1)
    SIMPLE_OP(Op::PATT_SEXP, 1, 1)
    SIMPLE_OP(Op::PATT_REF, 1, 1)
    SIMPLE_OP(Op::PATT_VAL, 1, 1)
    SIMPLE_OP(Op::PATT_FUN, 1, 1)

    SIMPLE_OP(Op::BUILTIN_READ, 0, 1)
    SIMPLE_OP(Op::BUILTIN_WRITE, 1, 1)
    SIMPLE_OP(Op::BUILTIN_LENGTH, 1, 1)
    SIMPLE_OP(Op::BUILTIN_STRING, 1, 1)
    SIMPLE_OP(Op::BUILTIN_ARRAY, bc.get_arg(offset + 1), 1)

    default:
        return report_error(state, "Invalid opcode");
    }
}

void analyze_bytecode_stage1(const std::optional<std::string> &entrypoint) {
    bc.meta = Meta{};

    if (bc.code_size == 0) {
        bc.meta.errors.emplace_back("Code section must not be empty");
        return;
    }
    if (auto code = bc.code; code[bc.code_size - 1] != 0xff) {
        bc.meta.errors.emplace_back("Code section must end with 0xff byte");
    }

    bc.meta.data.assign(bc.code_size, -1);

    std::vector<SymbolicInterpState> queue;
    queue.reserve(32);
    for (i32 i = 0; i < bc.public_table_size; i++) {
        auto entry = bc.public_table[i];
        i32 fn = entry.offset;

        if (entrypoint.has_value() && bc.get_string(entry.name_idx) != entrypoint) {
            continue;
        }

        if (fn < 0 || fn >= bc.code_size) {
            bc.meta.errors.emplace_back(std::format(
                "Function {} points outside of code segment",
                bc.get_string(bc.public_table[i].name_idx)
            ));
        } else {
            queue.push_back(SymbolicInterpState{
                .offset = fn,
                .current_fn = fn,
                .stack_depth = STACK_DEPTH_CALL,
            });
        }
    }

    while (!queue.empty()) {
        auto state = queue.back();
        queue.pop_back();

        i32 offset = state.offset;

        while (true) {
            if (bc.meta.errors.size() > 128) {
                return;
            }

            if (get_depth(offset) != NUM_UNK) {
                if (get_depth(offset) != state.stack_depth) {
                    bc.meta.errors.emplace_back(std::format(
                        "Stack depth mismatch at offset 0x{:08x}",
                        offset
                    ));
                }
                break;
            }

            set_depth(offset, state.stack_depth);
            update_max_stack_depth(state.current_fn, state.stack_depth);

            if (state.stack_depth > MAX_STACK_DEPTH) {
                bc.meta.errors.emplace_back(std::format(
                    "Maximum stack depth exceeded at offset 0x{:08x}",
                    offset
                ));
                break;
            }

            auto size_result = get_instruction_size(offset);
            if (std::holds_alternative<std::string>(size_result)) {
                bc.meta.errors.emplace_back(std::get<std::string>(size_result));
                break;
            }
            auto size = std::get<i32>(size_result);

            Op op = bc.get_opcode(offset);

            if (state.stack_depth == STACK_DEPTH_CALL) {
                if (op == Op::BEGIN || op == Op::CBEGIN) {
                    state.stack_depth = 0;
                } else {
                    bc.meta.errors.emplace_back("Function must start with BEGIN or CBEGIN instruction");
                    break;
                }
            }

            auto op_info_result = get_op_info(state, offset);
            if (std::holds_alternative<std::string>(op_info_result)) {
                bc.meta.errors.emplace_back(std::get<std::string>(op_info_result));
                break;
            }
            auto op_info = std::get<OpInfo>(op_info_result);

            if (op_info.forks_execution.has_value()) {
                queue.push_back(op_info.forks_execution.value());
            }
            offset += size;
            state.offset = offset;
            if (offset >= bc.code_size) {
                break;
            }
            if (op_info.new_stack_depth == STACK_DEPTH_UNK) {
                break;
            }
            state.stack_depth = op_info.new_stack_depth;
        }
    }

    bc.meta.stage = 1;
}

void analyze_bytecode(const std::optional<std::string> &entrypoint) {
    analyze_bytecode_stage1(entrypoint);
}

void dump_instruction(FILE *f, i32 offset) {
    // if (bc.meta.stage < 1) {
    //     throw std::runtime_error("Bytecode must be analyzed before formatting");
    // }
    Op op = bc.get_opcode(offset);
    i32 arg1_offset = offset + 1;
    i32 arg2_offset = offset + 5;
    switch (op) {
    case Op::BINOP_ADD: std::fprintf(f, "BINOP\t+");
    kase Op::BINOP_SUB: std::fprintf(f, "BINOP\t-");
    kase Op::BINOP_MUL: std::fprintf(f, "BINOP\t*");
    kase Op::BINOP_DIV: std::fprintf(f, "BINOP\t/");
    kase Op::BINOP_REM: std::fprintf(f, "BINOP\t%%");
    kase Op::BINOP_LT: std::fprintf(f, "BINOP\t<");
    kase Op::BINOP_GT: std::fprintf(f, "BINOP\t>");
    kase Op::BINOP_LE: std::fprintf(f, "BINOP\t<=");
    kase Op::BINOP_GE: std::fprintf(f, "BINOP\t>=");
    kase Op::BINOP_EQ: std::fprintf(f, "BINOP\t==");
    kase Op::BINOP_NE: std::fprintf(f, "BINOP\t!=");
    kase Op::BINOP_AND: std::fprintf(f, "BINOP\t&&");
    kase Op::BINOP_OR: std::fprintf(f, "BINOP\t!!");
    kase Op::CONST: std::fprintf(f, "CONST\t%d", bc.get_arg(arg1_offset));
    kase Op::STRING: std::fprintf(f, "STRING\t%s", bc.get_string_arg(arg1_offset));
    kase Op::SEXP: std::fprintf(f, "SEXP\t%s %d", bc.get_string_arg(arg1_offset), bc.get_arg(arg2_offset));
    // kase Op::STI: std::fprintf(f, "STI");
    kase Op::STA: std::fprintf(f, "STA");
    kase Op::JMP: std::fprintf(f, "JMP\t0x%.8x", bc.get_arg(arg1_offset));
    kase Op::END: std::fprintf(f, "END");
    // kase Op::RET: std::fprintf(f, "RET");
    kase Op::DROP: std::fprintf(f, "DROP");
    kase Op::DUP: std::fprintf(f, "DUP");
    // kase Op::SWAP: std::fprintf(f, "SWAP");
    kase Op::ELEM: std::fprintf(f, "ELEM");
    kase Op::LD_GLOBAL: std::fprintf(f, "LD\tG(%d)", bc.get_arg(arg1_offset));
    kase Op::LD_LOCAL: std::fprintf(f, "LD\tL(%d)", bc.get_arg(arg1_offset));
    kase Op::LD_ARG: std::fprintf(f, "LD\tA(%d)", bc.get_arg(arg1_offset));
    kase Op::LD_CLOSURE: std::fprintf(f, "LD\tC(%d)", bc.get_arg(arg1_offset));
    // kase Op::LDA_GLOBAL: std::fprintf(f, "LDA\tG(%d)", bc.get_arg(arg1_offset));
    // kase Op::LDA_LOCAL: std::fprintf(f, "LDA\tL(%d)", bc.get_arg(arg1_offset));
    // kase Op::LDA_ARG: std::fprintf(f, "LDA\tA(%d)", bc.get_arg(arg1_offset));
    // kase Op::LDA_CLOSURE: std::fprintf(f, "LDA\tC(%d)", bc.get_arg(arg1_offset));
    kase Op::ST_GLOBAL: std::fprintf(f, "ST\tG(%d)", bc.get_arg(arg1_offset));
    kase Op::ST_LOCAL: std::fprintf(f, "ST\tL(%d)", bc.get_arg(arg1_offset));
    kase Op::ST_ARG: std::fprintf(f, "ST\tA(%d)", bc.get_arg(arg1_offset));
    kase Op::ST_CLOSURE: std::fprintf(f, "ST\tC(%d)", bc.get_arg(arg1_offset));
    kase Op::CJMPZ: std::fprintf(f, "CJMPz\t0x%.8x", bc.get_arg(arg1_offset));
    kase Op::CJMPNZ: std::fprintf(f, "CJMPnz\t0x%.8x", bc.get_arg(arg1_offset));
    kase Op::BEGIN: {
        i32 args = bc.get_arg(arg1_offset);
        i32 locals = bc.get_arg(arg2_offset);
        std::fprintf(f, "BEGIN\t%d %d", args, locals);
    }
    kase Op::CBEGIN: {
        i32 args = bc.get_arg(arg1_offset);
        i32 locals = bc.get_arg(arg2_offset);
        std::fprintf(f, "CBEGIN\t%d %d", args, locals);
    }
    kase Op::CLOSURE: {
        std::fprintf(f, "CLOSURE\t0x%.8x", bc.get_arg(arg1_offset));
        i32 num_captures = bc.get_arg(arg2_offset);
        for (i32 i = 0, designator_offset = offset + 9; i < num_captures; i++, designator_offset += 5) {
            if (u8 designator = bc.get_byte(designator_offset); designator == 0) {
                std::fprintf(f, "G(%d)", bc.get_arg(designator_offset + 1));
            } else if (designator == 1) {
                std::fprintf(f, "L(%d)", bc.get_arg(designator_offset + 1));
            } else if (designator == 2) {
                std::fprintf(f, "A(%d)", bc.get_arg(designator_offset + 1));
            } else if (designator == 3) {
                std::fprintf(f, "C(%d)", bc.get_arg(designator_offset + 1));
            } else {
                throw std::runtime_error(std::format("Invalid capture designator %d", num_captures));
            }
        }
    }
    kase Op::CALLC: std::fprintf(f, "CALLC\t%d", bc.get_arg(arg1_offset));
    kase Op::CALL: {
        i32 fn = bc.get_arg(arg1_offset);
        i32 num_args = bc.get_arg(arg2_offset);
        std::fprintf(f, "CALL\t0x%.8x %d", fn, num_args);
    }
    kase Op::TAG: {
        char *s = bc.get_string_arg(arg1_offset);
        i32 num = bc.get_arg(arg2_offset);
        std::fprintf(f, "TAG\t%s %d", s, num);
    }
    kase Op::FAIL: {
        i32 line = bc.get_arg(arg1_offset);
        i32 column = bc.get_arg(arg2_offset);
        std::fprintf(f, "FAIL\t%d %d", line, column);
    }
    kase Op::LINE: std::fprintf(f, "LINE\t%d", bc.get_arg(arg1_offset));
    kase Op::PATT_STR: std::fprintf(f, "PATT\t=str");
    kase Op::PATT_STRING: std::fprintf(f, "PATT\t#string");
    kase Op::PATT_ARRAY: std::fprintf(f, "PATT\t#array");
    kase Op::PATT_ARR: std::fprintf(f, "ARRAY\t%d", bc.get_arg(arg1_offset));
    kase Op::PATT_SEXP: std::fprintf(f, "PATT\t#sexp");
    kase Op::PATT_REF: std::fprintf(f, "PATT\t#ref");
    kase Op::PATT_VAL: std::fprintf(f, "PATT\t#val");
    kase Op::PATT_FUN: std::fprintf(f, "PATT\t#fun");
    kase Op::BUILTIN_READ: std::fprintf(f, "CALL\tLread");
    kase Op::BUILTIN_WRITE: std::fprintf(f, "CALL\tLwrite");
    kase Op::BUILTIN_LENGTH: std::fprintf(f, "CALL\tLlength");
    kase Op::BUILTIN_STRING: std::fprintf(f, "CALL\tLstring");
    kase Op::BUILTIN_ARRAY: std::fprintf(f, "CALL\tBarray\t%d", bc.get_arg(arg1_offset));
    kase Op::STOP: std::fprintf(f, "<end>");
    }
}

void dump_bytecode(FILE *f) {
    std::fprintf(f, "String table size       : %lu\n", bc.strings_size);
    std::fprintf(f, "Global area size        : %lu\n", bc.globals_size);
    std::fprintf(f, "Number of public symbols: %lu\n", bc.public_table_size);
    std::fprintf(f, "Public symbols          :\n");

    for (usize i = 0; i < bc.public_table_size; i++) {
        auto [name_offset, offset] = bc.public_table[i];
        std::fprintf(f, "   0x%.8x: %s\n", offset, bc.get_string(name_offset));
    }
    std::fprintf(f, "Code:\n");

    for (i32 offset = 0; offset < bc.code_size;) {
        std::fprintf(f, "0x%.8x:\t", offset);
        dump_instruction(f, offset);
        if (!bc.meta.data.empty()) {
            std::fprintf(f, "\t[%d]", get_depth(offset));
        }
        std::fprintf(f, "\n");
        offset += std::get<i32>(get_instruction_size(offset));
    }
}

union Value {
    aint number;
    void *ptr;
};

struct ReturnStackEntry {
    u32 ip : 31;
    bool pop_cc : 1;
    u32 fp;
    u32 lp;
    u32 bp;
};

struct VmConfig {
    std::string entrypoint;
    std::string filename;
    usize stack_size;
    usize rstack_size;
};

enum class ExecutionError {
    END,
    STACK_OVERFLOW,
    STACK_UNDERFLOW,
    RETURN_STACK_OVERFLOW,
    INTEGER_EXPECTED,
    NAME_NOT_FOUND,
    LOCAL_SLOT_OUT_OF_BOUNDS,
    ARITHMETIC_ERROR,
    ARG_SLOT_OUT_OF_BOUNDS,
    POINTER_EXPECTED,
    TOO_MANY_ARGUMENTS,
    STACK_INCONSISTENT,
    NOT_IN_CLOSURE,
    MALFORMED_INSTRUCTION,
};

struct Vm {
    u32 ip; // instruction pointer
    Value *sp; // stack pointer
    u32 rsp; // return stack pointer
    Value *fp; // frame pointer (arguments)
    Value *lp; // local variables pointer (locals)
    Value *bp; // base pointer (temporaries)
    void *cc; // current closure register
    Value *stack_top;
    u32 rstack_size;
    u32 globals_size;
    u8 *code;
    std::unique_ptr<Value []> stack;
    std::unique_ptr<ReturnStackEntry []> rstack;
    std::unordered_map<std::string, i32> global_functions;
    std::string filename;
};

Vm vm;

void dump_stacks() {
    std::printf("====================\n");
    for (usize i = 0; i < vm.rsp; i++) {
        std::printf("%3zu rp=%08x fp=%08x lp=%08x\n", i, vm.rstack[i].ip, vm.rstack[i].fp, vm.rstack[i].lp);
    }
    aint cc = (aint)vm.cc;
    std::printf("closure=%s\n", (char *)Lstring(&cc));
    std::printf("====================\n");
    for (usize i = 0; i < (usize)(vm.sp - vm.stack.get()); i++) {
        if (i == vm.globals_size && i > 0) {
            std::printf("^^^ GLOBALS ^^^\n");
        }
        std::printf("%3zu %016lx %s", i, vm.stack[i].number, (char *)Lstring(&vm.stack[i].number));
        if (vm.stack.get() + i == vm.fp) std::printf(" <fp");
        if (vm.stack.get() + i == vm.lp) std::printf(" <lp");
        if (vm.stack.get() + i == vm.bp) std::printf(" <bp");
        std::printf("\n");
    }
}

ExecutionError vm_continue(bool trace = false) {
#define ESCAPE(varname) CONCAT(varname, _VAL)

#define POP(varname) \
    assert(vm.sp > vm.bp); \
    auto varname = *--vm.sp;

#define PEEK(varname) \
    assert(vm.sp > vm.bp); \
    auto varname = *(vm.sp - 1);

#define POP2(varname, varname2) \
    vm.sp--; \
    assert(vm.sp > vm.bp); \
    auto varname2 = *vm.sp--; \
    auto varname = *vm.sp;

#define POP_NUM(varname) \
    POP(ESCAPE(varname)); \
    if (!UNBOXED(ESCAPE(varname).number)) return ExecutionError::INTEGER_EXPECTED; \
    aint varname = UNBOX(ESCAPE(varname).number)

#define POP2_NUM(varname, varname2) \
    POP2(ESCAPE(varname), ESCAPE(varname2)); \
    if (!UNBOXED(ESCAPE(varname2).number)) return ExecutionError::INTEGER_EXPECTED; \
    if (!UNBOXED(ESCAPE(varname).number)) return ExecutionError::INTEGER_EXPECTED; \
    aint varname = UNBOX(ESCAPE(varname).number); \
    aint varname2 = UNBOX(ESCAPE(varname2).number)

#define POP_PTR(varname) \
    POP(ESCAPE(varname)); \
    if (UNBOXED(ESCAPE(varname).number)) return ExecutionError::POINTER_EXPECTED; \
    void *varname = ESCAPE(varname).ptr

#define PUSH(expr) { \
    assert(vm.sp < vm.stack_top); \
    auto __res = (expr); \
    *vm.sp++ = __res; \
    }

#define PUSH_PTR(expr) PUSH(Value { .ptr = (expr) })
#define PUSH_NUM_ASSUME_BOXED(expr) PUSH(Value { .number = expr })
#define PUSH_NUM(expr) PUSH_NUM_ASSUME_BOXED(BOX(expr))

#define FETCH_OP() (Op)vm.code[vm.ip++]
#define FETCH_VALUE(ty, varname) \
    ty varname; \
    std::memcpy(&varname, vm.code + vm.ip, sizeof(varname)); \
    vm.ip += sizeof(varname)

    while (true) {
        if (trace) {
            dump_stacks();
            printf("%08x:\t", vm.ip);
            dump_instruction(stdout, vm.ip);
            printf("\n\n");
            fflush(stdout);
        }
        switch (FETCH_OP()) {
        kase Op::BINOP_ADD: {
            POP2_NUM(a, b);
            PUSH_NUM(a + b);
        }
        kase Op::BINOP_SUB: {
            POP2_NUM(a, b);
            PUSH_NUM(a - b);
        }
        kase Op::BINOP_MUL: {
            POP2_NUM(a, b);
            PUSH_NUM(a * b);
        }
        kase Op::BINOP_DIV: {
            POP2_NUM(a, b);
            if (b == 0) return ExecutionError::ARITHMETIC_ERROR;
            PUSH_NUM(a / b);
        }
        kase Op::BINOP_REM: {
            POP2_NUM(a, b);
            if (b == 0) return ExecutionError::ARITHMETIC_ERROR;
            PUSH_NUM(a % b);
        }
        kase Op::BINOP_LT: {
            POP2_NUM(a, b);
            PUSH_NUM((aint)(a < b));
        }
        kase Op::BINOP_GT: {
            POP2_NUM(a, b);
            PUSH_NUM((aint)(a > b));
        }
        kase Op::BINOP_LE: {
            POP2_NUM(a, b);
            PUSH_NUM((aint)(a <= b));
        }
        kase Op::BINOP_GE: {
            POP2_NUM(a, b);
            PUSH_NUM((aint)(a >= b));
        }
        kase Op::BINOP_EQ: {
            POP2(a, b);
            PUSH_NUM((aint)(a.number == b.number));
        }
        kase Op::BINOP_NE: {
            POP2(a, b);
            PUSH_NUM((aint)(a.number != b.number));
        }
        kase Op::BINOP_AND: {
            POP2_NUM(a, b);
            PUSH_NUM((aint)(a && b));
        }
        kase Op::BINOP_OR: {
            POP2_NUM(a, b);
            PUSH_NUM((aint)(a || b));
        }
        kase Op::CONST: {
            FETCH_VALUE(i32, x);
            PUSH_NUM_ASSUME_BOXED(BOX(x));
        }
        kase Op::STRING: {
            FETCH_VALUE(i32, s);
            auto ptr = Value { .ptr = bc.get_string(s) };
            auto x = Bstring(&ptr.number);
            PUSH_PTR(x);
        }
        kase Op::JMP: {
            FETCH_VALUE(u32, offset);
            vm.ip = offset;
        }
        kase Op::CJMPZ: {
            POP(cond);
            FETCH_VALUE(u32, offset);
            if (cond.number == BOX(0)) {
                vm.ip = offset;
            }
        }
        kase Op::CJMPNZ: {
            POP(cond);
            FETCH_VALUE(u32, offset);
            if (cond.number != BOX(0)) {
                vm.ip = offset;
            }
        }
        kase Op::CBEGIN:
        case Op::BEGIN: {
            FETCH_VALUE(u32, packed);
            FETCH_VALUE(u32, locals);
            u32 temps = packed >> 16;
            u32 args = packed & 0xFFFF;
            if (vm.sp + locals + temps > vm.stack_top) {
                return ExecutionError::STACK_OVERFLOW;
            }
            vm.fp = vm.sp - args;
            vm.lp = vm.sp;
            for (u32 i = 0; i < locals; ++i) {
                vm.sp->number = BOX(0);
                vm.sp++;
            }
            vm.bp = vm.sp;
        }
        kase Op::END: {
            POP(ret);
            auto entry = vm.rstack[--vm.rsp];
            vm.sp = vm.fp;
            vm.ip = entry.ip;
            vm.fp = vm.stack.get() + entry.fp;
            vm.lp = vm.stack.get() + entry.lp;
            vm.bp = vm.stack.get() + entry.bp;
            if (vm.rsp == 0) [[unlikely]] {
                return ExecutionError::END;
            }
            if (entry.pop_cc) {
                POP_PTR(cc);
                vm.cc = cc;
            }
            PUSH(ret);
        }
        kase Op::DROP: {
            POP(_);
        }
        kase Op::DUP: {
            PEEK(x);
            PUSH(x);
        }
        kase Op::ELEM: {
            POP(i);
            POP_PTR(x);
            PUSH_PTR(Belem(x, i.number));
        }
        kase Op::STA: {
            POP(v);
            POP(i);
            POP_PTR(x);
            PUSH_PTR(Bsta(x, i.number, v.ptr));
        }
        kase Op::LD_GLOBAL: {
            FETCH_VALUE(u32, offset);
            PUSH(vm.stack[offset]);
        }
        kase Op::LD_LOCAL: {
            FETCH_VALUE(u32, offset);
            assert(vm.lp + offset < vm.bp);
            PUSH(vm.lp[offset]);
        }
        kase Op::LD_ARG: {
            FETCH_VALUE(u32, offset);
            assert(vm.fp + offset < vm.lp);
            PUSH(vm.fp[offset]);
        }
        kase Op::LD_CLOSURE: {
            assert(vm.cc != nullptr);
            FETCH_VALUE(u32, offset);
            PUSH_PTR(Belem(vm.cc, BOX((aint)offset + 1)));
        }
        kase Op::ST_GLOBAL: {
            PEEK(x);
            FETCH_VALUE(u32, offset);
            vm.stack[offset] = x;
        }
        kase Op::ST_LOCAL: {
            PEEK(x);
            FETCH_VALUE(u32, offset);
            assert(vm.lp + offset < vm.bp);
            vm.lp[offset] = x;
        }
        kase Op::ST_ARG: {
            PEEK(x);
            FETCH_VALUE(u32, offset);
            assert(vm.fp + offset < vm.lp);
            vm.fp[offset] = x;
        }
        kase Op::ST_CLOSURE: {
            assert(vm.cc != nullptr);
            POP(x);
            FETCH_VALUE(u32, offset);
            PUSH_PTR(Bsta(vm.cc, BOX((aint)offset + 1), x.ptr));
        }
        kase Op::CALL: {
            FETCH_VALUE(u32, offset);
            FETCH_VALUE(u32, num_args);
            if (vm.rsp >= vm.rstack_size) return ExecutionError::STACK_OVERFLOW;
            auto &entry = vm.rstack[vm.rsp++];
            entry.ip = vm.ip;
            entry.pop_cc = false;
            entry.fp = (u32)(vm.fp - vm.stack.get());
            entry.lp = (u32)(vm.lp - vm.stack.get());
            entry.bp = (u32)(vm.bp - vm.stack.get());
            vm.ip = offset;
        }
        kase Op::BUILTIN_READ: {
            PUSH_NUM_ASSUME_BOXED(Lread());
        }
        kase Op::BUILTIN_WRITE: {
            POP(x);
            Lwrite(x.number);
            PUSH_NUM(0);
        }
        kase Op::BUILTIN_LENGTH: {
            POP_PTR(x);
            PUSH_NUM_ASSUME_BOXED(Llength(x));
        }
        kase Op::BUILTIN_ARRAY: {
            FETCH_VALUE(u32, num_args);
            assert(vm.sp - vm.bp >= num_args);
            vm.sp -= num_args;
            PUSH_PTR(Barray(&vm.sp->number, BOX(num_args)));
        }
        kase Op::BUILTIN_STRING: {
            POP(x);
            PUSH_PTR(Lstring(&x.number));
        }
        kase Op::SEXP: {
            FETCH_VALUE(i32, tag_idx);
            char *tag = bc.get_string(tag_idx);
            FETCH_VALUE(u32, num_args);
            assert(vm.sp - vm.bp >= num_args);
            aint tag_hash = LtagHash(tag);
            vm.sp->number = tag_hash;

            vm.sp -= num_args;
            PUSH_PTR(Bsexp(&vm.sp->number, BOX(num_args + 1)));
        }
        kase Op::TAG: {
            FETCH_VALUE(i32, tag_idx);
            char *tag = bc.get_string(tag_idx);
            FETCH_VALUE(u32, num_args);
            aint tag_hash = LtagHash(tag);
            POP(x);
            PUSH_NUM_ASSUME_BOXED(Btag(x.ptr, tag_hash, BOX(num_args)));
        }
        kase Op::FAIL: {
            FETCH_VALUE(u32, line);
            FETCH_VALUE(u32, column);
            Bmatch_failure(vm.stack.get(), vm.filename.c_str(), line, column);
        }
        kase Op::CLOSURE: {
            FETCH_VALUE(u32, fn);
            PUSH_NUM(fn);
            FETCH_VALUE(u32, num_args);
            for (u32 i = 0; i < num_args; i++) {
                FETCH_VALUE(u8, designator);
                FETCH_VALUE(u32, idx);
                if (designator == 0) {
                    PUSH(vm.stack[idx]);
                } else if (designator == 1) {
                    PUSH(vm.lp[idx]);
                } else if (designator == 2) {
                    PUSH(vm.fp[idx]);
                } else {
                    PUSH_PTR(Belem(vm.cc, BOX((aint)idx + 1)));
                }
            }
            auto ptr = Bclosure(&vm.sp->number - (num_args + 1), BOX(num_args));
            vm.sp -= num_args + 1;
            PUSH_PTR(ptr);
        }
        kase Op::CALLC: {
            FETCH_VALUE(u32, num_args);
            assert(vm.sp - vm.bp >= num_args + 1);
            void *&cc = (vm.sp - num_args - 1)->ptr;
            i32 offset = UNBOX(((aint *)cc)[0]);
            if (vm.rsp >= vm.rstack_size) return ExecutionError::STACK_OVERFLOW;
            auto &entry = vm.rstack[vm.rsp++];
            entry.ip = vm.ip;
            entry.pop_cc = true;
            entry.fp = (u32)(vm.fp - vm.stack.get());
            entry.lp = (u32)(vm.lp - vm.stack.get());
            entry.bp = (u32)(vm.bp - vm.stack.get());
            std::swap(vm.cc, cc);
            vm.ip = offset;
        }
        kase Op::PATT_FUN: {
            POP(x);
            PUSH_NUM_ASSUME_BOXED(Bclosure_tag_patt(x.ptr));
        }
        kase Op::PATT_SEXP: {
            POP(x);
            PUSH_NUM_ASSUME_BOXED(Bsexp_tag_patt(x.ptr));
        }
        kase Op::PATT_STRING: {
            POP(x);
            PUSH_NUM_ASSUME_BOXED(Bstring_tag_patt(x.ptr));
        }
        kase Op::PATT_ARRAY: {
            POP(x);
            PUSH_NUM_ASSUME_BOXED(Barray_tag_patt(x.ptr));
        }
        kase Op::PATT_REF: {
            POP(x);
            PUSH_NUM_ASSUME_BOXED(Bboxed_patt(x.ptr));
        }
        kase Op::PATT_VAL: {
            POP(x);
            PUSH_NUM_ASSUME_BOXED(Bunboxed_patt(x.ptr));
        }
        kase Op::PATT_STR: {
            POP2(x, y);
            PUSH_NUM_ASSUME_BOXED(Bstring_patt(x.ptr, y.ptr));
        }
        kase Op::PATT_ARR: {
            POP(x);
            FETCH_VALUE(u32, n);
            PUSH_NUM_ASSUME_BOXED(Barray_patt(x.ptr, BOX((aint)n)));
        }
        kase Op::LINE: {
            FETCH_VALUE(u32, n);
        }
        otherwise:
            return ExecutionError::MALFORMED_INSTRUCTION;
        }
    }


#undef ESCAPE
#undef POP
#undef POP2
#undef POP_NUM
#undef POP2_NUM
#undef PUSH_RAW
#undef PUSH_PTR
#undef PUSH_NUM_ASSUME_BOXED
#undef PUSH_NUM
#undef FETCH_OP
#undef FETCH_VALUE
}


ExecutionError vm_run(const std::string &fn_name, const std::vector<Value> &args, bool trace = false) {
    if (!vm.global_functions.contains(fn_name)) {
        return ExecutionError::NAME_NOT_FOUND;
    }
    u32 offset = vm.global_functions.at(fn_name);
    vm.ip = offset;
    vm.rsp = 1;
    vm.sp = vm.stack.get() + vm.globals_size;
    vm.lp = vm.sp;
    vm.fp = vm.sp;
    vm.rstack[0] = {
        .ip = 0,
        .fp = vm.globals_size,
        .lp = vm.globals_size,
        .bp = vm.globals_size,
    };
    for (auto arg : args) {
        vm.sp->number = arg.number;
        vm.sp++;
    }
    vm.bp = vm.sp;
    __init();
    push_extra_root(&vm.cc);
    auto result = vm_continue(trace);
    clear_extra_roots();
    __shutdown();
    return result;
}

std::variant<Vm, std::vector<std::string>> build_vm(const VmConfig &config) {
    analyze_bytecode(config.entrypoint);

    if (!bc.meta.errors.empty()) {
        dump_bytecode(stdout);
        return bc.meta.errors;
    }

    bc.meta.data.clear();

    auto global_allocated = bc.globals_size;
    auto full_stack_size = global_allocated + config.stack_size;
    auto pinned_stack = std::unique_ptr<Value []>(new Value[full_stack_size]);
    auto pinned_rstack = std::unique_ptr<ReturnStackEntry []>(new ReturnStackEntry[config.rstack_size]);

    std::fill_n(pinned_stack.get(), global_allocated, Value{.number = BOX(0)});
    auto sp = pinned_stack.get() + global_allocated;

    std::unordered_map<std::string, i32> global_functions;

    for (i32 i = 0; i < bc.public_table_size; i++) {
        auto entry = bc.public_table[i];
        i32 fn = entry.offset;

        if (bc.get_string(entry.name_idx) != config.entrypoint) {
            continue;
        }

        global_functions[bc.get_string(entry.name_idx)] = fn;
    }

    return Vm{
        .ip = 0,
        .sp = sp,
        .rsp = 0,
        .fp = sp,
        .lp = sp,
        .bp = sp,
        .cc = nullptr,
        .stack_top = pinned_stack.get() + full_stack_size,
        .rstack_size = (u32)config.rstack_size,
        .globals_size = (u32)global_allocated,
        .code = bc.code,
        .stack = std::move(pinned_stack),
        .rstack = std::move(pinned_rstack),
        .global_functions = std::move(global_functions),
        .filename = config.filename,
    };
}

i32 app_execute(const char *bytecode_filename, bool trace = false, bool profile = false) {
    auto guard = bytecode_from_file(bytecode_filename);

    VmConfig config{
        .entrypoint = "main",
        .filename = std::string(bytecode_filename),
        .stack_size = 16 * 1024 * 1024,
        .rstack_size = 1024 * 1024,
    };

    std::chrono::steady_clock::time_point start;
    if (profile) {
        start = std::chrono::steady_clock::now();
    }

    auto maybe_vm = build_vm(config);

    if (trace) {
        dump_bytecode(stdout);
    }

    if (std::holds_alternative<std::vector<std::string>>(maybe_vm)) {
        for (auto &err : std::get<std::vector<std::string>>(maybe_vm)) {
            std::printf("%s\n", err.c_str());
        }
        return 1;
    }
    auto _ = std::move(guard);

    vm = std::move(std::get<Vm>(maybe_vm));

    if (profile) {
        auto end = std::chrono::steady_clock::now();
        std::fprintf(
            stderr,
            "Analysis time: %fms\n",
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1e6
        );
    }

    if (profile) {
        start = std::chrono::steady_clock::now();
    }

    auto result = vm_run(config.entrypoint, {
        Value { .number = BOX(0), },
        Value { .number = BOX(0), },
    }, trace);

    if (profile) {
        auto end = std::chrono::steady_clock::now();
        std::fprintf(
            stderr,
            "Execution time: %fs\n",
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1e3
        );
    }

    switch (result) {
        case ExecutionError::END:
            return 0;
        kase ExecutionError::NAME_NOT_FOUND:
            std::printf("Name not found\n");
        kase ExecutionError::STACK_UNDERFLOW:
            std::printf("Stack underflow\n");
        kase ExecutionError::STACK_OVERFLOW:
            std::printf("Stack overflow\n");
        kase ExecutionError::INTEGER_EXPECTED:
            std::printf("Integer expected\n");
        kase ExecutionError::RETURN_STACK_OVERFLOW:
            std::printf("Return stack overflow\n");
        kase ExecutionError::LOCAL_SLOT_OUT_OF_BOUNDS:
            std::printf("Local slot out of bounds\n");
        kase ExecutionError::ARITHMETIC_ERROR:
            std::printf("Arithmetic error\n");
        kase ExecutionError::ARG_SLOT_OUT_OF_BOUNDS:
            std::printf("Argument slot out of bounds\n");
        kase ExecutionError::POINTER_EXPECTED:
            std::printf("Pointer expected\n");
        kase ExecutionError::TOO_MANY_ARGUMENTS:
            std::printf("Too many arguments for a function\n");
        kase ExecutionError::STACK_INCONSISTENT:
            std::printf("Inconsistent stack usage\n");
        kase ExecutionError::NOT_IN_CLOSURE:
            std::printf("Attempt to access closure variables outside closure\n");
        kase ExecutionError::MALFORMED_INSTRUCTION:
            std::printf("Malformed instruction\n");
    }
    return 1;
}

i32 app_decompile(const char *bytecode_filename) {
    auto guard = bytecode_from_file(bytecode_filename);
    analyze_bytecode(std::nullopt);
    if (!bc.meta.errors.empty()) {
        for (const auto& err : bc.meta.errors) {
            std::fprintf(stderr, "Bytecode validation error: %s\n", err.c_str());
        }
        return 1;
    }
    dump_bytecode(stdout);
    return 0;
}

i32 main(i32 argc, char *argv[]) try {
    if (argc < 2) {
        std::printf("Usage: %s [subcommand] <bytecode.bc>\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        // if (argv[1] == std::string("fuzz")) {
        //     fuzzing_entrypoint();
        //     return 0;
        // }
        return app_execute(argv[1]);
    } else if (argc == 3) {
        if (std::string("execute").starts_with(argv[1])) {
            return app_execute(argv[2]);
        } else if (std::string("trace").starts_with(argv[1])) {
            return app_execute(argv[2], true);
        } else if (std::string("profile").starts_with(argv[1])) {
            return app_execute(argv[2], false, true);
        } else if (std::string("decompile").starts_with(argv[1])) {
            return app_decompile(argv[2]);
        } else {
            std::printf("Unknown subcommand: %s", argv[1]);
            return 1;
        }
    }
} catch (const std::runtime_error &e) {
    std::printf("%s\n", e.what());
    return 1;
}
