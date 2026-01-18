#ifndef J1T_UTIL_TIME_HPP
#define J1T_UTIL_TIME_HPP

#include <chrono>
#include <iostream>

namespace j1t::util
{
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

}

#endif
