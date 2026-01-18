#include <cstdint>
#include <hal/aarch64/macro_assembler.hpp>

namespace
{
    auto store_le_u32(uint8_t *buffer, uint32_t value) -> void
    {
        buffer[0] = static_cast<uint8_t>(value & 0xFFu);
        buffer[1] = static_cast<uint8_t>((value >> 8u) & 0xFFu);
        buffer[2] = static_cast<uint8_t>((value >> 16u) & 0xFFu);
        buffer[3] = static_cast<uint8_t>((value >> 24u) & 0xFFu);
    }

    auto load_le_u32(const uint8_t *pointer) -> uint32_t
    {
        return static_cast<uint32_t>(pointer[0]) | (static_cast<uint32_t>(pointer[1]) << 8u)
             | (static_cast<uint32_t>(pointer[2]) << 16u) | (static_cast<uint32_t>(pointer[3]) << 24u);
    }
}

namespace j1t::hal::aarch64
{
    auto macro_assembler::set_output(j1t::hal::executable_memory &output_memory) -> void
    {
        output_memory_internal = &output_memory;
        program_counter        = 0u;
        label_states.clear();
        branch_patches.clear();
    }

    auto macro_assembler::code_size_bytes(void) const -> uint32_t
    {
        return program_counter;
    }

    auto macro_assembler::emit_u32_instruction(uint32_t instruction) -> void
    {
        if (output_memory_internal == nullptr)
        {
            throw std::runtime_error(
                "macro_assembler output memory not set before emitting "
                "instruction"
            );
        }
        if (program_counter + 4u > output_memory_internal->size())
        {
            throw std::runtime_error(
                "macro_assembler output memory too small for emitted "
                "instruction"
            );
        }

        store_le_u32(output_memory_internal->data() + program_counter, instruction);
        program_counter += 4u;
    }

