#pragma once

#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <vector>

#include "backend.h"
#include "common.h"

/** Vulkan 1.1: свой swapchain, конвейер и синхронизация. */
class VulkanBackend final : public Backend {
public:
    ~VulkanBackend() override;

    bool init(ANativeWindow* window, int width, int height, bool vsync, int coarseScale,
              bool vertexLighting, std::string& error) override;
    void resize(int width, int height) override;
    void frame(float time) override;
    const char* name() const override { return "Vulkan"; }

private:
    bool createInstance(std::string& error);
    bool createSurface(ANativeWindow* window, std::string& error);
    bool pickPhysicalDevice(std::string& error);
    bool createDevice(std::string& error);
    bool createSwapchain(std::string& error);
    bool createDepthResources(std::string& error);
    bool createRenderPass(std::string& error);
    bool createFramebuffers(std::string& error);
    bool createPipeline(std::string& error);
    bool createGeometryBuffers(std::string& error);
    bool createUniformAndDescriptors(std::string& error);
    bool createCommandBuffers(std::string& error);
    bool createSyncObjects(std::string& error);

    void destroySwapchain();
    void recreateSwapchain();
    void record(FrameData& frameData, const DonutInstance* instances);
    bool drawFrame(FrameData& frameData, const DonutInstance* instances);

    ANativeWindow* window_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    bool vsync_ = true;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;

    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory uniformMemory_ = VK_NULL_HANDLE;
    void* uniformMapped_ = nullptr;

    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;

    uint32_t currentImage_ = 0;

    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
};
