#ifndef J1T_HAL_INTERFACE_MACRO_ASSEMBLER_HPP
#define J1T_HAL_INTERFACE_MACRO_ASSEMBLER_HPP

#include <hal/interface/executable_memory.hpp>
#include <stdint.h>

namespace j1t::hal
{
    class macro_assembler;

    class macro_assembler
    {
      public:
        struct label
        {
            uint32_t id { 0 };
        };

        virtual ~macro_assembler(void) = default;

        virtual auto set_output(j1t::hal::executable_memory &output_memory) -> void
            = 0;

        virtual auto create_label(void) -> label                  = 0;
        virtual auto bind_label(label target_label) -> void       = 0;

        virtual auto branch(label target_label) -> void           = 0;
        virtual auto branch_equal(label target_label) -> void     = 0;
        virtual auto branch_not_equal(label target_label) -> void = 0;

        virtual auto emit_move_immediate_u32(
            uint32_t destination_register,
            uint32_t immediate_value
        ) -> void
            = 0;
        virtual auto emit_load_u32_from_base_plus_offset(
            uint32_t destination_register,
            uint32_t base_register,
            int32_t  offset
        ) -> void
            = 0;
        virtual auto emit_store_u32_from_register_to_base_plus_offset(
            uint32_t source_register,
            uint32_t base_register,
            int32_t  offset
        ) -> void
            = 0;

        virtual auto emit_load_pointer_from_base_plus_offset(
            uint32_t destination_register,
            uint32_t base_register,
            int32_t  offset
        ) -> void
            = 0;
        virtual auto emit_store_pointer_from_register_to_base_plus_offset(
            uint32_t source_register,
            uint32_t base_register,
            int32_t  offset
        ) -> void
            = 0;

        virtual auto emit_add_immediate_to_pointer(
            uint32_t destination_register,
            uint32_t source_register,
            uint32_t immediate_value
        ) -> void
            = 0;
        virtual auto emit_subtract_immediate_from_pointer(
            uint32_t destination_register,
            uint32_t source_register,
            uint32_t immediate_value
        ) -> void
            = 0;

        virtual auto emit_add_u32_register(
            uint32_t destination_register,
            uint32_t left_register,
            uint32_t right_register
        ) -> void
            = 0;
        virtual auto emit_compare_u32_registers(
            uint32_t left_register,
            uint32_t right_register
        ) -> void
            = 0;

        virtual auto emit_return(void) -> void               = 0;

        virtual auto finalize(void) -> void                  = 0;
        virtual auto code_size_bytes(void) const -> uint32_t = 0;
    };
}

#endif
