#include "vulkan_backend.h"

#include <android/log.h>

#include <algorithm>
#include <cstring>

#include "vk_shaders.h"

#define LOG_TAG "roskos-vulkan"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

uint32_t findMemoryType(VkPhysicalDevice physical, uint32_t typeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        const bool bit = (typeBits & (1u << i)) != 0;
        const bool flags = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (bit && flags) return i;
    }
    return 0;
}

bool createBuffer(VkDevice device,
                  VkPhysicalDevice physical,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer& buffer,
                  VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(physical, requirements.memoryTypeBits, properties);
    if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) return false;

    return vkBindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS;
}

VkShaderModule createShaderModule(VkDevice device, const unsigned char* code, unsigned int size) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = reinterpret_cast<const uint32_t*>(code);
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}

VkFormat findDepthFormat(VkPhysicalDevice physical) {
    const VkFormat candidates[] = {
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
    };
    for (VkFormat format : candidates) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

}  // namespace

bool VulkanBackend::createInstance(std::string& error) {
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

    auto hasExtension = [&extensions](const char* name) {
        for (const auto& extension : extensions) {
            if (strcmp(extension.extensionName, name) == 0) return true;
        }
        return false;
    };

    const char* required[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    for (const char* name : required) {
        if (!hasExtension(name)) {
            error = std::string("Vulkan-расширение недоступно: ") + name;
            return false;
        }
    }

    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.pApplicationName = "roSkos bench";
    application.applicationVersion = 1;
    application.pEngineName = "roSkos";
    application.engineVersion = 1;
    // Ничего из Vulkan 1.1 не используем: так приложение работает и на Vulkan 1.0.
    application.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = required;

    const VkResult result = vkCreateInstance(&info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        error = "vkCreateInstance вернул " + std::to_string(static_cast<int>(result));
        return false;
    }
    return true;
}

bool VulkanBackend::createSurface(ANativeWindow* window, std::string& error) {
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    info.window = window;
    const VkResult result = vkCreateAndroidSurfaceKHR(instance_, &info, nullptr, &surface_);
    if (result != VK_SUCCESS) {
        error = "vkCreateAndroidSurfaceKHR вернул " + std::to_string(static_cast<int>(result));
        return false;
    }
    return true;
}

bool VulkanBackend::pickPhysicalDevice(std::string& error) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        error = "В системе нет Vulkan-устройств";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (VkPhysicalDevice device : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());

        int graphicsFamily = -1;
        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupported);
            const bool isGraphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (isGraphics && presentSupported) {
                graphicsFamily = static_cast<int>(i);
                break;
            }
        }
        if (graphicsFamily < 0) continue;

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> deviceExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, deviceExtensions.data());

        bool swapchainSupported = false;
        for (const auto& extension : deviceExtensions) {
            if (strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                swapchainSupported = true;
                break;
            }
        }
        if (!swapchainSupported) continue;

        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        LOGI("Выбрано Vulkan-устройство: %s", properties.deviceName);

        physical_ = device;
        queueFamily_ = static_cast<uint32_t>(graphicsFamily);
        return true;
    }

    error = "Не найдено Vulkan-устройство с графической очередью и swapchain";
    return false;
}

bool VulkanBackend::createDevice(std::string& error) {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;

    const VkResult result = vkCreateDevice(physical_, &info, nullptr, &device_);
    if (result != VK_SUCCESS) {
        error = "vkCreateDevice вернул " + std::to_string(static_cast<int>(result));
        return false;
    }
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return true;
}

