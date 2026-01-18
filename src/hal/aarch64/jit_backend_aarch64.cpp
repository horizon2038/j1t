#include <hal/interface/icache.hpp>
#include <hal/interface/jit_backend.hpp>

#include <hal/aarch64/executable_memory_macos.hpp>
#include <hal/aarch64/macro_assembler.hpp>

#include <vm/opcodes.hpp>

#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    auto read_u8(const std::vector<uint8_t> &code, uint32_t &program_counter) -> uint8_t
    {
        if (program_counter >= code.size())
        {
            throw std::runtime_error("read_u8: program counter out of range");
        }

        return code[program_counter++];
    }

    auto read_u32_le(const std::vector<uint8_t> &code, uint32_t &program_counter) -> uint32_t
    {
        if (program_counter + 4u > code.size())
        {
            throw std::runtime_error("read_u32_le: program counter out of range");
        }

        uint32_t byte0 = static_cast<uint32_t>(read_u8(code, program_counter));
        uint32_t byte1 = static_cast<uint32_t>(read_u8(code, program_counter));
        uint32_t byte2 = static_cast<uint32_t>(read_u8(code, program_counter));
        uint32_t byte3 = static_cast<uint32_t>(read_u8(code, program_counter));

        return (byte0 << 0u) | (byte1 << 8u) | (byte2 << 16u) | (byte3 << 24u);
    }

    static auto emit_push_boolean_from_cmp_eq(
        j1t::hal::aarch64::macro_assembler &assembler,
        uint32_t                            register_stack_top,
        uint32_t                            register_tmp_w,
        j1t::hal::macro_assembler::label   &label_true,
        j1t::hal::macro_assembler::label   &label_end
    ) -> void
    {
        // if EQ -> label_true
        assembler.branch_equal(label_true);

        // false: w7 = 0
        assembler.emit_move_immediate_u32(register_tmp_w, 0u);
        assembler.branch(label_end);

        // true:
        assembler.bind_label(label_true);
        assembler.emit_move_immediate_u32(register_tmp_w, 1u);

        assembler.bind_label(label_end);

        // push w7
        assembler.emit_store_u32_from_register_to_base_plus_offset(register_tmp_w, register_stack_top, 0);
        assembler.emit_add_immediate_to_pointer(register_stack_top, register_stack_top, 4u);
    }

    static auto emit_check_can_pop_bytes(
        j1t::hal::aarch64::macro_assembler &assembler,
        uint32_t                            register_context_x19,
        uint32_t                            register_stack_top_x20,
        uint32_t                            register_tmp_x9,
        uint32_t                            register_tmp_x10,
        uint32_t                            register_error_w1,
        j1t::hal::macro_assembler::label   &label_runtime_error,
        int32_t                             offset_stack_base,
        uint32_t                            pop_bytes,
        uint32_t                            error_code
    ) -> void
    {
        // w1 = error_code (prepare)
        assembler.emit_move_immediate_u32(register_error_w1, error_code);

        // x10 = ctx->stack_base
        assembler.emit_load_pointer_from_base_plus_offset(register_tmp_x10, register_context_x19, offset_stack_base);

        // x9 = stack_top - pop_bytes
        assembler.emit_subtract_immediate_from_pointer(register_tmp_x9, register_stack_top_x20, pop_bytes);

        // if (x9 < base) goto error
        assembler.emit_compare_pointer_registers(register_tmp_x9, register_tmp_x10);

        // LO = 0x3
        assembler.branch_cond(0x3u, label_runtime_error);
    }

    static auto emit_check_can_push_bytes(
        j1t::hal::aarch64::macro_assembler &assembler,
        uint32_t                            register_context_x19,
        uint32_t                            register_stack_top_x20,
        uint32_t                            register_tmp_x9,
        uint32_t                            register_tmp_x10,
        uint32_t                            register_error_w1,
        j1t::hal::macro_assembler::label   &label_runtime_error,
        int32_t                             offset_stack_end,
        uint32_t                            push_bytes,
        uint32_t                            error_code
    ) -> void
    {
        // w1 = error_code (prepare)
        assembler.emit_move_immediate_u32(register_error_w1, error_code);

        // x10 = ctx->stack_end
        assembler.emit_load_pointer_from_base_plus_offset(register_tmp_x10, register_context_x19, offset_stack_end);

        // x9 = stack_top + push_bytes
        assembler.emit_add_immediate_to_pointer(register_tmp_x9, register_stack_top_x20, push_bytes);

        // if (x9 > end) goto error
        assembler.emit_compare_pointer_registers(register_tmp_x9, register_tmp_x10);

        // HI = 0x8
        assembler.branch_cond(0x8u, label_runtime_error);
    }

}

