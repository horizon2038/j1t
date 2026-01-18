#ifndef J1T_VM_OPCODES_HPP
#define J1T_VM_OPCODES_HPP

#include <stdint.h>
#include <type_traits>
#include <utility>

namespace j1t::vm
{
    enum class opcode : uint8_t
    {
        NOP = 0x00,

        // stack operations
        PUSH, // imm32
        POP,

        // local
        LOCAL_GET, // u32
        LOCAL_SET, // u32

        // arithmetic
        ADD,
        SUB,
        MUL,
        DIV,

        // comparison
        EQ,
        LESS_THAN_SIGNED,
        LESS_THAN_UNSIGNED,

        // memory
        LOAD_8_UNSIGNED,
        LOAD_16_UNSIGNED,
        LOAD_32,

        STORE_8,

        // control flow
        JUMP,
        JUMP_IF_ZERO,
        JUMP_IF_NOT_ZERO,

        // return
        RET,

        // debug
        PRINT,
        READ_8_UNSIGNED,
    };

    // *explicitly* convert opcode to its raw underlying value
    inline constexpr auto op_to_raw(opcode op) -> std::underlying_type_t<opcode>
    {
        return std::to_underlying(op);
    }
}

#endif
