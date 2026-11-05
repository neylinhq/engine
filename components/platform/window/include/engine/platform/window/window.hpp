#ifndef ENGINE_PLATFORM_WINDOW_WINDOW_HPP
#define ENGINE_PLATFORM_WINDOW_WINDOW_HPP
#include <cstdint>
#include <engine/interaction/input/input.hpp>
#include <engine/platform/window_state/window_state.hpp>
#include <expected>
#include <memory>
#include <string>
#include <vector>
namespace engine::platform::window {
struct Extent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool operator==(const Extent&) const = default;
};
struct Configuration {
    int width = 1600;
    int height = 900;
    std::string title = "Engine";
    bool visible = true;
};
class Window {
  public:
    [[nodiscard]] static std::expected<Window, std::string>
    create(Configuration configuration = {});
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;
    ~Window();
    void poll();
    [[nodiscard]] bool key(interaction::input::Key key) const;
    void wait();
    [[nodiscard]] bool closed() const;
    [[nodiscard]] bool minimized() const;
    [[nodiscard]] bool focused() const;
    [[nodiscard]] Extent framebuffer() const;
    [[nodiscard]] Extent logical_extent() const;
    [[nodiscard]] interaction::input::Snapshot input(interaction::input::Access access = {});
    void capture(bool captured);
    // Borrowed GLFWwindow pointer for a native UI adapter; valid for the window lifetime.
    [[nodiscard]] void* native_handle() const;
    void title(const std::string& title);
    [[nodiscard]] std::expected<void, std::string> toggle_borderless();
    [[nodiscard]] bool borderless() const;
    [[nodiscard]] std::expected<std::vector<const char*>, std::string> instance_extensions() const;
    // VkInstance is a dispatchable pointer; VkSurfaceKHR is an opaque 64-bit token.
    // Returned surface ownership transfers to the Vulkan provider. Window must outlive it.
    [[nodiscard]] std::expected<std::uint64_t, std::string>
    create_surface(std::uintptr_t instance) const;

  private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace engine::platform::window
#endif