namespace j1t::hal
{
    namespace
    {
        class compiled_code_aarch64 final : public compiled_code
        {
          public:
            compiled_code_aarch64(std::unique_ptr<j1t::hal::executable_memory> memory, uint32_t code_size)
                : memory_internal(std::move(memory))
                , code_size_internal(code_size)
            {
            }

            auto entry(void) -> entry_type override
            {
                return reinterpret_cast<entry_type>(memory_internal->data());
            }

            auto code_size(void) const -> uint32_t override
            {
                return code_size_internal;
            }

          private:
            std::unique_ptr<j1t::hal::executable_memory> memory_internal;
            uint32_t                                     code_size_internal { 0 };
        };

        class jit_backend_aarch64 final : public j1t::hal::jit_backend
        {
          public:
            auto compile(const j1t::vm::program &target_program) -> std::unique_ptr<j1t::hal::compiled_code> override
            {
                // TODO: improve memory size estimation
                auto memory = std::make_unique<j1t::hal::aarch64::executable_memory_macos>(4096 * 8u);
                memory->begin_write();
                j1t::hal::aarch64::macro_assembler assembler;
                assembler.set_output(*memory);

                constexpr uint32_t REGISTER_CONTEXT   = 19;
                constexpr uint32_t REGISTER_STACK_TOP = 20;
                constexpr uint32_t REGISTER_TEMP_W2   = 2;
                constexpr uint32_t REGISTER_TEMP_W3   = 3;
                constexpr uint32_t REGISTER_RET_W0    = 0; // only return value
                constexpr uint32_t REGISTER_ZERO_WZR  = 31;
                constexpr uint32_t REGISTER_CALL_TMP  = 16; // x16 (IP0)
                constexpr uint32_t REGISTER_LR        = 30; // link
                constexpr uint32_t REGISTER_SP        = 31; // stack pointer
                constexpr uint32_t REGISTER_TMP_X9    = 9;
                constexpr uint32_t REGISTER_TMP_X10   = 10;
                constexpr uint32_t REGISTER_ERROR_W1  = 1;

                constexpr int32_t OFFSET_MEMORY       = 0;
                constexpr int32_t OFFSET_STACK_BASE   = static_cast<int32_t>(sizeof(void *) * 1);
                constexpr int32_t OFFSET_STACK_TOP    = static_cast<int32_t>(sizeof(void *) * 2);
                constexpr int32_t OFFSET_STACK_END    = static_cast<int32_t>(sizeof(void *) * 3);
                constexpr int32_t OFFSET_LOCALS       = static_cast<int32_t>(sizeof(void *) * 4);
                constexpr int32_t OFFSET_ERROR_CODE   = static_cast<int32_t>(sizeof(void *) * 5);

                auto label_runtime_error              = assembler.create_label();

                // Prologue: Save Context (LR, x19, x20)
                // [SP, 24] = LR, [SP, 16] = x20, [SP, 8] = x19
                assembler.emit_subtract_immediate_from_pointer(REGISTER_SP, REGISTER_SP, 32u);
                assembler.emit_store_pointer_from_register_to_base_plus_offset(REGISTER_LR, REGISTER_SP, 24);
                assembler.emit_store_pointer_from_register_to_base_plus_offset(REGISTER_STACK_TOP, REGISTER_SP, 16);
                assembler.emit_store_pointer_from_register_to_base_plus_offset(REGISTER_CONTEXT, REGISTER_SP, 8);

                // preserve context in x19 (callee-saved)
                assembler.emit_move_pointer_register(REGISTER_CONTEXT, 0 /*x0*/);

                // load stack_top to x20 and keep it across calls
                assembler.emit_load_pointer_from_base_plus_offset(REGISTER_STACK_TOP, REGISTER_CONTEXT, OFFSET_STACK_TOP);
                // 1st pass: create labels for each opcode boundary
                std::vector<j1t::hal::macro_assembler::label> pc_to_label;
                pc_to_label.resize(target_program.code.size() + 1);

                {
                    uint32_t scan_pc = 0;

                    while (scan_pc < target_program.code.size())
                    {
                        pc_to_label[scan_pc] = assembler.create_label();

                        uint8_t op_u8        = target_program.code[scan_pc++];
                        auto    op           = static_cast<j1t::vm::opcode>(op_u8);

                        switch (op)
                        {
                            case j1t::vm::opcode::PUSH :
                            case j1t::vm::opcode::LOCAL_GET :
                            case j1t::vm::opcode::LOCAL_SET :
                            case j1t::vm::opcode::JUMP :
                            case j1t::vm::opcode::JUMP_IF_ZERO :
                            case j1t::vm::opcode::JUMP_IF_NOT_ZERO :
                                scan_pc += 4;
                                break;

                            default :
                                break;
                        }
                    }

                    pc_to_label[scan_pc] = assembler.create_label();
                }

                uint32_t pc { 0 };

                auto label_epilogue = assembler.create_label();

                // test
                /*
                auto label_after = assembler.create_label();

                assembler.branch(label_after);
                assembler.bind_label(label_after);

                assembler.emit_move_immediate_u32(REGISTER_RET_W0, 0x1234u);

                assembler.emit_load_pointer_from_base_plus_offset(REGISTER_CONTEXT, REGISTER_SP, 8);
                assembler.emit_load_pointer_from_base_plus_offset(REGISTER_STACK_TOP, REGISTER_SP, 16);
                assembler.emit_load_pointer_from_base_plus_offset(REGISTER_LR, REGISTER_SP, 24);
                assembler.emit_add_immediate_to_pointer(REGISTER_SP, REGISTER_SP, 32u);
                assembler.emit_return();

                std::printf("memory->data()=%p assembler.output=%p\n", memory->data(), assembler.debug_output_base());
                assembler.finalize();

                memory->end_write();
                uintmax_t new_used_size = assembler.code_size_bytes();
                j1t::hal::flush_instruction_cache(memory->data(), new_used_size);
                memory->finalize();
                auto load_u32_le = [](const uint8_t *p) -> uint32_t
                {
                    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
                };

                std::printf("code_size_bytes=%ju\n", (uintmax_t)assembler.code_size_bytes());

                for (uint32_t i = 0; i < 24; ++i)
                {
                    uint32_t insn = load_u32_le(memory->data() + i * 4u);
                    std::printf("insn[%02u] pc=%03u 0x%08x\n", i, i * 4u, insn);
                }
                std::printf("insn[06] = 0x%08x\n", load_u32_le(memory->data() + 6 * 4u));
                return std::make_unique<compiled_code_aarch64>(std::move(memory), static_cast<uint32_t>(new_used_size));
                */

                while (pc < target_program.code.size())
                {
                    uint32_t        opcode_pc = pc;
                    uint8_t         opcode_u8 = read_u8(target_program.code, pc);
                    j1t::vm::opcode op        = static_cast<j1t::vm::opcode>(opcode_u8);
                    assembler.bind_label(pc_to_label[opcode_pc]);

                    switch (op)
                    {
                        case j1t::vm::opcode::NOP :
                            break;

                        case j1t::vm::opcode::PUSH :
                            {
                                uint32_t immediate_value = read_u32_le(target_program.code, pc);

                                emit_check_can_push_bytes(
                                    assembler,
                                    REGISTER_CONTEXT,
                                    REGISTER_STACK_TOP,
                                    REGISTER_TMP_X9,
                                    REGISTER_TMP_X10,
                                    REGISTER_ERROR_W1,
                                    label_runtime_error,
                                    OFFSET_STACK_END,
                                    4u,
                                    2u // STACK_OVERFLOW
                                );

                                // w2 = imm32
                                assembler.emit_move_immediate_u32(REGISTER_TEMP_W2, immediate_value);
                                // *sp = w2
                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W2,
                                    REGISTER_STACK_TOP,
                                    0
                                );
                                // sp += 4
                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::ADD :
                            {
                                emit_check_can_pop_bytes(
                                    assembler,
                                    REGISTER_CONTEXT,
                                    REGISTER_STACK_TOP,
                                    REGISTER_TMP_X9,
                                    REGISTER_TMP_X10,
                                    REGISTER_ERROR_W1,
                                    label_runtime_error,
                                    OFFSET_STACK_BASE,
                                    8u,
                                    1u
                                );
                                // pop rhs
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                // pop lhs
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W3, REGISTER_STACK_TOP, 0);

                                // w3 += w2
                                assembler.emit_add_u32_register(REGISTER_TEMP_W3, REGISTER_TEMP_W3, REGISTER_TEMP_W2);

                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W3,
                                    REGISTER_STACK_TOP,
                                    0
                                );

                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::RET :
                            {
                                emit_check_can_pop_bytes(
                                    assembler,
                                    REGISTER_CONTEXT,
                                    REGISTER_STACK_TOP,
                                    REGISTER_TMP_X9,
                                    REGISTER_TMP_X10,
                                    REGISTER_ERROR_W1,
                                    label_runtime_error,
                                    OFFSET_STACK_BASE,
                                    4u,
                                    1u
                                );

                                // pop return_value -> w2
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                // save stack_top
                                assembler.emit_store_pointer_from_register_to_base_plus_offset(
                                    REGISTER_STACK_TOP,
                                    REGISTER_CONTEXT,
                                    OFFSET_STACK_TOP
                                );

                                // w0 = w2
                                assembler.emit_add_u32_register(REGISTER_RET_W0, REGISTER_ZERO_WZR, REGISTER_TEMP_W2);

                                // jump to common epilogue
                                assembler.branch(label_epilogue);
                                break;
                            }

                        case j1t::vm::opcode::LOCAL_GET :
                            {
                                uint32_t local_index = read_u32_le(target_program.code, pc);

                                assembler.emit_load_pointer_from_base_plus_offset(
                                    4, // x4 = locals_ptr
                                    REGISTER_CONTEXT,
                                    OFFSET_LOCALS
                                );

                                assembler.emit_move_immediate_u32(
                                    5, // w5 = index
                                    local_index
                                );

                                assembler.emit_shift_left_u32_immediate(
                                    5, // w5 = w5 << 2
                                    5,
                                    2u
                                );

                                assembler.emit_add_pointer_register(
                                    6, // x6 = x4 + x5
                                    4,
                                    5
                                );

                                assembler.emit_load_u32_from_base_plus_offset(
                                    REGISTER_TEMP_W2, // w2 = locals[index]
                                    6,
                                    0
                                );

                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W2,
                                    REGISTER_STACK_TOP,
                                    0
                                );

                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::LOCAL_SET :
                            {
                                uint32_t local_index = read_u32_le(target_program.code, pc);

                                // pop value -> w2
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(
                                    REGISTER_TEMP_W2, // w2
                                    REGISTER_STACK_TOP,
                                    0
                                );

                                // x4 = locals_ptr
                                assembler.emit_load_pointer_from_base_plus_offset(
                                    4, // x4
                                    REGISTER_CONTEXT,
                                    OFFSET_LOCALS
                                );

                                // w5 = local_index
                                assembler.emit_move_immediate_u32(
                                    5, // w5
                                    local_index
                                );

                                // w5 <<= 2 (index * 4)
                                assembler.emit_shift_left_u32_immediate(
                                    5, // w5
                                    5,
                                    2u
                                );

                                // x6 = x4 + x5
                                assembler.emit_add_pointer_register(
                                    6, // x6
                                    4, // x4
                                    5  // x5
                                );

                                // locals[index] = w2
                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W2, // w2
                                    6,                // x6
                                    0
                                );

                                break;
                            }

                        case j1t::vm::opcode::SUB :
                            {
                                // rhs -> w2
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                // lhs -> w3
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W3, REGISTER_STACK_TOP, 0);

                                assembler.emit_subtract_u32_register(REGISTER_TEMP_W3, REGISTER_TEMP_W3, REGISTER_TEMP_W2);

                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W3,
                                    REGISTER_STACK_TOP,
                                    0
                                );
                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::MUL :
                            {
                                // rhs -> w2
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                // lhs -> w3
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W3, REGISTER_STACK_TOP, 0);