bool VulkanBackend::createSwapchain(std::string& error) {
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr);
    if (formatCount == 0) {
        error = "У поверхности нет форматов";
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& candidate : formats) {
        if (candidate.format == VK_FORMAT_R8G8B8A8_UNORM &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = candidate;
            break;
        }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = static_cast<uint32_t>(width_);
        extent.height = static_cast<uint32_t>(height_);
        extent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width, extent.width));
        extent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height, extent.height));
    }
    if (extent.width == 0 || extent.height == 0) {
        error = "Нулевой размер поверхности";
        return false;
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0) {
        if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0) {
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        } else if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        } else {
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = compositeAlpha;
    // FIFO = vsync. Без vsync берём MAILBOX (без разрывов), если он есть,
    // иначе IMMEDIATE: только тогда FPS честно показывает потолок GPU.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_) {
        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        if (modeCount > 0) {
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, modes.data());
        }
        for (const auto mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = mode;
                break;
            }
        }
        if (presentMode == VK_PRESENT_MODE_FIFO_KHR) {
            for (const auto mode : modes) {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    presentMode = mode;
                    break;
                }
            }
        }
    }
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = VK_NULL_HANDLE;

    const VkResult result = vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_);
    if (result != VK_SUCCESS) {
        error = "vkCreateSwapchainKHR вернул " + std::to_string(static_cast<int>(result));
        return false;
    }

    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());

    swapchainFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;

    swapchainViews_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = swapchainImages_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &swapchainViews_[i]) != VK_SUCCESS) {
            error = "vkCreateImageView (swapchain) не удался";
            return false;
        }
    }

    return true;
}

bool VulkanBackend::createDepthResources(std::string& error) {
    const VkFormat depthFormat = findDepthFormat(physical_);
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        error = "Нет подходящего формата глубины";
        return false;
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imageInfo, nullptr, &depthImage_) != VK_SUCCESS) {
        error = "vkCreateImage (глубина) не удался";
        return false;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, depthImage_, &requirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(
        physical_, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &allocateInfo, nullptr, &depthMemory_) != VK_SUCCESS) {
        error = "vkAllocateMemory (глубина) не удался";
        return false;
    }
    vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &depthView_) != VK_SUCCESS) {
        error = "vkCreateImageView (глубина) не удался";
        return false;
    }

    return true;
}

bool VulkanBackend::createRenderPass(std::string& error) {
    VkAttachmentDescription attachments[2]{};

    attachments[0].format = swapchainFormat_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    const VkFormat depthFormat = findDepthFormat(physical_);
    attachments[1].format = depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &info, nullptr, &renderPass_) != VK_SUCCESS) {
        error = "vkCreateRenderPass не удался";
        return false;
    }
    return true;
}

bool VulkanBackend::createFramebuffers(std::string& error) {
    framebuffers_.resize(swapchainViews_.size());
    for (size_t i = 0; i < swapchainViews_.size(); ++i) {
        VkImageView attachments[] = {swapchainViews_[i], depthView_};
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = renderPass_;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.width = swapchainExtent_.width;
        info.height = swapchainExtent_.height;
        info.layers = 1;
        if (vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            error = "vkCreateFramebuffer не удался";
            return false;
        }
    }
    return true;
}

bool VulkanBackend::createPipeline(std::string& error) {
    const VkShaderModule vertexModule =
        createShaderModule(device_, vk_donut_vert_spv, vk_donut_vert_spv_len);
    const VkShaderModule fragmentModule =
        createShaderModule(device_, vk_donut_frag_spv, vk_donut_frag_spv_len);
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE) {
        error = "Не удалось создать SPIR-V модули";
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 8 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = 3 * sizeof(float);
    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = 6 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    // Y перевёрнут в проекции (Vulkan NDC), поэтому лицевая грань — CW.
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptorInfo.bindingCount = 1;
    descriptorInfo.pBindings = &layoutBinding;
    if (vkCreateDescriptorSetLayout(device_, &descriptorInfo, nullptr, &descriptorLayout_) != VK_SUCCESS) {
        error = "vkCreateDescriptorSetLayout не удался";
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 28;  // mat4 + mat3 (std430) = 112 байт

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        error = "vkCreatePipelineLayout не удался";
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;

    const VkResult result =
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);

    vkDestroyShaderModule(device_, vertexModule, nullptr);
    vkDestroyShaderModule(device_, fragmentModule, nullptr);

    if (result != VK_SUCCESS) {
        error = "vkCreateGraphicsPipelines вернул " + std::to_string(static_cast<int>(result));
        return false;
    }
    return true;
}

