#include "fake_gpu_data.h"
#include <cstring>
#include <algorithm>

namespace venus_plus {

void generate_fake_physical_device_properties(VkPhysicalDeviceProperties* props) {
    memset(props, 0, sizeof(*props));

    // Basic device info
    props->apiVersion = VK_API_VERSION_1_3;
    props->driverVersion = VK_MAKE_VERSION(1, 0, 0);
    props->vendorID = 0x10DE;  // NVIDIA-like
    props->deviceID = 0x1234;
    props->deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    strncpy(props->deviceName, "Venus Plus Virtual GPU", VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);

    // Limits - set reasonable values for Phase 3
    VkPhysicalDeviceLimits& limits = props->limits;

    // Image/framebuffer limits
    limits.maxImageDimension1D = 16384;
    limits.maxImageDimension2D = 16384;
    limits.maxImageDimension3D = 2048;
    limits.maxImageDimensionCube = 16384;
    limits.maxImageArrayLayers = 2048;
    limits.maxTexelBufferElements = 128 * 1024 * 1024;
    limits.maxUniformBufferRange = 65536;
    limits.maxStorageBufferRange = UINT32_MAX;
    limits.maxPushConstantsSize = 256;
    limits.maxMemoryAllocationCount = 4096;
    limits.maxSamplerAllocationCount = 4000;
    limits.bufferImageGranularity = 131072;
    limits.sparseAddressSpaceSize = 0;  // No sparse support
    limits.maxBoundDescriptorSets = 8;
    limits.maxPerStageDescriptorSamplers = 16;
    limits.maxPerStageDescriptorUniformBuffers = 15;
    limits.maxPerStageDescriptorStorageBuffers = 16;
    limits.maxPerStageDescriptorSampledImages = 128;
    limits.maxPerStageDescriptorStorageImages = 8;
    limits.maxPerStageDescriptorInputAttachments = 8;
    limits.maxPerStageResources = 128;
    limits.maxDescriptorSetSamplers = 96;
    limits.maxDescriptorSetUniformBuffers = 90;
    limits.maxDescriptorSetUniformBuffersDynamic = 8;
    limits.maxDescriptorSetStorageBuffers = 96;
    limits.maxDescriptorSetStorageBuffersDynamic = 8;
    limits.maxDescriptorSetSampledImages = 256;
    limits.maxDescriptorSetStorageImages = 48;
    limits.maxDescriptorSetInputAttachments = 8;

    // Vertex/fragment limits
    limits.maxVertexInputAttributes = 32;
    limits.maxVertexInputBindings = 32;
    limits.maxVertexInputAttributeOffset = 2047;
    limits.maxVertexInputBindingStride = 2048;
    limits.maxVertexOutputComponents = 128;

    // Tessellation limits
    limits.maxTessellationGenerationLevel = 64;
    limits.maxTessellationPatchSize = 32;
    limits.maxTessellationControlPerVertexInputComponents = 128;
    limits.maxTessellationControlPerVertexOutputComponents = 128;
    limits.maxTessellationControlPerPatchOutputComponents = 120;
    limits.maxTessellationControlTotalOutputComponents = 4096;
    limits.maxTessellationEvaluationInputComponents = 128;
    limits.maxTessellationEvaluationOutputComponents = 128;

    // Geometry shader limits
    limits.maxGeometryShaderInvocations = 32;
    limits.maxGeometryInputComponents = 128;
    limits.maxGeometryOutputComponents = 128;
    limits.maxGeometryOutputVertices = 256;
    limits.maxGeometryTotalOutputComponents = 1024;

    // Fragment shader limits
    limits.maxFragmentInputComponents = 128;
    limits.maxFragmentOutputAttachments = 8;
    limits.maxFragmentDualSrcAttachments = 1;
    limits.maxFragmentCombinedOutputResources = 16;

    // Compute shader limits
    limits.maxComputeSharedMemorySize = 49152;
    limits.maxComputeWorkGroupCount[0] = 65535;
    limits.maxComputeWorkGroupCount[1] = 65535;
    limits.maxComputeWorkGroupCount[2] = 65535;
    limits.maxComputeWorkGroupInvocations = 1024;
    limits.maxComputeWorkGroupSize[0] = 1024;
    limits.maxComputeWorkGroupSize[1] = 1024;
    limits.maxComputeWorkGroupSize[2] = 64;

    limits.subPixelPrecisionBits = 8;
    limits.subTexelPrecisionBits = 8;
    limits.mipmapPrecisionBits = 8;
    limits.maxDrawIndexedIndexValue = UINT32_MAX;
    limits.maxDrawIndirectCount = UINT32_MAX;
    limits.maxSamplerLodBias = 15.0f;
    limits.maxSamplerAnisotropy = 16.0f;
    limits.maxViewports = 16;
    limits.maxViewportDimensions[0] = 16384;
    limits.maxViewportDimensions[1] = 16384;
    limits.viewportBoundsRange[0] = -32768.0f;
    limits.viewportBoundsRange[1] = 32767.0f;
    limits.viewportSubPixelBits = 8;
    limits.minMemoryMapAlignment = 64;
    limits.minTexelBufferOffsetAlignment = 16;
    limits.minUniformBufferOffsetAlignment = 256;
    limits.minStorageBufferOffsetAlignment = 16;
    limits.minTexelOffset = -8;
    limits.maxTexelOffset = 7;
    limits.minTexelGatherOffset = -32;
    limits.maxTexelGatherOffset = 31;
    limits.minInterpolationOffset = -0.5f;
    limits.maxInterpolationOffset = 0.5f;
    limits.subPixelInterpolationOffsetBits = 4;
    limits.maxFramebufferWidth = 16384;
    limits.maxFramebufferHeight = 16384;
    limits.maxFramebufferLayers = 2048;
    limits.framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    limits.framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    limits.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    limits.framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    limits.maxColorAttachments = 8;
    limits.sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    limits.sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits.sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    limits.sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT;
    limits.storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    limits.maxSampleMaskWords = 1;
    limits.timestampComputeAndGraphics = VK_TRUE;
    limits.timestampPeriod = 1.0f;
    limits.maxClipDistances = 8;
    limits.maxCullDistances = 8;
    limits.maxCombinedClipAndCullDistances = 8;
    limits.discreteQueuePriorities = 2;
    limits.pointSizeRange[0] = 1.0f;
    limits.pointSizeRange[1] = 64.0f;
    limits.lineWidthRange[0] = 1.0f;
    limits.lineWidthRange[1] = 1.0f;
    limits.pointSizeGranularity = 0.125f;
    limits.lineWidthGranularity = 0.0f;
    limits.strictLines = VK_FALSE;
    limits.standardSampleLocations = VK_TRUE;
    limits.optimalBufferCopyOffsetAlignment = 1;
    limits.optimalBufferCopyRowPitchAlignment = 1;
    limits.nonCoherentAtomSize = 64;

    // Sparse properties (all zero - we don't support sparse)
    props->sparseProperties = {};
}

void generate_fake_physical_device_features(VkPhysicalDeviceFeatures* features) {
    memset(features, 0, sizeof(*features));

    // Enable commonly used features for Phase 3
    features->robustBufferAccess = VK_TRUE;
    features->fullDrawIndexUint32 = VK_TRUE;
    features->imageCubeArray = VK_TRUE;
    features->independentBlend = VK_TRUE;
    features->geometryShader = VK_TRUE;
    features->tessellationShader = VK_TRUE;
    features->sampleRateShading = VK_TRUE;
    features->dualSrcBlend = VK_TRUE;
    features->logicOp = VK_TRUE;
    features->multiDrawIndirect = VK_TRUE;
    features->drawIndirectFirstInstance = VK_TRUE;
    features->depthClamp = VK_TRUE;
    features->depthBiasClamp = VK_TRUE;
    features->fillModeNonSolid = VK_TRUE;
    features->depthBounds = VK_FALSE;
    features->wideLines = VK_FALSE;
    features->largePoints = VK_TRUE;
    features->alphaToOne = VK_TRUE;
    features->multiViewport = VK_TRUE;
    features->samplerAnisotropy = VK_TRUE;
    features->textureCompressionETC2 = VK_FALSE;
    features->textureCompressionASTC_LDR = VK_FALSE;
    features->textureCompressionBC = VK_TRUE;
    features->occlusionQueryPrecise = VK_TRUE;
    features->pipelineStatisticsQuery = VK_TRUE;
    features->vertexPipelineStoresAndAtomics = VK_TRUE;
    features->fragmentStoresAndAtomics = VK_TRUE;
    features->shaderTessellationAndGeometryPointSize = VK_TRUE;
    features->shaderImageGatherExtended = VK_TRUE;
    features->shaderStorageImageExtendedFormats = VK_TRUE;
    features->shaderStorageImageMultisample = VK_TRUE;
    features->shaderStorageImageReadWithoutFormat = VK_TRUE;
    features->shaderStorageImageWriteWithoutFormat = VK_TRUE;
    features->shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
    features->shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    features->shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    features->shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    features->shaderClipDistance = VK_TRUE;
    features->shaderCullDistance = VK_TRUE;
    features->shaderFloat64 = VK_TRUE;
    features->shaderInt64 = VK_TRUE;
    features->shaderInt16 = VK_TRUE;
    features->shaderResourceResidency = VK_FALSE;
    features->shaderResourceMinLod = VK_FALSE;
    features->sparseBinding = VK_FALSE;
    features->sparseResidencyBuffer = VK_FALSE;
    features->sparseResidencyImage2D = VK_FALSE;
    features->sparseResidencyImage3D = VK_FALSE;
    features->sparseResidency2Samples = VK_FALSE;
    features->sparseResidency4Samples = VK_FALSE;
    features->sparseResidency8Samples = VK_FALSE;
    features->sparseResidency16Samples = VK_FALSE;
    features->sparseResidencyAliased = VK_FALSE;
    features->variableMultisampleRate = VK_FALSE;
    features->inheritedQueries = VK_TRUE;
}

void generate_fake_queue_family_properties(uint32_t* pCount, VkQueueFamilyProperties* pProps) {
    const uint32_t family_count = 1;

    if (!pProps) {
        *pCount = family_count;
        return;
    }

    const uint32_t to_return = std::min(*pCount, family_count);
    *pCount = to_return;

    if (to_return >= 1) {
        memset(&pProps[0], 0, sizeof(VkQueueFamilyProperties));
        pProps[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        pProps[0].queueCount = 4;
        pProps[0].timestampValidBits = 64;
        pProps[0].minImageTransferGranularity.width = 1;
        pProps[0].minImageTransferGranularity.height = 1;
        pProps[0].minImageTransferGranularity.depth = 1;
    }
}

void generate_fake_memory_properties(VkPhysicalDeviceMemoryProperties* memProps) {
    memset(memProps, 0, sizeof(*memProps));

    // Memory Type 0: Device local (VRAM)
    memProps->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    memProps->memoryTypes[0].heapIndex = 0;

    // Memory Type 1: Host visible and coherent (system RAM)
    memProps->memoryTypes[1].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    memProps->memoryTypes[1].heapIndex = 1;

    memProps->memoryTypeCount = 2;

    // Memory Heap 0: 8GB device local (VRAM)
    memProps->memoryHeaps[0].size = 8ULL * 1024 * 1024 * 1024;  // 8GB
    memProps->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

    // Memory Heap 1: 16GB host memory
    memProps->memoryHeaps[1].size = 16ULL * 1024 * 1024 * 1024;  // 16GB
    memProps->memoryHeaps[1].flags = 0;

    memProps->memoryHeapCount = 2;
}

} // namespace venus_plus
