#ifndef J1T_VM_INTERPRETER_HPP
#define J1T_VM_INTERPRETER_HPP

#include <expected>
#include <optional>
#include <span>
#include <stdint.h>
#include <vector>

#include <vm/opcodes.hpp>

namespace j1t::vm
{
    class interpreter;
    struct program;
    struct state;

    class interpreter
    {
      public:
        enum class error
        {
            STACK_UNDERFLOW,
            INVALID_LOCAL_INDEX,
            PC_OUT_OF_RANGE,
            INVALID_OPCODE,
            DIVISION_BY_ZERO,
            MEMORY_OUT_OF_BOUNDS,
            NON_TERMINATED_PROGRAM,
        };

        static constexpr const char *error_to_string(error err)
        {
            switch (err)
            {
                case error::STACK_UNDERFLOW :
                    return "Stack underflow";

                case error::INVALID_LOCAL_INDEX :
                    return "Invalid local index";

                case error::PC_OUT_OF_RANGE :
                    return "Program counter out of range";

                case error::INVALID_OPCODE :
                    return "Invalid opcode";

                case error::DIVISION_BY_ZERO :
                    return "Division by zero";

                case error::MEMORY_OUT_OF_BOUNDS :
                    return "Memory out of bounds";

                case error::NON_TERMINATED_PROGRAM :
                    return "Non-terminated program";

                default :
                    return "Unknown error";
            }
        }

        struct execution_info
        {
            uint32_t pc;
            uint32_t return_value;
        };

        template<typename T = execution_info>
        using result = std::expected<T, error>;

      public:
        auto run(const program &target_program, state &initial_state) -> result<>;

      private:
        inline static constexpr uint32_t MAX_STACK_SIZE  = 1024;
        inline static constexpr uint32_t MAX_MEMORY_SIZE = 65536;
    };

    struct program
    {
        std::vector<uint8_t> code;
    };

    struct state
    {
        std::vector<uint8_t>  memory;
        std::vector<uint32_t> stack;
        std::vector<uint32_t> locals;
    };

}

#endif