                                assembler.emit_multiply_u32_register(REGISTER_TEMP_W3, REGISTER_TEMP_W3, REGISTER_TEMP_W2);

                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W3,
                                    REGISTER_STACK_TOP,
                                    0
                                );
                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::DIV :
                            {
                                // rhs -> w2
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                // lhs -> w3
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W3, REGISTER_STACK_TOP, 0);

                                assembler.emit_divide_i32_register(REGISTER_TEMP_W3, REGISTER_TEMP_W3, REGISTER_TEMP_W2);

                                assembler.emit_store_u32_from_register_to_base_plus_offset(
                                    REGISTER_TEMP_W3,
                                    REGISTER_STACK_TOP,
                                    0
                                );
                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::EQ :
                            {
                                // pop rhs -> w2
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                // pop lhs -> w3
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W3, REGISTER_STACK_TOP, 0);

                                assembler.emit_compare_u32_registers(REGISTER_TEMP_W3, REGISTER_TEMP_W2);

                                // w7 = (w3 == w2) ? 1 : 0
                                assembler.emit_cset_u32(7, 0u); // eq = 0

                                // push w7
                                assembler.emit_store_u32_from_register_to_base_plus_offset(7, REGISTER_STACK_TOP, 0);
                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::LESS_THAN_SIGNED :
                            {
                                // rhs w2, lhs w3
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W3, REGISTER_STACK_TOP, 0);

