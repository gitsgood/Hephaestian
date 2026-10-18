#ifndef TIMEUTILS_H
#define TIMEUTILS_H

#include <chrono>
#include <type_traits>
#include <functional>
#include <string>
#include <print>
#include <utility>

#include "Vulkanised/Logger.h"

namespace Vulkanised
{
	namespace Utilities
	{
		namespace Time
		{
			// Static local time. We can use this to log order of operations, as well as their speed. Initialised at the first line of main and stays that way for the rest of runtime.
			inline std::chrono::steady_clock::time_point getStartTime() noexcept
            {
				static std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
				return startTime;
			}

            template <typename F, typename... Args>
            auto timeIt(std::string_view name, F&& f, Args&&... args) noexcept
            {
                // Determine the return type of f when called with these specific args
                using ReturnType = std::invoke_result_t<F, Args...>;

                auto start = std::chrono::high_resolution_clock::now();

                if constexpr (std::is_void_v<ReturnType>) {
                    // Use std::invoke to handle functions, lambdas, and member functions uniformly
                    std::invoke(std::forward<F>(f), std::forward<Args>(args)...);

                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::nano> elapsed = end - start;
                    //LOGT("{}: took {} �s", name, elapsed.count());
                    std::println("{}{}{}: took {} ns{}", "\033[48;2;0;255;20;38;2;7;35;55m", " TIMING : ", name, elapsed.count(), "\033[0m");
                    //std::cout << name << ": took " << elapsed.count() << " ms\n";
                }
                else {
                    // Capture the return value
                    auto result = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);

                    auto end = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::nano> elapsed = end - start;

                    //LOGT("{}: took {} �s", name, elapsed.count());
                    std::println("{}{}{}: took {} ns{}", "\033[48;2;0;255;20;38;2;7;35;55m", " TIMING : ", name, elapsed.count(), "\033[0m");
                    //std::cout << name << ": took " << elapsed.count() << " ms\n";

                    return result;
                }
            }
		}
	}
}

#endif // !TIMEUTILS_H