bool VulkanBackend::createGeometryBuffers(std::string& error) {
    const DonutMesh mesh = buildDonut(1.0f, 0.38f, 512, 128);
    indexCount_ = mesh.indexCount();

    const VkMemoryPropertyFlags hostVisible =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (!createBuffer(device_, physical_,
                      static_cast<VkDeviceSize>(mesh.vertices.size() * sizeof(float)),
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostVisible, vertexBuffer_, vertexMemory_)) {
        error = "Не удалось создать vertex buffer";
        return false;
    }
    if (!createBuffer(device_, physical_,
                      static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t)),
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT, hostVisible, indexBuffer_, indexMemory_)) {
        error = "Не удалось создать index buffer";
        return false;
    }

    void* mapped = nullptr;
    vkMapMemory(device_, vertexMemory_, 0, VK_WHOLE_SIZE, 0, &mapped);
    memcpy(mapped, mesh.vertices.data(), mesh.vertices.size() * sizeof(float));
    vkUnmapMemory(device_, vertexMemory_);

    vkMapMemory(device_, indexMemory_, 0, VK_WHOLE_SIZE, 0, &mapped);
    memcpy(mapped, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
    vkUnmapMemory(device_, indexMemory_);

    return true;
}

bool VulkanBackend::createUniformAndDescriptors(std::string& error) {
    const VkDeviceSize size = 80;  // mat4 viewProj + vec4 cameraPos/time
    if (!createBuffer(device_, physical_, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      uniformBuffer_, uniformMemory_)) {
        error = "Не удалось создать uniform buffer";
        return false;
    }
    if (vkMapMemory(device_, uniformMemory_, 0, size, 0, &uniformMapped_) != VK_SUCCESS) {
        error = "vkMapMemory (uniform) не удался";
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        error = "vkCreateDescriptorPool не удался";
        return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorLayout_;
    if (vkAllocateDescriptorSets(device_, &allocateInfo, &descriptorSet_) != VK_SUCCESS) {
        error = "vkAllocateDescriptorSets не удался";
        return false;
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer_;
    bufferInfo.offset = 0;
    bufferInfo.range = size;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    return true;
}

bool VulkanBackend::createCommandBuffers(std::string& error) {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        error = "vkCreateCommandPool не удался";
        return false;
    }

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer_) != VK_SUCCESS) {
        error = "vkAllocateCommandBuffers не удался";
        return false;
    }
    return true;
}

bool VulkanBackend::createSyncObjects(std::string& error) {
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinished_) != VK_SUCCESS ||
        vkCreateFence(device_, &fenceInfo, nullptr, &inFlight_) != VK_SUCCESS) {
        error = "Не удалось создать объекты синхронизации";
        return false;
    }
    return true;
}

bool VulkanBackend::init(ANativeWindow* window, int width, int height, bool vsync,
                         std::string& error) {
    window_ = window;
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    vsync_ = vsync;

    if (!createInstance(error)) return false;
    if (!createSurface(window, error)) return false;
    if (!pickPhysicalDevice(error)) return false;
    if (!createDevice(error)) return false;
    if (!createSwapchain(error)) return false;
    if (!createDepthResources(error)) return false;
    if (!createRenderPass(error)) return false;
    if (!createFramebuffers(error)) return false;
    if (!createPipeline(error)) return false;
    if (!createGeometryBuffers(error)) return false;
    if (!createUniformAndDescriptors(error)) return false;
    if (!createCommandBuffers(error)) return false;
    if (!createSyncObjects(error)) return false;

    LOGI("Vulkan init ok: %ux%u, %u triangles per donut", swapchainExtent_.width,
         swapchainExtent_.height, indexCount_ / 3);
    return true;
}

void VulkanBackend::resize(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
}