                                assembler.emit_compare_u32_registers(REGISTER_TEMP_W3, REGISTER_TEMP_W2);
                                assembler.emit_cset_u32(7, 0xBu);

                                assembler.emit_store_u32_from_register_to_base_plus_offset(7, REGISTER_STACK_TOP, 0);
                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::LESS_THAN_UNSIGNED :
                            {
                                // rhs w2, lhs w3
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W3, REGISTER_STACK_TOP, 0);

                                assembler.emit_compare_u32_registers(REGISTER_TEMP_W3, REGISTER_TEMP_W2);
                                assembler.emit_cset_u32(7, 0x3u);

                                assembler.emit_store_u32_from_register_to_base_plus_offset(7, REGISTER_STACK_TOP, 0);
                                assembler.emit_add_immediate_to_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::POP :
                            {
                                emit_check_can_pop_bytes(
                                    assembler,
                                    REGISTER_CONTEXT,
                                    REGISTER_STACK_TOP,
                                    REGISTER_TMP_X9,
                                    REGISTER_TMP_X10,
                                    REGISTER_ERROR_W1,
                                    label_runtime_error,
                                    OFFSET_STACK_BASE,
                                    4u,
                                    1u // STACK_UNDERFLOW
                                );
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                break;
                            }

                        case j1t::vm::opcode::JUMP :
                            {
                                int32_t rel               = static_cast<int32_t>(read_u32_le(target_program.code, pc));

                                const int64_t base_pc     = static_cast<int64_t>(opcode_pc);
                                const int64_t target_pc64 = base_pc + static_cast<int64_t>(rel);

                                if (target_pc64 < 0 || target_pc64 > static_cast<int64_t>(target_program.code.size()))
                                {
                                    throw std::runtime_error("JUMP: target_pc out of range");
                                }

                                const uint32_t target_pc = static_cast<uint32_t>(target_pc64);
                                assembler.branch(pc_to_label[target_pc]);
                                break;
                            }

