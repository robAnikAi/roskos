#include "vulkan_backend.h"

#include <android/log.h>

#include <cstring>

#define LOG_TAG "roskos-vulkan"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {
struct PushConstants {
    float model[16];
    float normal[12];  // mat3 в std430: три выровненных vec4
};
}  // namespace

void VulkanBackend::destroySwapchain() {
    if (device_ == VK_NULL_HANDLE) return;

    for (VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();

    for (VkImageView view : swapchainViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainViews_.clear();
    swapchainImages_.clear();

    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanBackend::recreateSwapchain() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);
    destroySwapchain();

    std::string error;
    if (!createSwapchain(error) || !createDepthResources(error) || !createFramebuffers(error)) {
        LOGE("Не удалось пересоздать swapchain: %s", error.c_str());
        return;
    }
    LOGI("Swapchain пересоздан: %ux%u", swapchainExtent_.width, swapchainExtent_.height);
}

void VulkanBackend::record(FrameData& frameData, const DonutInstance* instances) {
    vkResetCommandBuffer(commandBuffer_, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commandBuffer_, &beginInfo);

    VkClearValue clearValues[2];
    clearValues[0].color = {{0.03f, 0.04f, 0.06f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[currentImage_];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent_;
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffer_, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapchainExtent_.width);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapchainExtent_;
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);

    const VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer_, offsets);
    vkCmdBindIndexBuffer(commandBuffer_, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descriptorSet_, 0, nullptr);

    for (int i = 0; i < DONUT_COUNT; ++i) {
        float model[16];
        float normal[9];
        mat4Compose(model, instances[i].position, instances[i].rotation, instances[i].scale);
        mat3NormalFromMat4(normal, model);

        PushConstants push{};
        memcpy(push.model, model, sizeof(model));
        for (int column = 0; column < 3; ++column) {
            push.normal[column * 4 + 0] = normal[column * 3 + 0];
            push.normal[column * 4 + 1] = normal[column * 3 + 1];
            push.normal[column * 4 + 2] = normal[column * 3 + 2];
        }

        vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push), &push);
        vkCmdDrawIndexed(commandBuffer_, indexCount_, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(commandBuffer_);
    vkEndCommandBuffer(commandBuffer_);
}

bool VulkanBackend::drawFrame(FrameData& frameData, const DonutInstance* instances) {
    vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);

    memcpy(uniformMapped_, frameData.viewProjection, sizeof(float) * 16);
    float camera[4] = {frameData.cameraPosition[0], frameData.cameraPosition[1],
                       frameData.cameraPosition[2], frameData.time};
    memcpy(static_cast<char*>(uniformMapped_) + 64, camera, sizeof(camera));

    uint32_t imageIndex = 0;
    const VkResult acquire =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        return false;
    }

    vkResetFences(device_, 1, &inFlight_);
    currentImage_ = imageIndex;
    record(frameData, instances);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_;

    if (vkQueueSubmit(queue_, 1, &submitInfo, inFlight_) != VK_SUCCESS) {
        LOGE("vkQueueSubmit не удался");
        return false;
    }

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult present = vkQueuePresentKHR(queue_, &presentInfo);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }
    return true;
}

void VulkanBackend::frame(float time) {
    if (device_ == VK_NULL_HANDLE) return;

    if (swapchainExtent_.width != static_cast<uint32_t>(width_) ||
        swapchainExtent_.height != static_cast<uint32_t>(height_)) {
        recreateSwapchain();
    }
    if (swapchain_ == VK_NULL_HANDLE) return;

    FrameData frameData;
    buildCamera(true, static_cast<float>(width_) / static_cast<float>(height_), time, &frameData);

    DonutInstance instances[DONUT_COUNT];
    updateInstances(time, instances);

    drawFrame(frameData, instances);
}

VulkanBackend::~VulkanBackend() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();

        if (inFlight_ != VK_NULL_HANDLE) vkDestroyFence(device_, inFlight_, nullptr);
        if (renderFinished_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, renderFinished_, nullptr);
        if (imageAvailable_ != VK_NULL_HANDLE) vkDestroySemaphore(device_, imageAvailable_, nullptr);

        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);

        if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        if (descriptorLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);

        if (uniformMapped_ != nullptr) vkUnmapMemory(device_, uniformMemory_);
        if (uniformBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, uniformBuffer_, nullptr);
        if (uniformMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, uniformMemory_, nullptr);

        if (indexBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, indexBuffer_, nullptr);
        if (indexMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, indexMemory_, nullptr);
        if (vertexBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        if (vertexMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, vertexMemory_, nullptr);

        if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
        if (pipelineLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        if (renderPass_ != VK_NULL_HANDLE) vkDestroyRenderPass(device_, renderPass_, nullptr);

        vkDestroyDevice(device_, nullptr);
    }

    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}
