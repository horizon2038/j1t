#include <vm/assembler.hpp>
#include <vm/emitter.hpp>

namespace j1t::vm
{
    auto assembler::create_label(void) -> label
    {
        uint32_t id = static_cast<uint32_t>(label_states.size());
        label_states.push_back(label_state {});
        return label { id };
    }

    auto assembler::emit_8(uint8_t value) -> void
    {
        ::j1t::vm::emit_u8(code, value);
    }

    auto assembler::emit_u32_le(uint32_t value) -> void
    {
        ::j1t::vm::emit_u32_le(code, value);
    }

    auto assembler::emit_i32_le(int32_t value) -> void
    {
        ::j1t::vm::emit_i32_le(code, value);
    }

    auto assembler::patch_i32_le(uint32_t position, int32_t value) -> void
    {
        ::j1t::vm::patch_i32_le(code, position, value);
    }

    auto assembler::emit_op(j1t::vm::opcode op) -> void
    {
        ::j1t::vm::emit_op(code, op);
    }

    auto assembler::emit_push_u32(uint32_t value) -> void
    {
        ::j1t::vm::emit_push(code, value);
    }

    auto assembler::emit_local_get(uint32_t local_index) -> void
    {
        ::j1t::vm::emit_local_get(code, local_index);
    }

    auto assembler::emit_local_set(uint32_t local_index) -> void
    {
        ::j1t::vm::emit_local_set(code, local_index);
    }

    auto assembler::emit_add(void) -> void
    {
        ::j1t::vm::emit_add(code);
    }

    auto assembler::emit_sub(void) -> void
    {
        ::j1t::vm::emit_sub(code);
    }

    auto assembler::emit_mul(void) -> void
    {
        ::j1t::vm::emit_mul(code);
    }

    auto assembler::emit_div(void) -> void
    {
        ::j1t::vm::emit_div(code);
    }

    auto assembler::emit_eq(void) -> void
    {
        ::j1t::vm::emit_eq(code);
    }

    auto assembler::emit_load8_u(void) -> void
    {
        ::j1t::vm::emit_load8_u(code);
    }

    auto assembler::emit_load16_u(void) -> void
    {
        ::j1t::vm::emit_load16_u(code);
    }

    auto assembler::emit_load32(void) -> void
    {
        ::j1t::vm::emit_load32(code);
    }

    auto assembler::emit_jump(label target_label) -> void
    {
        uint32_t opcode_pc = code.size();
        emit_op(j1t::vm::opcode::JUMP);

        uint32_t imm_position = code.size();
        emit_i32_le(0); // placeholder

        patches.push_back(patch { imm_position, opcode_pc, target_label.id });
    }

    auto assembler::emit_jump_if_zero(label target_label) -> void
    {
        uint32_t opcode_pc = code.size();
        emit_op(j1t::vm::opcode::JUMP_IF_ZERO);

        uint32_t imm_position = code.size();
        emit_i32_le(0); // placeholder

        patches.push_back(patch { imm_position, opcode_pc, target_label.id });
    }

    auto assembler::emit_jump_if_not_zero(label target_label) -> void
    {
        uint32_t opcode_pc = code.size();
        emit_op(j1t::vm::opcode::JUMP_IF_NOT_ZERO);

        uint32_t imm_position = code.size();
        emit_i32_le(0); // placeholder

        patches.push_back(patch { imm_position, opcode_pc, target_label.id });
    }

    auto assembler::emit_ret(void) -> void
    {
        ::j1t::vm::emit_ret(code);
    }

    auto assembler::emit_print(void) -> void
    {
        ::j1t::vm::emit_print(code);
    }

    auto assembler::emit_print_literal(const char *str) -> void
    {
        ::j1t::vm::emit_print_literal(code, str);
    }

    auto assembler::bind_label(label target_label) -> void
    {
        if (target_label.id >= label_states.size())
        {
            fprintf(
                stderr,
                "Error: Attempted to bind invalid label (id=%u)\n",
                target_label.id
            );
            std::abort();
        }

        label_states[target_label.id].is_bound = true;
        label_states[target_label.id].pc = static_cast<uint32_t>(code.size());
    }

    auto assembler::finalize(void) -> void
    {
        for (const auto &p : patches)
        {
            if (p.target_label_id >= label_states.size())
            {
                fprintf(
                    stderr,
                    "Error: Attempted to finalize assembler with unbound "
                    "label\n"
                );
                std::abort();
            }

            const auto &target = label_states[p.target_label_id];
            if (!target.is_bound)
            {
                fprintf(
                    stderr,
                    "Error: Attempted to finalize assembler with unbound "
                    "label (id=%u)\n",
                    p.target_label_id
                );
                std::abort();
            }

            int32_t relative_offset
                = static_cast<int32_t>(target.pc)
                - static_cast<int32_t>(p.opcode_pc); // 1 (opcode) + 4 (imm32)

            patch_i32_le(p.imm_position, relative_offset);
        }
    }

    auto assembler::to_program(void) -> const program &
    {
        static program prog;
        prog.code = std::move(code);
        return prog;
    }
}