                        case j1t::vm::opcode::JUMP_IF_ZERO :
                            {
                                int32_t rel               = static_cast<int32_t>(read_u32_le(target_program.code, pc));

                                const int64_t base_pc     = static_cast<int64_t>(opcode_pc);
                                const int64_t target_pc64 = base_pc + static_cast<int64_t>(rel);

                                if (target_pc64 < 0 || target_pc64 > static_cast<int64_t>(target_program.code.size()))
                                {
                                    throw std::runtime_error("JUMP_IF_ZERO: target_pc out of range");
                                }

                                const uint32_t target_pc = static_cast<uint32_t>(target_pc64);

                                emit_check_can_pop_bytes(
                                    assembler,
                                    REGISTER_CONTEXT,
                                    REGISTER_STACK_TOP,
                                    REGISTER_TMP_X9,
                                    REGISTER_TMP_X10,
                                    REGISTER_ERROR_W1,
                                    label_runtime_error,
                                    OFFSET_STACK_BASE,
                                    4u,
                                    1u
                                );

                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                assembler.emit_compare_u32_registers(REGISTER_TEMP_W2, REGISTER_ZERO_WZR);
                                assembler.branch_equal(pc_to_label[target_pc]);
                                break;
                            }

                        case j1t::vm::opcode::JUMP_IF_NOT_ZERO :
                            {
                                int32_t rel               = static_cast<int32_t>(read_u32_le(target_program.code, pc));

                                const int64_t base_pc     = static_cast<int64_t>(opcode_pc);
                                const int64_t target_pc64 = base_pc + static_cast<int64_t>(rel);

                                if (target_pc64 < 0 || target_pc64 > static_cast<int64_t>(target_program.code.size()))
                                {
                                    throw std::runtime_error("JUMP_IF_NOT_ZERO: target_pc out of range");
                                }

                                const uint32_t target_pc = static_cast<uint32_t>(target_pc64);

                                emit_check_can_pop_bytes(
                                    assembler,
                                    REGISTER_CONTEXT,
                                    REGISTER_STACK_TOP,
                                    REGISTER_TMP_X9,
                                    REGISTER_TMP_X10,
                                    REGISTER_ERROR_W1,
                                    label_runtime_error,
                                    OFFSET_STACK_BASE,
                                    4u,
                                    1u
                                );

                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                assembler.emit_compare_u32_registers(REGISTER_TEMP_W2, REGISTER_ZERO_WZR);
                                assembler.branch_not_equal(pc_to_label[target_pc]);
                                break;
                            }

