#ifndef J1T_VM_ASSEMBLER_HPP
#define J1T_VM_ASSEMBLER_HPP

#include <vm/interpreter.hpp>

namespace j1t::vm
{
    class assembler;

    class assembler
    {
      public:
        struct label
        {
            uint32_t id { 0 };
        };

        struct label_state
        {
            bool     is_bound { false };
            uint32_t pc { 0 };
        };

        struct patch
        {
            uint32_t imm_position { 0 };
            uint32_t opcode_pc { 0 };
            // TODO: clean this ( remove pointer )
            uint32_t target_label_id { 0 };
        };

      public:
        std::vector<uint8_t>     code;
        std::vector<label_state> label_states;
        std::vector<patch>       patches;

      public:
        auto create_label(void) -> label;

        auto emit_8(uint8_t value) -> void;
        auto emit_u32_le(uint32_t value) -> void;
        auto emit_i32_le(int32_t value) -> void;
        auto patch_i32_le(uint32_t position, int32_t value) -> void;
        auto emit_op(j1t::vm::opcode op) -> void;
        auto emit_push_u32(uint32_t value) -> void;
        auto emit_local_get(uint32_t local_index) -> void;
        auto emit_local_set(uint32_t local_index) -> void;
        auto emit_add(void) -> void;
        auto emit_sub(void) -> void;
        auto emit_mul(void) -> void;
        auto emit_div(void) -> void;
        auto emit_eq(void) -> void;
        auto emit_load8_u(void) -> void;
        auto emit_load16_u(void) -> void;
        auto emit_load32(void) -> void;
        auto emit_jump(label target) -> void;
        auto emit_jump_if_zero(label target) -> void;
        auto emit_jump_if_not_zero(label target) -> void;
        auto emit_ret(void) -> void;
        auto emit_print(void) -> void;
        auto emit_print_literal(const char *str) -> void;
        auto bind_label(label target_label) -> void;
        auto finalize(void) -> void;

        auto to_program(void) -> const program &;
    };
}

#endif
