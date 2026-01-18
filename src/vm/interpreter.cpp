#include <vm/interpreter.hpp>

namespace j1t::vm
{
    auto interpreter::run(const program &target_program, state &initial_state) -> result<>
    {
        uint32_t                       pc   = 0;
        const std::span<const uint8_t> code = target_program.code;

        auto read_u8                        = [&](void) -> std::optional<uint8_t>
        {
            if (pc >= code.size())
            {
                return std::nullopt;
            }

            return code[pc++];
        };

        auto read_u32_le = [&](void) -> std::optional<uint32_t>
        {
            if (pc + 4 > code.size())
            {
                return std::nullopt;
            }

            uint32_t value = static_cast<uint32_t>(code[pc]) | (static_cast<uint32_t>(code[pc + 1]) << 8)
                           | (static_cast<uint32_t>(code[pc + 2]) << 16) | (static_cast<uint32_t>(code[pc + 3]) << 24);
            pc += 4;

            return value;
        };

        auto read_i32_le = [&](void) -> std::optional<int32_t>
        {
            return read_u32_le().transform(
                [](uint32_t value)
                {
                    return static_cast<int32_t>(value);
                }
            );
        };

        auto pop_u32 = [&](void) -> std::optional<uint32_t>
        {
            if (initial_state.stack.empty())
            {
                return std::nullopt;
            }

            uint32_t value = initial_state.stack.back();
            initial_state.stack.pop_back();

            return value;
        };

        auto push_u32 = [&](uint32_t value) -> void
        {
            initial_state.stack.push_back(value);
        };

        auto jump_relative = [&](uint32_t opcode_pc, int32_t relative_offset) -> result<>
        {
            auto base = static_cast<std::ptrdiff_t>(opcode_pc);
            auto next = base + static_cast<std::ptrdiff_t>(relative_offset);

            if (next < 0)
            {
                return std::unexpected(error::PC_OUT_OF_RANGE);
            }

            pc = static_cast<uint32_t>(next);

            if (pc > code.size())
            {
                return std::unexpected(error::PC_OUT_OF_RANGE);
            }

            return {};
        };

        while (pc < code.size())
        {
            uint32_t opcode_pc = pc;

            auto opcode_u8     = read_u8();
            if (!opcode_u8.has_value())
            {
                return std::unexpected(error::PC_OUT_OF_RANGE);
            }

            opcode op = static_cast<opcode>(opcode_u8.value());

            switch (op)
            {
                case opcode::NOP :
                    {
                        // do nothing
                        break;
                    }

                case opcode::PUSH :
                    {
                        auto imm32 = read_u32_le();
                        if (!imm32.has_value())
                        {
                            return std::unexpected(error::PC_OUT_OF_RANGE);
                        }

                        push_u32(imm32.value());
                        break;
                    }

                case opcode::POP :
                    {
                        auto value = pop_u32();
                        if (!value.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        break;
                    }

                case opcode::LOCAL_GET :
                    {
                        auto local_index = read_u32_le();
                        if (!local_index.has_value())
                        {
                            return std::unexpected(error::PC_OUT_OF_RANGE);
                        }

                        uint32_t index = local_index.value();
                        if (index >= initial_state.locals.size())
                        {
                            return std::unexpected(error::INVALID_LOCAL_INDEX);
                        }

                        uint32_t value = initial_state.locals[index];
                        push_u32(value);
                        break;
                    }

                case opcode::LOCAL_SET :
                    {
                        auto local_index = read_u32_le();
                        if (!local_index.has_value())
                        {
                            return std::unexpected(error::PC_OUT_OF_RANGE);
                        }

                        uint32_t index = local_index.value();
                        if (index >= initial_state.locals.size())
                        {
                            return std::unexpected(error::INVALID_LOCAL_INDEX);
                        }

                        auto value = pop_u32();
                        if (!value.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        initial_state.locals[index] = value.value();
                        break;
                    }

                case opcode::ADD :
                    {
                        auto rhs = pop_u32();
                        auto lhs = pop_u32();
                        if (!lhs.has_value() || !rhs.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        push_u32(lhs.value() + rhs.value());
                        break;
                    }

                case opcode::SUB :
                    {
                        auto rhs = pop_u32();
                        auto lhs = pop_u32();
                        if (!lhs.has_value() || !rhs.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        push_u32(lhs.value() - rhs.value());
                        break;
                    }

                case opcode::MUL :
                    {
                        auto rhs = pop_u32();
                        auto lhs = pop_u32();
                        if (!lhs.has_value() || !rhs.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        push_u32(lhs.value() * rhs.value());
                        break;
                    }

                case opcode::DIV :
                    {
                        auto rhs_u = pop_u32();
                        auto lhs_u = pop_u32();
                        if (!lhs_u.has_value() || !rhs_u.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        int32_t rhs = static_cast<int32_t>(rhs_u.value());
                        int32_t lhs = static_cast<int32_t>(lhs_u.value());

                        if (rhs == 0)
                        {
                            return std::unexpected(error::DIVISION_BY_ZERO);
                        }

                        int32_t result = lhs / rhs;

                        push_u32(static_cast<uint32_t>(result));
                        break;
                    }

                case opcode::EQ :
                    {
                        auto rhs = pop_u32();
                        auto lhs = pop_u32();
                        if (!lhs.has_value() || !rhs.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        push_u32(lhs.value() == rhs.value() ? 1 : 0);
                        break;
                    }

                case opcode::LOAD_8_UNSIGNED :
                    {
                        auto address = pop_u32();
                        if (!address.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        uint32_t addr = address.value();
                        if (addr >= initial_state.memory.size())
                        {
                            return std::unexpected(error::MEMORY_OUT_OF_BOUNDS);
                        }

                        uint8_t value = initial_state.memory[addr];
                        push_u32(static_cast<uint32_t>(value));
                        break;
                    }

                case opcode::LOAD_16_UNSIGNED :
                    {
                        auto address = pop_u32();
                        if (!address.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        uint32_t addr = address.value();
                        if (addr + 1 >= initial_state.memory.size())
                        {
                            return std::unexpected(error::MEMORY_OUT_OF_BOUNDS);
                        }

                        uint16_t value = static_cast<uint16_t>(initial_state.memory[addr])
                                       | (static_cast<uint16_t>(initial_state.memory[addr + 1]) << 8);
                        push_u32(static_cast<uint32_t>(value));
                        break;
                    }

                case opcode::LESS_THAN_SIGNED :
                    {
                        if (initial_state.stack.size() < 2)
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        uint32_t rhs_u32 = initial_state.stack.back();
                        initial_state.stack.pop_back();
                        uint32_t lhs_u32 = initial_state.stack.back();
                        initial_state.stack.pop_back();

                        int32_t rhs = static_cast<int32_t>(rhs_u32);
                        int32_t lhs = static_cast<int32_t>(lhs_u32);

                        initial_state.stack.push_back((lhs < rhs) ? 1u : 0u);
                        break;
                    }

                case opcode::LESS_THAN_UNSIGNED :
                    {
                        if (initial_state.stack.size() < 2)
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        uint32_t rhs = initial_state.stack.back();
                        initial_state.stack.pop_back();
                        uint32_t lhs = initial_state.stack.back();
                        initial_state.stack.pop_back();

                        initial_state.stack.push_back((lhs < rhs) ? 1u : 0u);
                        break;
                    }

                case opcode::LOAD_32 :
                    {
                        auto address = pop_u32();
                        if (!address.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        uint32_t addr = address.value();
                        if (addr + 3 >= initial_state.memory.size())
                        {
                            return std::unexpected(error::MEMORY_OUT_OF_BOUNDS);
                        }

                        uint32_t value
                            = static_cast<uint32_t>(initial_state.memory[addr])
                            | (static_cast<uint32_t>(initial_state.memory[addr + 1]) << 8)
                            | (static_cast<uint32_t>(initial_state.memory[addr + 2]) << 16)
                            | (static_cast<uint32_t>(initial_state.memory[addr + 3]) << 24);
                        push_u32(value);
                        break;
                    }

                case opcode::JUMP :
                    {
                        auto relative_offset = read_i32_le();
                        if (!relative_offset.has_value())
                        {
                            return std::unexpected(error::PC_OUT_OF_RANGE);
                        }

                        auto jump_result = jump_relative(opcode_pc, relative_offset.value());
                        if (!jump_result.has_value())
                        {
                            return std::unexpected(jump_result.error());
                        }

                        break;
                    }

                case opcode::JUMP_IF_ZERO :
                    {
                        auto relative_offset = read_i32_le();
                        if (!relative_offset.has_value())
                        {
                            return std::unexpected(error::PC_OUT_OF_RANGE);
                        }

                        auto condition = pop_u32();
                        if (!condition.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        if (condition.value() == 0)
                        {
                            auto jump_result = jump_relative(opcode_pc, relative_offset.value());
                            if (!jump_result.has_value())
                            {
                                return std::unexpected(jump_result.error());
                            }
                        }

                        break;
                    }

                case opcode::JUMP_IF_NOT_ZERO :
                    {
                        auto relative_offset = read_i32_le();
                        if (!relative_offset.has_value())
                        {
                            return std::unexpected(error::PC_OUT_OF_RANGE);
                        }

                        auto condition = pop_u32();
                        if (!condition.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        if (condition.value() != 0)
                        {
                            auto jump_result = jump_relative(opcode_pc, relative_offset.value());
                            if (!jump_result.has_value())
                            {
                                return std::unexpected(jump_result.error());
                            }
                        }

                        break;
                    }

                case opcode::RET :
                    {
                        auto value = pop_u32();
                        if (!value.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        execution_info info {
                            .pc           = pc,
                            .return_value = value.value(),
                        };

                        return info;
                    }

                case opcode::PRINT :
                    {
                        auto value = pop_u32();
                        if (!value.has_value())
                        {
                            return std::unexpected(error::STACK_UNDERFLOW);
                        }

                        // for now, just print to stdout
                        putchar(static_cast<uint8_t>(value.value()));
                        break;
                    }

                default :
                    {
                        return std::unexpected(error::INVALID_OPCODE);
                    }
            }
        }

        return std::unexpected(error::NON_TERMINATED_PROGRAM);
    }
}
