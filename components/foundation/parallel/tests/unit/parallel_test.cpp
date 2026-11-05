#include <engine/foundation/parallel/parallel.hpp>

#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>

int main() {
    std::array<std::atomic<unsigned>, 4096> visits{};
    engine::foundation::parallel::for_each_index(visits.size(),
                                                 [&](std::size_t index) { ++visits[index]; });
    for (const auto& visit : visits) {
        if (visit != 1) {
            throw std::runtime_error("A parallel index was missed or assigned twice");
        }
    }
    bool caught = false;
    try {
        engine::foundation::parallel::for_each_index(visits.size(), [](std::size_t index) {
            if (index == 117) {
                throw std::runtime_error("worker failure");
            }
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    if (!caught) {
        throw std::runtime_error("Worker failure was not propagated");
    }
    std::cout << "Parallel indices and worker exception propagation passed\n";
}
