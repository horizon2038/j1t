#include <vm/assembler.hpp>
#include <vm/interpreter.hpp>
#include <vm/opcodes.hpp>

#include <jit/engine.hpp>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr int32_t FIXED_SCALE = 4096;

    enum : uint32_t
    {
        L_X = 0,
        L_Y,

        L_C_RE,
        L_C_IM,

        L_Z_RE,
        L_Z_IM,

        L_TMP_RE,
        L_IT,
        L_MAG2,

        L_STEP_RE,
        L_STEP_IM,

        L_WIDTH,
        L_HEIGHT,
        L_MAX_IT,

        L_RE_MIN,
        L_IM_MIN,
        L_ESCAPE,

        L_SHADE,     // 0..palette_len-1
        L_PALETTE_N, // palette_len
        L_PALETTE_LAST,
    };

    auto emit_push_i32(j1t::vm::assembler &assembler, int32_t value) -> void
    {
        assembler.emit_push_u32(static_cast<uint32_t>(value));
    }

    auto emit_mul_fixed(j1t::vm::assembler &assembler) -> void
    {
        assembler.emit_mul();
        emit_push_i32(assembler, FIXED_SCALE);
        assembler.emit_div();
    }

    auto emit_print_char(j1t::vm::assembler &assembler, uint8_t c) -> void
    {
        assembler.emit_push_u32(static_cast<uint32_t>(c));
        assembler.emit_print();
    }

    auto build_mandelbrot_program(uint32_t width, uint32_t height, uint32_t max_iter) -> j1t::vm::program
    {
        if (width < 2 || height < 2)
        {
            throw std::runtime_error("width/height too small");
        }
        if (max_iter == 0)
        {
            throw std::runtime_error("max_iter must be >= 1");
        }

        const int32_t re_min   = static_cast<int32_t>(-2 * FIXED_SCALE);
        const int32_t im_min   = static_cast<int32_t>(-(12 * FIXED_SCALE) / 10); // -1.2

        const int32_t re_range = static_cast<int32_t>(3 * FIXED_SCALE);         // 3.0
        const int32_t im_range = static_cast<int32_t>((24 * FIXED_SCALE) / 10); // 2.4

        const int32_t step_re = static_cast<int32_t>((static_cast<int64_t>(re_range)) / static_cast<int64_t>(width - 1));
        const int32_t step_im = static_cast<int32_t>((static_cast<int64_t>(im_range)) / static_cast<int64_t>(height - 1));

        // escape if |z|^2 > 4.0
        const int32_t escape_threshold           = static_cast<int32_t>(4 * FIXED_SCALE);

        static constexpr const char *PALETTE     = " .:-=+*#%@";
        static constexpr uint32_t    PALETTE_LEN = 10;

        j1t::vm::assembler assembler {};

        // constants -> locals
        emit_push_i32(assembler, step_re);
        assembler.emit_local_set(L_STEP_RE);

        emit_push_i32(assembler, step_im);
        assembler.emit_local_set(L_STEP_IM);

        assembler.emit_push_u32(width);
        assembler.emit_local_set(L_WIDTH);

        assembler.emit_push_u32(height);
        assembler.emit_local_set(L_HEIGHT);

        assembler.emit_push_u32(max_iter);
        assembler.emit_local_set(L_MAX_IT);

        emit_push_i32(assembler, re_min);
        assembler.emit_local_set(L_RE_MIN);

        emit_push_i32(assembler, im_min);
        assembler.emit_local_set(L_IM_MIN);

        emit_push_i32(assembler, escape_threshold);
        assembler.emit_local_set(L_ESCAPE);

        assembler.emit_push_u32(PALETTE_LEN);
        assembler.emit_local_set(L_PALETTE_N);

        assembler.emit_push_u32(PALETTE_LEN - 1);
        assembler.emit_local_set(L_PALETTE_LAST);

        // y = 0
        assembler.emit_push_u32(0u);
        assembler.emit_local_set(L_Y);

        auto label_y_loop    = assembler.create_label();
        auto label_y_done    = assembler.create_label();
        auto label_x_loop    = assembler.create_label();
        auto label_x_done    = assembler.create_label();
        auto label_iter_loop = assembler.create_label();
        auto label_iter_done = assembler.create_label();

        // shade print chain labels
        std::vector<j1t::vm::assembler::label> label_print_palette;
        label_print_palette.resize(PALETTE_LEN);
        for (uint32_t i = 0; i < PALETTE_LEN; ++i)
        {
            label_print_palette[i] = assembler.create_label();
        }
        auto label_print_palette_end = assembler.create_label();

        // y loop
        assembler.bind_label(label_y_loop);

        // if (y == height) break;
        assembler.emit_local_get(L_Y);
        assembler.emit_local_get(L_HEIGHT);
        assembler.emit_eq();
        assembler.emit_jump_if_not_zero(label_y_done);

        // c_im = im_min + y * step_im
        assembler.emit_local_get(L_Y);
        assembler.emit_local_get(L_STEP_IM);
        assembler.emit_mul();
        assembler.emit_local_get(L_IM_MIN);
        assembler.emit_add();
        assembler.emit_local_set(L_C_IM);

        // x = 0
        assembler.emit_push_u32(0u);
        assembler.emit_local_set(L_X);

        // x loop
        assembler.bind_label(label_x_loop);

        // if (x == width) goto x_done;
        assembler.emit_local_get(L_X);
        assembler.emit_local_get(L_WIDTH);
        assembler.emit_eq();
        assembler.emit_jump_if_not_zero(label_x_done);

        // c_re = re_min + x * step_re
        assembler.emit_local_get(L_X);
        assembler.emit_local_get(L_STEP_RE);
        assembler.emit_mul();
        assembler.emit_local_get(L_RE_MIN);
        assembler.emit_add();
        assembler.emit_local_set(L_C_RE);

        // z = 0
        emit_push_i32(assembler, 0);
        assembler.emit_local_set(L_Z_RE);

        emit_push_i32(assembler, 0);
        assembler.emit_local_set(L_Z_IM);

        // it = 0
        assembler.emit_push_u32(0u);
        assembler.emit_local_set(L_IT);

        // iter loop
        assembler.bind_label(label_iter_loop);

        // if (it == max_iter) goto iter_done;
        assembler.emit_local_get(L_IT);
        assembler.emit_local_get(L_MAX_IT);
        assembler.emit_eq();
        assembler.emit_jump_if_not_zero(label_iter_done);

        // mag2 = zr^2 + zi^2  (fixed)
        assembler.emit_local_get(L_Z_RE);
        assembler.emit_local_get(L_Z_RE);
        emit_mul_fixed(assembler); // zr^2

        assembler.emit_local_get(L_Z_IM);
        assembler.emit_local_get(L_Z_IM);
        emit_mul_fixed(assembler); // zi^2

        assembler.emit_add();
        assembler.emit_local_set(L_MAG2);

        // if (escape < mag2) goto iter_done;  (mag2 > 4)
        assembler.emit_local_get(L_ESCAPE);
        assembler.emit_local_get(L_MAG2);
        assembler.emit_op(j1t::vm::opcode::LESS_THAN_SIGNED);
        assembler.emit_jump_if_not_zero(label_iter_done);

        // tmp_re = (zr^2 - zi^2) + c_re
        assembler.emit_local_get(L_Z_RE);
        assembler.emit_local_get(L_Z_RE);
        emit_mul_fixed(assembler); // zr^2

        assembler.emit_local_get(L_Z_IM);
        assembler.emit_local_get(L_Z_IM);
        emit_mul_fixed(assembler); // zi^2

        assembler.emit_sub();
        assembler.emit_local_get(L_C_RE);
        assembler.emit_add();
        assembler.emit_local_set(L_TMP_RE);

        // z_im = (2*zr*zi) + c_im
        assembler.emit_local_get(L_Z_RE);
        assembler.emit_local_get(L_Z_IM);
        emit_mul_fixed(assembler); // zr*zi

        emit_push_i32(assembler, 2);
        assembler.emit_mul(); // 2*zr*zi

        assembler.emit_local_get(L_C_IM);
        assembler.emit_add();
        assembler.emit_local_set(L_Z_IM);

        // z_re = tmp_re
        assembler.emit_local_get(L_TMP_RE);
        assembler.emit_local_set(L_Z_RE);

        // it++
        assembler.emit_local_get(L_IT);
        assembler.emit_push_u32(1u);
        assembler.emit_add();
        assembler.emit_local_set(L_IT);

        assembler.emit_jump(label_iter_loop);

        // iter_done
        assembler.bind_label(label_iter_done);

        // shade = (it * (palette_len - 1)) / max_iter
        assembler.emit_local_get(L_IT);
        assembler.emit_local_get(L_PALETTE_LAST);
        assembler.emit_mul();
        assembler.emit_local_get(L_MAX_IT);
        assembler.emit_div();
        assembler.emit_local_set(L_SHADE);

        // jump table (chain): if shade == i -> label_print_palette[i]
        for (uint32_t i = 0; i < PALETTE_LEN; ++i)
        {
            assembler.emit_local_get(L_SHADE);
            assembler.emit_push_u32(i);
            assembler.emit_eq();
            assembler.emit_jump_if_not_zero(label_print_palette[i]);
        }

        // fallback (should not happen): print last
        assembler.emit_jump(label_print_palette[PALETTE_LEN - 1]);

        for (uint32_t i = 0; i < PALETTE_LEN; ++i)
        {
            assembler.bind_label(label_print_palette[i]);
            emit_print_char(assembler, static_cast<uint8_t>(PALETTE[i]));
            assembler.emit_jump(label_print_palette_end);
        }

        assembler.bind_label(label_print_palette_end);

        // x++
        assembler.emit_local_get(L_X);
        assembler.emit_push_u32(1u);
        assembler.emit_add();
        assembler.emit_local_set(L_X);

        assembler.emit_jump(label_x_loop);

        // x_done: newline
        assembler.bind_label(label_x_done);
        emit_print_char(assembler, static_cast<uint8_t>('\n'));

        // y++
        assembler.emit_local_get(L_Y);
        assembler.emit_push_u32(1u);
        assembler.emit_add();
        assembler.emit_local_set(L_Y);

        assembler.emit_jump(label_y_loop);

        // y_done
        assembler.bind_label(label_y_done);

        // return 0
        assembler.emit_push_u32(0u);
        assembler.emit_ret();

        assembler.finalize();
        return assembler.to_program();
    }
}

