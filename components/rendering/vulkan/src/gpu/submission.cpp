#include "../state.hpp"

namespace engine::rendering::vulkan {

void Renderer::Impl::submit() {
    device.logical->resetFences(*completion);
    const auto wait_stage = direct_frame ? vk::PipelineStageFlagBits2::eColorAttachmentOutput
                                         : vk::PipelineStageFlagBits2::eTransfer;
    const vk::SemaphoreSubmitInfo wait(acquisition_semaphore(), 0, wait_stage);
    const vk::SemaphoreSubmitInfo signal(*finished[image_index], 0,
                                         vk::PipelineStageFlagBits2::eAllCommands);
    const vk::CommandBufferSubmitInfo commands(command, 1);
    vk::SubmitInfo2 submit;
    submit.setWaitSemaphoreInfos(wait).setCommandBufferInfos(commands).setSignalSemaphoreInfos(
        signal);
    device.queue.submit2(submit, *completion);
    ++status.frame_count;
    submitted = true;
    acquired = false;
    const vk::PresentInfoKHR present(*finished[image_index], *swapchain, image_index);
    try {
        const auto result = device.queue.presentKHR(present);
        resize = resize || result == vk::Result::eSuboptimalKHR;
    } catch (const vk::OutOfDateKHRError&) {
        resize = true;
    }
}

} // namespace engine::rendering::vulkan
