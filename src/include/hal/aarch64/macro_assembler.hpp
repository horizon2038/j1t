#ifndef J1T_HAL_AARCH64_MACRO_ASSEMBLER_HPP
#define J1T_HAL_AARCH64_MACRO_ASSEMBLER_HPP

#include <hal/interface/macro_assembler.hpp>

#include <vector>

namespace j1t::hal::aarch64
{
    class macro_assembler final : public j1t::hal::macro_assembler
    {
      public:
        auto set_output(j1t::hal::executable_memory &output_memory) -> void override;

        auto create_label(void) -> label override;
        auto bind_label(label target_label) -> void override;

        auto branch(label target_label) -> void override;
        auto branch_equal(label target_label) -> void override;
        auto branch_not_equal(label target_label) -> void override;

        auto emit_move_immediate_u32(uint32_t destination_register, uint32_t immediate_value) -> void override;
        auto emit_load_u32_from_base_plus_offset(uint32_t destination_register, uint32_t base_register, int32_t offset)
            -> void override;
        auto emit_store_u32_from_register_to_base_plus_offset(uint32_t source_register, uint32_t base_register, int32_t offset)
            -> void override;
        auto emit_add_pointer_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
            -> void override;
        auto emit_shift_left_u32_immediate(uint32_t destination_register, uint32_t source_register, uint32_t shift)
            -> void override;
        auto emit_move_u32_register(uint32_t destination_register, uint32_t source_register) -> void override;
        auto emit_move_pointer_immediate(uint32_t destination_register, uintptr_t immediate_value) -> void override;

        auto emit_move_pointer_register(uint32_t destination_register, uint32_t source_register) -> void override;

        auto emit_call_register(uint32_t function_register) -> void override;
        auto emit_subtract_u32_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
            -> void override;

        auto emit_multiply_u32_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
            -> void override;

        auto emit_divide_u32_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
            -> void override;
        auto emit_divide_i32_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
            -> void override;

        auto emit_cset_u32(uint32_t destination_register, uint32_t condition) -> void override;

        auto emit_load_pointer_from_base_plus_offset(uint32_t destination_register, uint32_t base_register, int32_t offset)
            -> void override;
        auto emit_store_pointer_from_register_to_base_plus_offset(
            uint32_t source_register,
            uint32_t base_register,
            int32_t  offset
        ) -> void override;

        auto emit_add_immediate_to_pointer(uint32_t destination_register, uint32_t source_register, uint32_t immediate_value)
            -> void override;
        auto emit_subtract_immediate_from_pointer(
            uint32_t destination_register,
            uint32_t source_register,
            uint32_t immediate_value
        ) -> void override;

        auto emit_add_u32_register(uint32_t destination_register, uint32_t left_register, uint32_t right_register)
            -> void override;
        auto emit_compare_u32_registers(uint32_t left_register, uint32_t right_register) -> void override;

        auto emit_compare_pointer_registers(uint32_t left_register, uint32_t right_register) -> void override;

        auto emit_return(void) -> void override;

        auto finalize(void) -> void override;
        auto code_size_bytes(void) const -> uint32_t override;

      public:
        // no override
        auto branch_cond(uint32_t condition, label target_label) -> void;
        auto debug_branch_patch_count(void) const -> uint32_t;
        auto debug_branch_patch_address_bytes(uint32_t patch_index) const -> uint32_t;
        auto debug_output_base(void) const -> const uint8_t *;

      private:
        struct label_state
        {
            bool     is_bound { false };
            uint32_t program_counter { 0 };
        };

        struct branch_patch
        {
            uint32_t instruction_address_bytes { 0 };
            uint32_t target_label_id { 0 };
            enum class type
            {
                UNCONDITIONAL,
                CONDITIONAL,
            } patch_type { type::UNCONDITIONAL };

            uint32_t condition { 0 };
        };

      private:
        auto emit_u32_instruction(uint32_t instruction_bits) -> void;
        auto overwrite_u32_instruction(uint32_t instruction_address_bytes, uint32_t instruction_bits) -> void;

        auto encode_unconditional_immediate26(int32_t immediate26) -> uint32_t;
        auto encode_conditional_immediate19(uint32_t condition, int32_t immediate19) -> uint32_t;

      private:
        executable_memory *output_memory_internal { nullptr };
        uint32_t           program_counter { 0 };

        std::vector<label_state>  label_states;
        std::vector<branch_patch> branch_patches;
    };
}

#endif