#include <chrono>
#include <iostream>

inline constexpr auto calculate_time(auto function) -> decltype(auto)
{
    std::chrono::system_clock::time_point start  = std::chrono::system_clock::now();
    auto                                  result = function();
    std::chrono::system_clock::time_point end    = std::chrono::system_clock::now();

    double elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double elapsed_s  = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

    std::cout << std::format("elapsed time: {:.2f} ns, {:.2f} ms, {:.2f} s\n", elapsed_ns, elapsed_ms, elapsed_s);

    return result;
}

int main(void)
{
    try
    {
        const uint32_t width     = 213;
        const uint32_t height    = 85;
        const uint32_t max_iter  = 1024;

        j1t::vm::program program = build_mandelbrot_program(width, height, max_iter);

        j1t::vm::state state {};
        state.locals.resize(512, 0);
        state.stack.clear();
        state.memory.clear();

        std::printf("Running interpreter...\n");
        j1t::vm::interpreter interpreter {};
        // auto                 result = interpreter.run(program, state);
        auto result = calculate_time(
            [&]()
            {
                return interpreter.run(program, state);
            }
        );
        if (!result)
        {
            std::printf("interpreter error: %s\n", j1t::vm::interpreter::error_to_string(result.error()));
            return 1;
        }

        std::printf("\nRunning JIT...\n");
        j1t::vm::state j_state {};
        j_state.locals.resize(512, 0);
        j_state.stack.clear();
        j_state.memory.clear();

        j1t::jit::engine jit_engine {};
        // auto             jit_result = jit_engine.run(program, j_state);
        auto jit_result = calculate_time(
            [&]()
            {
                return jit_engine.run(program, j_state);
            }
        );
        if (!jit_result)
        {
            std::printf("JIT error: %s\n", j1t::vm::interpreter::error_to_string(jit_result.error()));
            return 1;
        }

        std::printf("\nret=%u\n", result->return_value);
        return 0;
    }
    catch (const std::exception &e)
    {
        std::printf("fatal: %s\n", e.what());
        return 1;
    }
}