    auto macro_assembler::emit_add_pointer_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
        -> void
    {
        // ADD xd, xn, xm
        emit_u32_instruction(
            0x8B00'0000u | ((right_register & 0x1Fu) << 16u) | ((left_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_shift_left_u32_immediate(uint32_t destination_register, uint32_t source_register, uint32_t shift)
        -> void
    {
        // LSL wd, wn, #shift  (alias of UBFM)
        if (shift > 31u)
        {
            throw std::runtime_error("emit_shift_left_u32_immediate: invalid shift");
        }

        // UBFM wd, wn, #((32 - shift) % 32), #(31 - shift)
        uint32_t immr = (32u - shift) & 31u;
        uint32_t imms = 31u - shift;

        emit_u32_instruction(
            0x5300'0000u | ((immr & 0x3Fu) << 16u) | ((imms & 0x3Fu) << 10u) | ((source_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_move_u32_register(uint32_t destination_register, uint32_t source_register) -> void
    {
        // MOV wd, wn  (alias: ORR wd, wzr, wn)
        emit_u32_instruction(0x2A00'03E0u | ((source_register & 0x1Fu) << 16u) | (destination_register & 0x1Fu));
    }

    auto macro_assembler::emit_subtract_u32_register(
        uint32_t destination_register,
        uint32_t left_register,
        uint32_t right_register
    ) -> void
    {
        // SUB wd, wn, wm
        emit_u32_instruction(
            0x4B00'0000u | ((right_register & 0x1Fu) << 16u) | ((left_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_multiply_u32_register(
        uint32_t destination_register,
        uint32_t left_register,
        uint32_t right_register
    ) -> void
    {
        // MUL wd, wn, wm   (alias: MADD wd, wn, wm, wzr)
        emit_u32_instruction(
            0x1B00'7C00u | ((right_register & 0x1Fu) << 16u) | ((left_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_divide_u32_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
        -> void
    {
        // UDIV wd, wn, wm
        emit_u32_instruction(
            0x1AC0'0800u | ((right_register & 0x1Fu) << 16u) | ((left_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_move_pointer_register(uint32_t destination_register, uint32_t source_register) -> void
    {
        // MOV xd, xn  (alias: ORR xd, xzr, xn)
        emit_u32_instruction(0xAA00'03E0u | ((source_register & 0x1Fu) << 16u) | (destination_register & 0x1Fu));
    }

    auto macro_assembler::emit_move_pointer_immediate(uint32_t destination_register, uintptr_t immediate_value) -> void
    {
        // Load 64-bit immediate into Xd using MOVZ/MOVK (x-reg variants)
        // MOVZ Xd, imm16, LSL #shift
        // MOVK Xd, imm16, LSL #shift
        auto value    = static_cast<uint64_t>(immediate_value);

        uint16_t imm0 = static_cast<uint16_t>((value >> 0u) & 0xFFFFu);
        uint16_t imm1 = static_cast<uint16_t>((value >> 16u) & 0xFFFFu);
        uint16_t imm2 = static_cast<uint16_t>((value >> 32u) & 0xFFFFu);
        uint16_t imm3 = static_cast<uint16_t>((value >> 48u) & 0xFFFFu);

        // MOVZ Xd, imm0, LSL #0
        emit_u32_instruction(0xD280'0000u | (static_cast<uint32_t>(imm0) << 5u) | (destination_register & 0x1Fu));

        if (imm1 != 0u)
        {
            // MOVK Xd, imm1, LSL #16  (hw=1)
            emit_u32_instruction(
                0xF280'0000u | (1u << 21u) | (static_cast<uint32_t>(imm1) << 5u) | (destination_register & 0x1Fu)
            );
        }
        if (imm2 != 0u)
        {
            // MOVK Xd, imm2, LSL #32  (hw=2)
            emit_u32_instruction(
                0xF280'0000u | (2u << 21u) | (static_cast<uint32_t>(imm2) << 5u) | (destination_register & 0x1Fu)
            );
        }
        if (imm3 != 0u)
        {
            // MOVK Xd, imm3, LSL #48  (hw=3)
            emit_u32_instruction(
                0xF280'0000u | (3u << 21u) | (static_cast<uint32_t>(imm3) << 5u) | (destination_register & 0x1Fu)
            );
        }
    }

    auto macro_assembler::emit_call_register(uint32_t function_register) -> void
    {
        // BLR Xn
        emit_u32_instruction(0xD63F'0000u | ((function_register & 0x1Fu) << 5u));
    }

    auto macro_assembler::emit_cset_u32(uint32_t destination_register, uint32_t condition) -> void
    {
        // CSET wd, cond  (alias: CSINC wd, wzr, wzr, invert(cond))
        // CSINC (32-bit): base 0x1A80'0400
        // cond in [15:12]
        const uint32_t inverted_condition = (condition ^ 1u) & 0x0Fu;

        emit_u32_instruction(
            0x1A80'0400u                                           // <-- FIX: CSINC, not CSEL
            | (inverted_condition << 12u) | ((31u & 0x1Fu) << 16u) // Rm = wzr
            | ((31u & 0x1Fu) << 5u)                                // Rn = wzr
            | (destination_register & 0x1Fu)                       // Rd
        );
    }

    auto macro_assembler::overwrite_u32_instruction(uint32_t program_counter_address, uint32_t instruction) -> void
    {
        if (output_memory_internal == nullptr)
        {
            throw std::runtime_error("overwrite: output not set");
        }

        uint8_t *dst       = output_memory_internal->data() + program_counter_address;

        dst[0]             = static_cast<uint8_t>(instruction & 0xFFu);
        dst[1]             = static_cast<uint8_t>((instruction >> 8u) & 0xFFu);
        dst[2]             = static_cast<uint8_t>((instruction >> 16u) & 0xFFu);
        dst[3]             = static_cast<uint8_t>((instruction >> 24u) & 0xFFu);

        uint32_t read_back = static_cast<uint32_t>(dst[0]) | (static_cast<uint32_t>(dst[1]) << 8u)
                           | (static_cast<uint32_t>(dst[2]) << 16u) | (static_cast<uint32_t>(dst[3]) << 24u);

        if (read_back != instruction)
        {
            throw std::runtime_error("overwrite: write did not stick (mapping/protection)");
        }
    }

    auto macro_assembler::create_label(void) -> label
    {
        uint32_t id = static_cast<uint32_t>(label_states.size());
        label_states.push_back(label_state {});
        return label { id };
    }

    auto macro_assembler::bind_label(label target_label) -> void
    {
        if (target_label.id >= label_states.size())
        {
            throw std::runtime_error("macro_assembler bind_label: invalid label");
        }

        label_states[target_label.id].is_bound        = true;
        label_states[target_label.id].program_counter = program_counter;
    }

    auto macro_assembler::encode_unconditional_immediate26(int32_t immediate26) -> uint32_t
    {
        // B imm26: p@ecode: 0b000101 [31:26]
        return 0x1400'0000u | (static_cast<uint32_t>(immediate26) & 0x03FF'FFFFu);
    }

    auto macro_assembler::encode_conditional_immediate19(uint32_t condition, int32_t immediate19) -> uint32_t
    {
        // B.cond imm19: p@ecode: 0b01010100 [31:24], cond [3:0]
        return 0x5400'0000u | ((static_cast<uint32_t>(immediate19) & 0x0007'FFFFu) << 5u) | (condition & 0x000Fu);
    }

    auto macro_assembler::branch_cond(uint32_t condition, label target_label) -> void
    {
        uint32_t instruction_pc = program_counter;
        emit_u32_instruction(encode_conditional_immediate19(condition, 0));
        branch_patches.push_back(
            branch_patch {
                instruction_pc,
                target_label.id,
                branch_patch::type::CONDITIONAL,
                (condition & 0x0Fu),
            }
        );
    }

    auto macro_assembler::branch(label target_label) -> void
    {
        uint32_t instruction_pc = program_counter;
        emit_u32_instruction(encode_unconditional_immediate26(0));
        branch_patches.push_back(branch_patch { instruction_pc, target_label.id, branch_patch::type::UNCONDITIONAL, 0u });
    }

    auto macro_assembler::branch_equal(label target_label) -> void
    {
        // eq = 0
        branch_cond(0u, target_label);
    }

    auto macro_assembler::branch_not_equal(label target_label) -> void
    {
        // ne = 1
        branch_cond(1u, target_label);
    }

    auto macro_assembler::emit_move_immediate_u32(uint32_t destination_register, uint32_t immediate_value) -> void
    {
        // movz wd, imm16, lsl 0/16; movk wd, imm16, lsl 16/0 (if needed)
        uint32_t imm0 = immediate_value & 0xFFFFu;
        uint32_t imm1 = (immediate_value >> 16u) & 0xFFFFu;

        emit_u32_instruction(0x5280'0000u | (imm0 << 5u) | (destination_register & 0x1Fu));

        if (imm1 != 0u)
        {
            emit_u32_instruction(0x7280'0000u | (0x1u << 21u) | (imm1 << 5u) | (destination_register & 0x1Fu));
        }
    }

    auto macro_assembler::emit_load_u32_from_base_plus_offset(
        uint32_t destination_register,
        uint32_t base_register,
        int32_t  offset
    ) -> void
    {
        // LDR wd, [xn, #imm12]
        if (offset < 0 || (offset % 4) != 0 || offset > 4092)
        {
            throw std::runtime_error(
                "macro_assembler emit_load_u32_from_base_plus_offset: "
                "invalid offset"
            );
        }

        uint32_t imm12 = static_cast<uint32_t>(offset) / 4u;

        emit_u32_instruction(
            0xB940'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u) | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_store_u32_from_register_to_base_plus_offset(
        uint32_t source_register,
        uint32_t base_register,
        int32_t  offset
    ) -> void
    {
        // STR wd, [xn, #imm12]
        if (offset < 0 || (offset % 4) != 0 || offset > 4092)
        {
            throw std::runtime_error(
                "macro_assembler "
                "emit_store_u32_from_register_to_base_plus_offset: "
                "invalid offset"
            );
        }

        uint32_t imm12 = static_cast<uint32_t>(offset) / 4u;

        emit_u32_instruction(0xB900'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u) | (source_register & 0x1Fu));
    }

    auto macro_assembler::emit_load_pointer_from_base_plus_offset(
        uint32_t destination_register,
        uint32_t base_register,
        int32_t  offset
    ) -> void
    {
        // LDR xw, [xn, #imm12]
        if (offset < 0 || (offset % 8) != 0)
        {
            throw std::runtime_error(
                "macro_assembler emit_load_pointer_from_base_plus_offset: "
                "invalid offset"
            );
        }
        uint32_t imm12 = static_cast<uint32_t>(offset) / 8u;
        emit_u32_instruction(
            0xF940'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u) | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_store_pointer_from_register_to_base_plus_offset(
        uint32_t source_register,
        uint32_t base_register,
        int32_t  offset
    ) -> void
    {
        // STR xw, [xn, #imm12]
        if (offset < 0 || (offset % 8) != 0)
        {
            throw std::runtime_error(
                "macro_assembler "
                "emit_store_pointer_from_register_to_base_plus_offset: "
                "invalid offset"
            );
        }
        uint32_t imm12 = static_cast<uint32_t>(offset) / 8u;
        emit_u32_instruction(0xF900'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u) | (source_register & 0x1Fu));
    }

    auto macro_assembler::emit_add_immediate_to_pointer(
        uint32_t destination_register,
        uint32_t source_register,
        uint32_t immediate_value
    ) -> void
    {
        // ADD xw, xn, #imm12
        if (immediate_value > 4095u)
        {
            throw std::runtime_error(
                "macro_assembler emit_add_immediate_to_pointer: "
                "invalid immediate value"
            );
        }

        emit_u32_instruction(
            0x9100'0000u | ((immediate_value & 0x0FFF) << 10u) | ((source_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_subtract_immediate_from_pointer(
        uint32_t destination_register,
        uint32_t source_register,
        uint32_t immediate_value
    ) -> void
    {
        // SUB xw, xn, #imm12
        if (immediate_value > 4095u)
        {
            throw std::runtime_error(
                "macro_assembler emit_subtract_immediate_from_pointer: "
                "invalid immediate value"
            );
        }

        emit_u32_instruction(
            0xD100'0000u | ((immediate_value & 0x0FFF) << 10u) | ((source_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_add_u32_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
        -> void
    {
        // ADD wd, wn, wm
        emit_u32_instruction(
            0x0B00'0000u | ((right_register & 0x1Fu) << 16u) | ((left_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_compare_u32_registers(uint32_t left_register, uint32_t right_register) -> void
    {
        // CMP wn, wm  (alias of SUBS wzr, wn, wm)
        // SUBS (shifted register) base: 0x6B00_0000
        // Rd must be WZR (=31) to avoid clobbering registers
        emit_u32_instruction(
            0x6B00'0000u | ((right_register & 0x1Fu) << 16u) // Rm
            | ((left_register & 0x1Fu) << 5u)                // Rn
            | 31u                                            // Rd = WZR
        );
    }

    auto macro_assembler::emit_compare_pointer_registers(uint32_t left_register, uint32_t right_register) -> void
    {
        // CMP xn, xm  (alias of SUBS xzr, xn, xm)
        // SUBS (shifted register, 64-bit) base: 0xEB00_0000
        emit_u32_instruction(
            0xEB00'0000u | ((right_register & 0x1Fu) << 16u) // Rm
            | ((left_register & 0x1Fu) << 5u)                // Rn
            | 31u                                            // Rd = XZR
        );
    }

    auto macro_assembler::emit_return(void) -> void
    {
        // RET
        emit_u32_instruction(0xD65F'03C0u);
    }

    // TODO: test
    auto macro_assembler::debug_branch_patch_count(void) const -> uint32_t
    {
        return static_cast<uint32_t>(branch_patches.size());
    }

    auto macro_assembler::debug_branch_patch_address_bytes(uint32_t patch_index) const -> uint32_t
    {
        if (patch_index >= branch_patches.size())
        {
            throw std::runtime_error("debug_branch_patch_address_bytes: out of range");
        }
        return branch_patches[patch_index].instruction_address_bytes;
    }

    auto macro_assembler::debug_output_base(void) const -> const uint8_t *
    {
        if (output_memory_internal == nullptr)
        {
            return nullptr;
        }
        return output_memory_internal->data();
    }

    auto macro_assembler::finalize(void) -> void
    {
        for (const branch_patch &patch : branch_patches)
        {
            if (patch.target_label_id >= label_states.size())
            {
                throw std::runtime_error("macro_assembler finalize: invalid branch target label");
            }

            const auto &target_label_state = label_states[patch.target_label_id];
            if (!target_label_state.is_bound)
            {
                throw std::runtime_error("macro_assembler finalize: unbound branch target label");
            }

            // IMPORTANT:
            // AArch64 branch immediates for:
            //   - B imm26
            //   - B.cond imm19
            // are PC-relative to the address of *this* instruction (not PC+4).
            //
            // If you use (branch_pc + 4) as the base, a branch to the next
            // instruction becomes imm=0, which encodes "b ." (infinite loop).
            const int64_t target_pc_bytes = static_cast<int64_t>(static_cast<uint64_t>(target_label_state.program_counter));
            const int64_t branch_pc_bytes = static_cast<int64_t>(static_cast<uint64_t>(patch.instruction_address_bytes));

            const int64_t delta_bytes = target_pc_bytes - branch_pc_bytes;

            if ((delta_bytes % 4) != 0)
            {
                throw std::runtime_error("macro_assembler finalize: branch target not aligned");
            }

            const int64_t delta_instructions_64 = delta_bytes / 4;
            if (patch.patch_type == branch_patch::type::UNCONDITIONAL)
            {
                // B imm26 : signed 26-bit (range: [-2^25, 2^25-1])
                if (delta_instructions_64 < -(1LL << 25) || delta_instructions_64 >= (1LL << 25))
                {
                    throw std::runtime_error(
                        "macro_assembler finalize: unconditional branch "
                        "target out of range"
                    );
                }

                const int32_t delta_instructions = static_cast<int32_t>(delta_instructions_64);

                overwrite_u32_instruction(
                    patch.instruction_address_bytes,
                    encode_unconditional_immediate26(delta_instructions)
                );
            }
            else
            {
                // B.cond imm19 : signed 19-bit (range: [-2^18, 2^18-1])
                if (delta_instructions_64 < -(1LL << 18) || delta_instructions_64 >= (1LL << 18))
                {
                    throw std::runtime_error(
                        "macro_assembler finalize: conditional branch "
                        "target out of range"
                    );
                }
                const int32_t delta_instructions = static_cast<int32_t>(delta_instructions_64);
                overwrite_u32_instruction(
                    patch.instruction_address_bytes,
                    encode_conditional_immediate19(patch.condition, delta_instructions)
                );
            }
        }
    }
}
