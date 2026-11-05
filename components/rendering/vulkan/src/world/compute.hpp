#pragma once

#include "../state.hpp"

namespace engine::rendering::vulkan {

using BufferMap = std::map<std::string, std::shared_ptr<Buffer>, std::less<>>;

struct ComputeBinding {
    std::shared_ptr<Buffer> uniform;
    vk::UniqueDescriptorPool pool;
    vk::DescriptorSet descriptors;
    std::vector<std::shared_ptr<Buffer>> buffers;
    std::vector<std::shared_ptr<Image>> images;
};

struct ComputeProgram {
    std::unique_ptr<Program> pipeline;
    std::vector<std::unique_ptr<ComputeBinding>> bindings;
    std::size_t used_bindings = 0;
};

class WorldCompute {
  public:
    Device& device;
    BufferMap& buffers;
    ChunkImages& chunks;

    WorldCompute(Device& device, std::filesystem::path shaders, BufferMap& buffers,
                 ChunkImages& chunks);
    ~WorldCompute();
    WorldCompute(const WorldCompute&) = delete;
    WorldCompute& operator=(const WorldCompute&) = delete;
    void allocate(const std::string& name, vk::DeviceSize bytes, bool clear = true);
    void grow(const std::string& name, vk::DeviceSize bytes);
    void upload(const std::string& name, std::span<const std::byte> bytes,
                vk::DeviceSize offset = 0);
    [[nodiscard]] Bytes read(const std::string& name, vk::DeviceSize offset, vk::DeviceSize count);
    void run(const std::string& name, vk::Extent3D groups, const UniformValues& values);
    void run_indirect(const std::string& name, const std::string& arguments,
                      const UniformValues& values);
    void synchronize();
    void cancel();
    [[nodiscard]] vk::CommandBuffer command();
    void attach(vk::CommandBuffer command);
    void detach();
    // The caller must complete any detached frame submission before releasing its slots.
    void release_completed();
    [[nodiscard]] const std::map<std::string, std::uint64_t>& execution_counts() const;
    [[nodiscard]] std::uint64_t allocated_bytes() const;
    [[nodiscard]] std::uint64_t binding_allocations() const;

  private:
    std::filesystem::path shaders_;
    vk::UniqueCommandPool pool_;
    vk::CommandBuffer internal_command_;
    vk::CommandBuffer external_command_;
    vk::UniqueFence fence_;
    bool recording_ = false;
    bool pending_ = false;
    std::map<std::string, ComputeProgram, std::less<>> programs_;
    std::vector<std::shared_ptr<Buffer>> retained_;
    std::map<std::string, std::uint64_t> executions_;
    [[nodiscard]] ComputeProgram& program(const std::string& name);
    [[nodiscard]] ComputeBinding& bind(ComputeProgram& program, const UniformValues& values);
};

[[nodiscard]] std::unique_ptr<ComputeBinding> make_compute_binding(WorldCompute& context,
                                                                   Program& program);
void update_compute_binding(WorldCompute& context, Program& program, ComputeBinding& binding,
                            const UniformValues& values);

} // namespace engine::rendering::vulkan
