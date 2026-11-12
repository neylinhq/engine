#ifndef ENGINE_FOUNDATION_PARALLEL_PARALLEL_HPP
#define ENGINE_FOUNDATION_PARALLEL_PARALLEL_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <thread>
#include <vector>

namespace engine::foundation::parallel {
namespace detail {

template <typename Function> struct Batch {
    std::size_t count;
    Function& function;
    std::atomic<std::size_t> next = 0;

    void run(std::exception_ptr& error) {
        try {
            for (auto start = next.fetch_add(8); start < count; start = next.fetch_add(8)) {
                for (auto index = start; index < std::min(start + 8, count); ++index) {
                    function(index);
                }
            }
        } catch (...) {
            error = std::current_exception();
        }
    }
};

} // namespace detail

template <typename Function>
void for_each_index(std::size_t count, unsigned worker_count, Function function,
                    std::size_t minimum_count = 128) {
    if (count < minimum_count) {
        for (std::size_t index = 0; index < count; ++index) {
            function(index);
        }
        return;
    }
    const auto workers = std::clamp(worker_count, 1u, 8u);
    detail::Batch<Function> batch{count, function};
    std::vector<std::exception_ptr> errors(workers);
    std::vector<std::jthread> threads;
    for (unsigned worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&, worker] { batch.run(errors[worker]); });
    }
    threads.clear();
    for (const auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
}

} // namespace engine::foundation::parallel

#endif