                        case j1t::vm::opcode::PRINT :
                            {
                                // pop value -> w2
                                assembler.emit_subtract_immediate_from_pointer(REGISTER_STACK_TOP, REGISTER_STACK_TOP, 4u);
                                assembler.emit_load_u32_from_base_plus_offset(REGISTER_TEMP_W2, REGISTER_STACK_TOP, 0);

                                // arg0 (w0) = w2  (putchar expects int in w0)
                                assembler.emit_add_u32_register(
                                    0, // w0
                                    REGISTER_ZERO_WZR,
                                    REGISTER_TEMP_W2
                                );

                                // call putchar
                                assembler.emit_move_pointer_immediate(
                                    REGISTER_CALL_TMP,
                                    reinterpret_cast<uintptr_t>(&putchar)
                                );
                                assembler.emit_call_register(REGISTER_CALL_TMP);
                                break;
                            }

                        default :
                            {
                                throw std::runtime_error("jit_backend_aarch64: unsupported opcode");
                            }
                    }
                }

                assembler.bind_label(pc_to_label[static_cast<uint32_t>(target_program.code.size())]);

                // finalize
                assembler.emit_store_pointer_from_register_to_base_plus_offset(
                    REGISTER_STACK_TOP,
                    REGISTER_CONTEXT,
                    OFFSET_STACK_TOP
                );
                assembler.emit_move_immediate_u32(REGISTER_RET_W0, 0u);
                assembler.branch(label_epilogue);

                assembler.bind_label(label_runtime_error);

                assembler.emit_store_pointer_from_register_to_base_plus_offset(
                    REGISTER_STACK_TOP,
                    REGISTER_CONTEXT,
                    OFFSET_STACK_TOP
                );

                assembler.emit_store_u32_from_register_to_base_plus_offset(
                    REGISTER_ERROR_W1,
                    REGISTER_CONTEXT,
                    OFFSET_ERROR_CODE
                );

                // return 0
                assembler.emit_move_immediate_u32(REGISTER_RET_W0, 0u);
                assembler.branch(label_epilogue);

                assembler.bind_label(label_epilogue);

                // Epilogue (Fallthrough)
                // assembler.emit_move_immediate_u32(REGISTER_RET_W0, 0xDEADu);
                assembler.emit_load_pointer_from_base_plus_offset(REGISTER_CONTEXT, REGISTER_SP, 8);
                assembler.emit_load_pointer_from_base_plus_offset(REGISTER_STACK_TOP, REGISTER_SP, 16);
                assembler.emit_load_pointer_from_base_plus_offset(REGISTER_LR, REGISTER_SP, 24);
                assembler.emit_add_immediate_to_pointer(REGISTER_SP, REGISTER_SP, 32u);

                assembler.emit_return();
                std::printf("memory->data()=%p assembler.output=%p\n", memory->data(), assembler.debug_output_base());
                assembler.finalize();

                memory->end_write();
                uintmax_t used_size = assembler.code_size_bytes();
                j1t::hal::flush_instruction_cache(memory->data(), used_size);
                memory->finalize();

                return std::make_unique<compiled_code_aarch64>(std::move(memory), static_cast<uint32_t>(used_size));
            }
        };
    }

    auto make_native_jit_backend(void) -> std::unique_ptr<jit_backend>
    {
        return std::make_unique<jit_backend_aarch64>();
    }
}
