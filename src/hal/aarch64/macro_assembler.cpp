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
        return static_cast<uint32_t>(pointer[0])
             | (static_cast<uint32_t>(pointer[1]) << 8u)
             | (static_cast<uint32_t>(pointer[2]) << 16u)
             | (static_cast<uint32_t>(pointer[3]) << 24u);
    }
}

namespace j1t::hal::aarch64
{
    auto macro_assembler::set_output(j1t::hal::executable_memory &output_memory)
        -> void
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

    auto macro_assembler::overwrite_u32_instruction(
        uint32_t program_counter_address,
        uint32_t instruction
    ) -> void
    {
        if (output_memory_internal == nullptr)
        {
            throw std::runtime_error(
                "macro_assembler output memory not set before overwriting "
                "instruction"
            );
        }
        if (program_counter_address + 4u > output_memory_internal->size())
        {
            throw std::runtime_error(
                "macro_assembler output memory too small for overwriting "
                "instruction"
            );
        }

        store_le_u32(
            output_memory_internal->data() + program_counter_address,
            instruction
        );
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

    auto macro_assembler::encode_unconditional_immediate26(int32_t immediate26)
        -> uint32_t
    {
        // B imm26: p@ecode: 0b000101 [31:26]
        return 0x1400'0000u | (static_cast<uint32_t>(immediate26) & 0x03FF'FFFFu);
    }

    auto macro_assembler::encode_conditional_immediate19(
        uint32_t condition,
        int32_t  immediate19
    ) -> uint32_t
    {
        // B.cond imm19: p@ecode: 0b01010100 [31:24], cond [3:0]
        return 0x5400'0000u
             | ((static_cast<uint32_t>(immediate19) & 0x0007'FFFFu) << 5u)
             | (condition & 0x000Fu);
    }

    auto macro_assembler::branch(label target_label) -> void
    {
        uint32_t instruction_pc = program_counter;
        emit_u32_instruction(encode_unconditional_immediate26(0));
        branch_patches.push_back(
            branch_patch { instruction_pc, target_label.id, branch_patch::Type::UNCONDITIONAL }
        );
    }

    auto macro_assembler::branch_equal(label target_label) -> void
    {
        uint32_t instruction_pc = program_counter;
        emit_u32_instruction(encode_conditional_immediate19(0u, 0));
        branch_patches.push_back(
            branch_patch { instruction_pc, target_label.id, branch_patch::Type::EQUAL }
        );
    }

    auto macro_assembler::branch_not_equal(label target_label) -> void
    {
        uint32_t instruction_pc = program_counter;
        emit_u32_instruction(encode_conditional_immediate19(1u, 0));
        branch_patches.push_back(
            branch_patch { instruction_pc, target_label.id, branch_patch::Type::NOTEQUAL }
        );
    }

    auto macro_assembler::emit_move_immediate_u32(
        uint32_t destination_register,
        uint32_t immediate_value
    ) -> void
    {
        // movz wd, imm16, lsl 0/16; movk wd, imm16, lsl 16/0 (if needed)
        uint32_t imm0 = immediate_value & 0xFFFFu;
        uint32_t imm1 = (immediate_value >> 16u) & 0xFFFFu;

        emit_u32_instruction(
            0x5280'0000u | (imm0 << 5u) | (destination_register & 0x1Fu)
        );

        if (imm1 != 0u)
        {
            emit_u32_instruction(
                0x7280'0000u | (0x1u << 21u) | (imm1 << 5u)
                | (destination_register & 0x1Fu)
            );
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
            0xB940'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
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

        emit_u32_instruction(
            0xB900'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u)
            | (source_register & 0x1Fu)
        );
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
            0xF940'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u)
            | (destination_register & 0x1Fu)
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
        emit_u32_instruction(
            0xF900'0000u | (imm12 << 10u) | ((base_register & 0x1Fu) << 5u)
            | (source_register & 0x1Fu)
        );
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
            0x9100'0000u | ((immediate_value & 0x0FFF) << 10u)
            | ((source_register & 0x1Fu) << 5u) | (destination_register & 0x1Fu)
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
            0xD100'0000u | ((immediate_value & 0x0FFF) << 10u)
            | ((source_register & 0x1Fu) << 5u) | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_add_u32_register(
        uint32_t destination_register,
        uint32_t left_register,
        uint32_t right_register
    ) -> void
    {
        // ADD wd, wn, wm
        emit_u32_instruction(
            0x0B00'0000u | ((right_register & 0x1Fu) << 16u)
            | ((left_register & 0x1Fu) << 5u) | (destination_register & 0x1Fu)
        );
    }

    auto macro_assembler::emit_compare_u32_registers(
        uint32_t left_register,
        uint32_t right_register
    ) -> void
    {
        // CMP wn, wm
        emit_u32_instruction(
            0x6B00'0000u | ((right_register & 0x1Fu) << 16u)
            | ((left_register & 0x1Fu) << 5u)
        );
    }

    auto macro_assembler::emit_return(void) -> void
    {
        // RET
        emit_u32_instruction(0xD65F'03C0u);
    }

    auto macro_assembler::finalize(void) -> void
    {
        for (const branch_patch &patch : branch_patches)
        {
            if (patch.target_label_id >= label_states.size())
            {
                throw std::runtime_error(
                    "macro_assembler finalize: invalid branch target label"
                );
            }

            const auto &target_label_state = label_states[patch.target_label_id];
            if (!target_label_state.is_bound)
            {
                throw std::runtime_error(
                    "macro_assembler finalize: unbound branch target label"
                );
            }

            int32_t delta_bytes = static_cast<int32_t>(
                target_label_state.program_counter - patch.instruction_address_bytes
            );
            if (delta_bytes % 4 != 0)
            {
                throw std::runtime_error(
                    "macro_assembler finalize: branch target not aligned"
                );
            }

            int32_t delta_instructions = delta_bytes / 4;
            if (patch.type == branch_patch::Type::UNCONDITIONAL)
            {
                if (delta_instructions < -(1 << 25)
                    || delta_instructions >= (1 << 25))
                {
                    throw std::runtime_error(
                        "macro_assembler finalize: unconditional branch "
                        "target out of range"
                    );
                }
                overwrite_u32_instruction(
                    patch.instruction_address_bytes,
                    encode_unconditional_immediate26(delta_instructions)
                );
            }
            else
            {
                if (delta_instructions < -(1 << 18)
                    || delta_instructions >= (1 << 18))
                {
                    throw std::runtime_error(
                        "macro_assembler finalize: conditional branch "
                        "target out of range"
                    );
                }
                uint32_t condition = 0u;
                if (patch.type == branch_patch::Type::NOTEQUAL)
                {
                    condition = 1u;
                }
                overwrite_u32_instruction(
                    patch.instruction_address_bytes,
                    encode_conditional_immediate19(condition, delta_instructions)
                );
            }
        }
    }
}
