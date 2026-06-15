#include "vulkanrenderer.h"

#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>

#include "node/value.h"
#include "render/job/shaderjob.h"

#ifdef OAK_HAS_SHADERC
#include <shaderc/shaderc.h>
#endif

namespace olive
{

struct VulkanRenderer::VulkanTexture {
	quint64 id = 0;
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	int width = 0;
	int height = 0;
	int depth = 0;
	PixelFormat format = PixelFormat::INVALID;
	int channel_count = 0;
	VkFormat vk_format = VK_FORMAT_UNDEFINED;
	VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct VulkanRenderer::UniformInfo {
	QString name;
	QString type;
	VkDeviceSize offset;
	VkDeviceSize size;
};

struct VulkanRenderer::VulkanShader {
	quint64 id = 0;
	VkShaderModule vert_module = VK_NULL_HANDLE;
	VkShaderModule frag_module = VK_NULL_HANDLE;
	VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
	QHash<VkFormat, VkPipeline> pipeline_cache;
	bool pipeline_created = false;
	QVector<UniformInfo> uniforms;
	VkDeviceSize ubo_size = 0;
	int sampler_count = 0;
};

static const float kBlitVertices[] = {
	-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
	 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
	 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
	-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
	-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
	 1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
};

VulkanRenderer::VulkanRenderer(QObject *parent) : Renderer(parent)
{
}

VulkanRenderer::~VulkanRenderer()
{
	Destroy();
	PostDestroy();
}

bool VulkanRenderer::Init()
{
	if (instance_ != VK_NULL_HANDLE) {
		return true;
	}
	return CreateInstance() && CreateDevice() && CreateCommandPool() &&
		   CreateDescriptorPool();
}

void VulkanRenderer::PostInit()
{
	if (vertex_buffer_ != VK_NULL_HANDLE) {
		return;
	}
	CreateVertexBuffer();
	CreateLinearSampler();
	CreateNearestSampler();
}

void VulkanRenderer::PostDestroy()
{
}

void VulkanRenderer::DestroyInternal()
{
	if (device_ != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(device_);
	}

	{
		QMutexLocker lock(&mutex_);
		for (auto it = textures_.begin(); it != textures_.end(); ++it) {
			VulkanTexture *tex = it.value();
			if (tex->view != VK_NULL_HANDLE) {
				vkDestroyImageView(device_, tex->view, nullptr);
			}
			if (tex->image != VK_NULL_HANDLE) {
				vkDestroyImage(device_, tex->image, nullptr);
			}
			if (tex->memory != VK_NULL_HANDLE) {
				vkFreeMemory(device_, tex->memory, nullptr);
			}
			delete tex;
		}
		textures_.clear();

		for (auto it = shaders_.begin(); it != shaders_.end(); ++it) {
			VulkanShader *sh = it.value();
			for (auto pit = sh->pipeline_cache.begin(); pit != sh->pipeline_cache.end(); ++pit) {
				if (pit.value() != VK_NULL_HANDLE) {
					vkDestroyPipeline(device_, pit.value(), nullptr);
				}
			}
			sh->pipeline_cache.clear();
			if (sh->pipeline_layout != VK_NULL_HANDLE) {
				vkDestroyPipelineLayout(device_, sh->pipeline_layout, nullptr);
			}
			if (sh->descriptor_layout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device_, sh->descriptor_layout, nullptr);
			}
			if (sh->vert_module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device_, sh->vert_module, nullptr);
			}
			if (sh->frag_module != VK_NULL_HANDLE) {
				vkDestroyShaderModule(device_, sh->frag_module, nullptr);
			}
			delete sh;
		}
		shaders_.clear();
	}

	if (linear_sampler_ != VK_NULL_HANDLE) {
		vkDestroySampler(device_, linear_sampler_, nullptr);
		linear_sampler_ = VK_NULL_HANDLE;
	}
	if (nearest_sampler_ != VK_NULL_HANDLE) {
		vkDestroySampler(device_, nearest_sampler_, nullptr);
		nearest_sampler_ = VK_NULL_HANDLE;
	}

	if (vertex_buffer_ != VK_NULL_HANDLE) {
		vkDestroyBuffer(device_, vertex_buffer_, nullptr);
		vkFreeMemory(device_, vertex_buffer_memory_, nullptr);
		vertex_buffer_ = VK_NULL_HANDLE;
		vertex_buffer_memory_ = VK_NULL_HANDLE;
	}

	for (auto it = render_pass_cache_.begin(); it != render_pass_cache_.end(); ++it) {
		if (it.value() != VK_NULL_HANDLE) {
			vkDestroyRenderPass(device_, it.value(), nullptr);
		}
	}
	render_pass_cache_.clear();

	if (descriptor_pool_ != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
		descriptor_pool_ = VK_NULL_HANDLE;
	}

	if (command_pool_ != VK_NULL_HANDLE) {
		vkDestroyCommandPool(device_, command_pool_, nullptr);
		command_pool_ = VK_NULL_HANDLE;
	}

	if (device_ != VK_NULL_HANDLE) {
		vkDestroyDevice(device_, nullptr);
		device_ = VK_NULL_HANDLE;
	}

	if (instance_ != VK_NULL_HANDLE) {
		vkDestroyInstance(instance_, nullptr);
		instance_ = VK_NULL_HANDLE;
	}
}

bool VulkanRenderer::CreateInstance()
{
	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "Oak Video Editor";
	app_info.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
	app_info.pEngineName = "Oak";
	app_info.engineVersion = VK_MAKE_VERSION(0, 3, 0);
	app_info.apiVersion = VK_API_VERSION_1_2;

	VkInstanceCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;

	VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan instance:" << result;
		return false;
	}
	qDebug() << "Vulkan instance created successfully";
	return true;
}

bool VulkanRenderer::CreateDevice()
{
	VkResult result = vkEnumeratePhysicalDevices(instance_, &physical_device_count_, nullptr);
	if (result != VK_SUCCESS || physical_device_count_ == 0) {
		qWarning() << "No Vulkan-capable physical devices found";
		return false;
	}

	QVector<VkPhysicalDevice> devices(physical_device_count_);
	result = vkEnumeratePhysicalDevices(instance_, &physical_device_count_, devices.data());
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to enumerate Vulkan physical devices:" << result;
		return false;
	}

	// Pick the first device that has a graphics queue family. In the future we
	// should score devices (discrete > integrated > CPU) and check feature support.
	for (VkPhysicalDevice device : devices) {
		vkGetPhysicalDeviceProperties(device, &device_properties_);
		vkGetPhysicalDeviceMemoryProperties(device, &mem_properties_);

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
		QVector<VkQueueFamilyProperties> queue_families(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
											 queue_families.data());

		for (uint32_t i = 0; i < queue_family_count; i++) {
			if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				physical_device_ = device;
				graphics_queue_family_ = i;
				break;
			}
		}

		if (physical_device_ != VK_NULL_HANDLE) {
			break;
		}
	}

	if (physical_device_ == VK_NULL_HANDLE || graphics_queue_family_ == UINT32_MAX) {
		qWarning() << "No Vulkan physical device with a graphics queue found";
		return false;
	}

	float queue_priority = 1.0f;
	VkDeviceQueueCreateInfo queue_create_info = {};
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = graphics_queue_family_;
	queue_create_info.queueCount = 1;
	queue_create_info.pQueuePriorities = &queue_priority;

	VkPhysicalDeviceFeatures device_features = {};

	// No device extensions are required for offscreen rendering. Requesting
	// VK_KHR_swapchain caused failures on headless/CI setups and is unused.
	VkDeviceCreateInfo device_create_info = {};
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.pQueueCreateInfos = &queue_create_info;
	device_create_info.queueCreateInfoCount = 1;
	device_create_info.pEnabledFeatures = &device_features;
	device_create_info.enabledExtensionCount = 0;
	device_create_info.ppEnabledExtensionNames = nullptr;

	result = vkCreateDevice(physical_device_, &device_create_info, nullptr, &device_);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan logical device:" << result;
		return false;
	}

	qDebug() << "Vulkan device created successfully on"
			 << QString::fromUtf8(device_properties_.deviceName);
	vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
	return true;
}

bool VulkanRenderer::CreateCommandPool()
{
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = graphics_queue_family_;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkResult result = vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan command pool:" << result;
		return false;
	}
	return true;
}

bool VulkanRenderer::CreateDescriptorPool()
{
	VkDescriptorPoolSize pool_sizes[2] = {};
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_sizes[0].descriptorCount = kMaxDescriptorSets;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[1].descriptorCount = kMaxDescriptorSets * 8;

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.poolSizeCount = 2;
	pool_info.pPoolSizes = pool_sizes;
	pool_info.maxSets = kMaxDescriptorSets;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	VkResult result = vkCreateDescriptorPool(device_, &pool_info, nullptr,
										 &descriptor_pool_);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan descriptor pool:" << result;
		return false;
	}
	return true;
}

VkRenderPass VulkanRenderer::GetOrCreateRenderPass(VkFormat format, bool clear)
{
	const quint64 key = (static_cast<quint64>(format) << 1) | (clear ? 1ULL : 0ULL);
	auto it = render_pass_cache_.find(key);
	if (it != render_pass_cache_.end()) {
		return it.value();
	}

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR :
											  VK_ATTACHMENT_LOAD_OP_LOAD;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_ref = {};
	color_ref.attachment = 0;
	color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;

	// Explicit dependencies so layout transitions and read-after-write are
	// correctly synchronized. The destination image is brought in by the
	// pipeline barrier before the render pass; here we synchronize the render
	// pass output with whatever stage reads it next.
	VkSubpassDependency dependency = {};
	dependency.srcSubpass = 0;
	dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
							  VK_PIPELINE_STAGE_TRANSFER_BIT;
	dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
							   VK_ACCESS_TRANSFER_READ_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	VkRenderPass render_pass = VK_NULL_HANDLE;
	VkResult result = vkCreateRenderPass(device_, &render_pass_info, nullptr,
									 &render_pass);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan render pass:" << result;
		return VK_NULL_HANDLE;
	}

	render_pass_cache_.insert(key, render_pass);
	return render_pass;
}


bool VulkanRenderer::CreateVertexBuffer()
{
	VkDeviceSize buffer_size = sizeof(kBlitVertices);

	VkBufferCreateInfo buffer_info = {};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = buffer_size;
	buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer staging_buffer;
	VkResult result = vkCreateBuffer(device_, &buffer_info, nullptr, &staging_buffer);
	if (result != VK_SUCCESS) {
		return false;
	}

	VkMemoryRequirements mem_req;
	vkGetBufferMemoryRequirements(device_, staging_buffer, &mem_req);

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_req.size;
	alloc_info.memoryTypeIndex = FindMemoryType(
		mem_req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (alloc_info.memoryTypeIndex == UINT32_MAX) {
		vkDestroyBuffer(device_, staging_buffer, nullptr);
		return false;
	}

	VkDeviceMemory staging_memory;
	result = vkAllocateMemory(device_, &alloc_info, nullptr, &staging_memory);
	if (result != VK_SUCCESS) {
		vkDestroyBuffer(device_, staging_buffer, nullptr);
		return false;
	}

	result = vkBindBufferMemory(device_, staging_buffer, staging_memory, 0);
	if (result != VK_SUCCESS) {
		vkFreeMemory(device_, staging_memory, nullptr);
		vkDestroyBuffer(device_, staging_buffer, nullptr);
		return false;
	}

	void *data;
	vkMapMemory(device_, staging_memory, 0, buffer_size, 0, &data);
	memcpy(data, kBlitVertices, (size_t)buffer_size);
	vkUnmapMemory(device_, staging_memory);

	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	result = vkCreateBuffer(device_, &buffer_info, nullptr, &vertex_buffer_);
	if (result != VK_SUCCESS) {
		vkFreeMemory(device_, staging_memory, nullptr);
		vkDestroyBuffer(device_, staging_buffer, nullptr);
		return false;
	}

	vkGetBufferMemoryRequirements(device_, vertex_buffer_, &mem_req);

	alloc_info.allocationSize = mem_req.size;
	alloc_info.memoryTypeIndex =
		FindMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (alloc_info.memoryTypeIndex == UINT32_MAX) {
		vkDestroyBuffer(device_, vertex_buffer_, nullptr);
		vkFreeMemory(device_, staging_memory, nullptr);
		vkDestroyBuffer(device_, staging_buffer, nullptr);
		return false;
	}

	result = vkAllocateMemory(device_, &alloc_info, nullptr, &vertex_buffer_memory_);
	if (result != VK_SUCCESS) {
		vkDestroyBuffer(device_, vertex_buffer_, nullptr);
		vkFreeMemory(device_, staging_memory, nullptr);
		vkDestroyBuffer(device_, staging_buffer, nullptr);
		return false;
	}

	result = vkBindBufferMemory(device_, vertex_buffer_, vertex_buffer_memory_, 0);
	if (result != VK_SUCCESS) {
		vkFreeMemory(device_, vertex_buffer_memory_, nullptr);
		vkDestroyBuffer(device_, vertex_buffer_, nullptr);
		vkFreeMemory(device_, staging_memory, nullptr);
		vkDestroyBuffer(device_, staging_buffer, nullptr);
		return false;
	}

	// Copy from staging to device local
	VkCommandBuffer cmd = BeginOneTimeCommands();
	VkBufferCopy copy_region = {};
	copy_region.size = buffer_size;
	vkCmdCopyBuffer(cmd, staging_buffer, vertex_buffer_, 1, &copy_region);
	EndOneTimeCommands(cmd);

	vkFreeMemory(device_, staging_memory, nullptr);
	vkDestroyBuffer(device_, staging_buffer, nullptr);

	return true;
}

bool VulkanRenderer::CreateLinearSampler()
{
	VkSamplerCreateInfo sampler_info = {};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.anisotropyEnable = VK_FALSE;
	sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	sampler_info.unnormalizedCoordinates = VK_FALSE;
	sampler_info.compareEnable = VK_FALSE;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler_info.mipLodBias = 0.0f;
	sampler_info.minLod = 0.0f;
	sampler_info.maxLod = 0.0f;

	VkResult result = vkCreateSampler(device_, &sampler_info, nullptr, &linear_sampler_);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan linear sampler:" << result;
		return false;
	}
	return true;
}

bool VulkanRenderer::CreateNearestSampler()
{
	VkSamplerCreateInfo sampler_info = {};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_NEAREST;
	sampler_info.minFilter = VK_FILTER_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.anisotropyEnable = VK_FALSE;
	sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	sampler_info.unnormalizedCoordinates = VK_FALSE;
	sampler_info.compareEnable = VK_FALSE;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.mipLodBias = 0.0f;
	sampler_info.minLod = 0.0f;
	sampler_info.maxLod = 0.0f;

	VkResult result = vkCreateSampler(device_, &sampler_info, nullptr, &nearest_sampler_);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan nearest sampler:" << result;
		return false;
	}
	return true;
}

VkSampler VulkanRenderer::GetSampler(Texture::Interpolation interpolation) const
{
	switch (interpolation) {
	case Texture::kNearest:
		return nearest_sampler_ != VK_NULL_HANDLE ? nearest_sampler_ : linear_sampler_;
	case Texture::kLinear:
	case Texture::kMipmappedLinear:
	default:
		return linear_sampler_;
	}
}

bool VulkanRenderer::CreateStagingBuffer(VkDeviceSize size, VkBuffer *out_buffer,
									   VkDeviceMemory *out_memory)
{
	VkBufferCreateInfo buffer_info = {};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkResult result = vkCreateBuffer(device_, &buffer_info, nullptr, out_buffer);
	if (result != VK_SUCCESS) {
		return false;
	}

	VkMemoryRequirements mem_req;
	vkGetBufferMemoryRequirements(device_, *out_buffer, &mem_req);

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_req.size;
	alloc_info.memoryTypeIndex = FindMemoryType(
		mem_req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (alloc_info.memoryTypeIndex == UINT32_MAX) {
		qWarning() << "Failed to find host-visible memory type for Vulkan staging buffer";
		vkDestroyBuffer(device_, *out_buffer, nullptr);
		return false;
	}

	result = vkAllocateMemory(device_, &alloc_info, nullptr, out_memory);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to allocate Vulkan staging buffer memory:" << result;
		vkDestroyBuffer(device_, *out_buffer, nullptr);
		return false;
	}

	result = vkBindBufferMemory(device_, *out_buffer, *out_memory, 0);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to bind Vulkan staging buffer memory:" << result;
		vkFreeMemory(device_, *out_memory, nullptr);
		vkDestroyBuffer(device_, *out_buffer, nullptr);
		return false;
	}
	return true;
}

void VulkanRenderer::DestroyStagingBuffer(VkBuffer buffer, VkDeviceMemory memory)
{
	if (buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(device_, buffer, nullptr);
	}
	if (memory != VK_NULL_HANDLE) {
		vkFreeMemory(device_, memory, nullptr);
	}
}

VkCommandBuffer VulkanRenderer::BeginOneTimeCommands()
{
	VkCommandBufferAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandPool = command_pool_;
	alloc_info.commandBufferCount = 1;

	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(cmd, &begin_info);
	return cmd;
}

void VulkanRenderer::EndOneTimeCommands(VkCommandBuffer cmd)
{
	vkEndCommandBuffer(cmd);

	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmd;

	vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphics_queue_);

	vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
}

void VulkanRenderer::TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
										   VkImageLayout old_layout,
										   VkImageLayout new_layout)
{
	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = old_layout;
	barrier.newLayout = new_layout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	auto set_transfer = [&]() {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	};
	auto set_shader_read = [&]() {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	};
	auto set_color_attachment = [&]() {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	};

	if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
		barrier.srcAccessMask = 0;
		source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
	} else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			set_shader_read();
		} else if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			set_color_attachment();
		} else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			set_transfer();
		}
	} else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			set_transfer();
		}
	} else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
	} else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		source_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		if (new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
	}

	vkCmdPipelineBarrier(cmd, source_stage, destination_stage, 0, 0, nullptr, 0,
						 nullptr, 1, &barrier);
}


void VulkanRenderer::CopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer,
									   VkImage image, uint32_t width,
									   uint32_t height, uint32_t depth)
{
	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { width, height, depth };

	vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						   1, &region);
}

void VulkanRenderer::CopyImageToBuffer(VkCommandBuffer cmd, VkImage image,
									   VkBuffer buffer, uint32_t width,
									   uint32_t height,
									   uint32_t offset_x, uint32_t offset_y)
{
	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { static_cast<int32_t>(offset_x), static_cast<int32_t>(offset_y), 0 };
	region.imageExtent = { width, height, 1 };

	vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer,
						   1, &region);
}

VkFormat VulkanRenderer::PixelFormatToVkFormat(PixelFormat format,
										   int channel_count) const
{
	switch (format) {
	case PixelFormat::U8:
		switch (channel_count) {
		case 1: return VK_FORMAT_R8_UNORM;
		case 2: return VK_FORMAT_R8G8_UNORM;
		case 3: return VK_FORMAT_R8G8B8_UNORM;
		case 4: return VK_FORMAT_R8G8B8A8_UNORM;
		}
		break;
	case PixelFormat::U16:
		switch (channel_count) {
		case 1: return VK_FORMAT_R16_UNORM;
		case 2: return VK_FORMAT_R16G16_UNORM;
		case 3: return VK_FORMAT_R16G16B16_UNORM;
		case 4: return VK_FORMAT_R16G16B16A16_UNORM;
		}
		break;
	case PixelFormat::F16:
		switch (channel_count) {
		case 1: return VK_FORMAT_R16_SFLOAT;
		case 2: return VK_FORMAT_R16G16_SFLOAT;
		case 3: return VK_FORMAT_R16G16B16_SFLOAT;
		case 4: return VK_FORMAT_R16G16B16A16_SFLOAT;
		}
		break;
	case PixelFormat::F32:
		switch (channel_count) {
		case 1: return VK_FORMAT_R32_SFLOAT;
		case 2: return VK_FORMAT_R32G32_SFLOAT;
		case 3: return VK_FORMAT_R32G32B32_SFLOAT;
		case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
		}
		break;
	case PixelFormat::INVALID:
	case PixelFormat::COUNT:
		break;
	}
	return VK_FORMAT_UNDEFINED;
}

bool VulkanRenderer::IsColorAttachmentSupported(VkFormat format) const
{
	VkFormatProperties props;
	vkGetPhysicalDeviceFormatProperties(physical_device_, format, &props);
	return (props.optimalTilingFeatures &
			VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

VkFormat VulkanRenderer::PickRenderableFormat(PixelFormat format,
										  int channel_count) const
{
	VkFormat candidate = PixelFormatToVkFormat(format, channel_count);
	if (candidate != VK_FORMAT_UNDEFINED && IsColorAttachmentSupported(candidate)) {
		return candidate;
	}

	// 3-channel formats are often unsupported as color attachments; fallback
	// to the 4-channel equivalent.
	if (channel_count == 3) {
		VkFormat rgba = PixelFormatToVkFormat(format, 4);
		if (rgba != VK_FORMAT_UNDEFINED && IsColorAttachmentSupported(rgba)) {
			return rgba;
		}
	}

	return VK_FORMAT_UNDEFINED;
}

int VulkanRenderer::GetVkFormatBytesPerPixel(VkFormat format) const
{
	switch (format) {
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8_SINT:
		return 1;
	case VK_FORMAT_R8G8_UNORM:
		return 2;
	case VK_FORMAT_R8G8B8_UNORM:
		return 3;
	case VK_FORMAT_R8G8B8A8_UNORM:
		return 4;
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16_SFLOAT:
		return 2;
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16_SFLOAT:
		return 4;
	case VK_FORMAT_R16G16B16_UNORM:
	case VK_FORMAT_R16G16B16_SFLOAT:
		return 6;
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return 8;
	case VK_FORMAT_R32_SFLOAT:
		return 4;
	case VK_FORMAT_R32G32_SFLOAT:
		return 8;
	case VK_FORMAT_R32G32B32_SFLOAT:
		return 12;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return 16;
	default:
		// For packed or compressed formats, return 0 and let callers fall back
		// to the requested channel count.
		return 0;
	}
}

float VulkanRenderer::GetFormatMaxAlpha(PixelFormat format) const
{
	if (format == PixelFormat::U8) {
		return 255.0f;
	} else if (format == PixelFormat::U16) {
		return 65535.0f;
	}
	return 1.0f;
}

void VulkanRenderer::CopyPixelsWithChannelConversion(const void *src, void *dst,
												 int width, int height, int depth,
												 int src_channels, int dst_channels,
												 PixelFormat format) const
{
	int src_bpc = VideoParams::GetBytesPerChannel(format);
	int dst_bpc = src_bpc;
	float alpha = GetFormatMaxAlpha(format);

	int plane_pixels = width * height;
	int total_pixels = plane_pixels * depth;

	const char *src_ptr = static_cast<const char *>(src);
	char *dst_ptr = static_cast<char *>(dst);

	for (int i = 0; i < total_pixels; ++i) {
		for (int c = 0; c < dst_channels; ++c) {
			if (c < src_channels) {
				memcpy(dst_ptr + (i * dst_channels + c) * dst_bpc,
					   src_ptr + (i * src_channels + c) * src_bpc,
					   dst_bpc);
			} else {
				// Fill missing channels with 0 (color) or max alpha.
				if (c == 3) {
					if (format == PixelFormat::U8) {
						*reinterpret_cast<uint8_t *>(dst_ptr +
												   (i * dst_channels + c) * dst_bpc) =
							static_cast<uint8_t>(alpha);
					} else if (format == PixelFormat::U16) {
						*reinterpret_cast<uint16_t *>(dst_ptr +
													 (i * dst_channels + c) * dst_bpc) =
							static_cast<uint16_t>(alpha);
					} else if (format == PixelFormat::F16) {
						// Half-float 1.0: 0x3C00
						*reinterpret_cast<uint16_t *>(dst_ptr +
													 (i * dst_channels + c) * dst_bpc) =
							0x3C00;
					} else {
						*reinterpret_cast<float *>(dst_ptr +
													 (i * dst_channels + c) * dst_bpc) =
							alpha;
					}
				} else {
					memset(dst_ptr + (i * dst_channels + c) * dst_bpc, 0, dst_bpc);
				}
			}
		}
	}
}

VkDeviceSize VulkanRenderer::AlignSize(VkDeviceSize size,
									   VkDeviceSize alignment) const
{
	return (size + alignment - 1) & ~(alignment - 1);
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t type_filter,
										VkMemoryPropertyFlags properties) const
{
	for (uint32_t i = 0; i < mem_properties_.memoryTypeCount; i++) {
		if ((type_filter & (1 << i)) &&
			(mem_properties_.memoryTypes[i].propertyFlags & properties) ==
				properties) {
			return i;
		}
	}
	return UINT32_MAX;
}

QVariant VulkanRenderer::CreateNativeTexture(int width, int height, int depth,
										 PixelFormat format, int channel_count,
										 const void *data, int linesize)
{
	QMutexLocker lock(&mutex_);

	VkFormat vk_format = PickRenderableFormat(format, channel_count);
	if (vk_format == VK_FORMAT_UNDEFINED) {
		qWarning() << "Unsupported pixel format for Vulkan texture";
		return QVariant();
	}

	VulkanTexture *tex = new VulkanTexture();
	tex->id = next_texture_id_++;
	tex->width = width;
	tex->height = height;
	tex->depth = depth;
	tex->format = format;
	tex->channel_count = channel_count;
	tex->vk_format = vk_format;

	VkImageCreateInfo image_info = {};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
	image_info.extent.width = static_cast<uint32_t>(width);
	image_info.extent.height = static_cast<uint32_t>(height);
	image_info.extent.depth = static_cast<uint32_t>(depth);
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.format = vk_format;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
					   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
					   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;

	VkResult result = vkCreateImage(device_, &image_info, nullptr, &tex->image);
	if (result != VK_SUCCESS) {
		delete tex;
		return QVariant();
	}

	VkMemoryRequirements mem_req;
	vkGetImageMemoryRequirements(device_, tex->image, &mem_req);

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = mem_req.size;
	alloc_info.memoryTypeIndex =
		FindMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (alloc_info.memoryTypeIndex == UINT32_MAX) {
		qWarning() << "Failed to find device-local memory type for Vulkan image";
		vkDestroyImage(device_, tex->image, nullptr);
		delete tex;
		return QVariant();
	}

	result = vkAllocateMemory(device_, &alloc_info, nullptr, &tex->memory);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to allocate device memory for Vulkan image:" << result;
		vkDestroyImage(device_, tex->image, nullptr);
		delete tex;
		return QVariant();
	}

	result = vkBindImageMemory(device_, tex->image, tex->memory, 0);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to bind Vulkan image memory:" << result;
		vkFreeMemory(device_, tex->memory, nullptr);
		vkDestroyImage(device_, tex->image, nullptr);
		delete tex;
		return QVariant();
	}

	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = tex->image;
	view_info.viewType = depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = vk_format;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.baseMipLevel = 0;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.baseArrayLayer = 0;
	view_info.subresourceRange.layerCount = 1;

	// Single-channel textures are typically intended as grayscale. Replicate the
	// red channel to RGB and force alpha to 1, matching OpenGL's swizzle behavior.
	if (channel_count == 1) {
		view_info.components.r = VK_COMPONENT_SWIZZLE_R;
		view_info.components.g = VK_COMPONENT_SWIZZLE_R;
		view_info.components.b = VK_COMPONENT_SWIZZLE_R;
		view_info.components.a = VK_COMPONENT_SWIZZLE_ONE;
	}

	result = vkCreateImageView(device_, &view_info, nullptr, &tex->view);
	if (result != VK_SUCCESS) {
		vkFreeMemory(device_, tex->memory, nullptr);
		vkDestroyImage(device_, tex->image, nullptr);
		delete tex;
		return QVariant();
	}

	// Upload initial data if provided
	if (data) {
		int cpu_bytes_per_pixel = VideoParams::GetBytesPerPixel(format, channel_count);
		int gpu_bytes_per_pixel = GetVkFormatBytesPerPixel(vk_format);
		if (gpu_bytes_per_pixel == 0) {
			gpu_bytes_per_pixel = cpu_bytes_per_pixel;
		}
		VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * depth *
								  gpu_bytes_per_pixel;
		if (linesize == 0) {
			linesize = width * cpu_bytes_per_pixel;
		}

		VkBuffer staging_buffer;
		VkDeviceMemory staging_memory;
		if (CreateStagingBuffer(image_size, &staging_buffer, &staging_memory)) {
			void *mapped;
			vkMapMemory(device_, staging_memory, 0, image_size, 0, &mapped);
			if (cpu_bytes_per_pixel == gpu_bytes_per_pixel) {
				if (linesize == width * cpu_bytes_per_pixel) {
					memcpy(mapped, data, static_cast<size_t>(image_size));
				} else {
					char *dst = static_cast<char *>(mapped);
					const char *src = static_cast<const char *>(data);
					for (int row = 0; row < height * depth; row++) {
						memcpy(dst + row * width * cpu_bytes_per_pixel,
							   src + row * linesize,
							   static_cast<size_t>(width * cpu_bytes_per_pixel));
					}
				}
			} else {
				// The GPU format has a different channel count than the CPU data
				// (e.g. 3-channel RGB fallback to 4-channel RGBA). Repack the data
				// in the staging buffer so the copy uses the GPU texel layout.
				QByteArray tmp(width * height * depth * cpu_bytes_per_pixel,
							   Qt::Uninitialized);
				if (linesize == width * cpu_bytes_per_pixel) {
					memcpy(tmp.data(), data, static_cast<size_t>(tmp.size()));
				} else {
					char *dst = tmp.data();
					const char *src = static_cast<const char *>(data);
					for (int row = 0; row < height * depth; row++) {
						memcpy(dst + row * width * cpu_bytes_per_pixel,
							   src + row * linesize,
							   static_cast<size_t>(width * cpu_bytes_per_pixel));
					}
				}
				int gpu_channels = gpu_bytes_per_pixel /
								   VideoParams::GetBytesPerChannel(format);
				CopyPixelsWithChannelConversion(tmp.constData(), mapped,
											width, height, depth,
											channel_count, gpu_channels,
											format);
			}
			vkUnmapMemory(device_, staging_memory);

			VkCommandBuffer cmd = BeginOneTimeCommands();
			TransitionImageLayout(cmd, tex->image, VK_IMAGE_LAYOUT_UNDEFINED,
								  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			CopyBufferToImage(cmd, staging_buffer, tex->image,
							  static_cast<uint32_t>(width),
							  static_cast<uint32_t>(height),
							  static_cast<uint32_t>(depth));
			TransitionImageLayout(cmd, tex->image,
								  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
								  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			EndOneTimeCommands(cmd);

			DestroyStagingBuffer(staging_buffer, staging_memory);
			tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	} else {
		VkCommandBuffer cmd = BeginOneTimeCommands();
		TransitionImageLayout(cmd, tex->image, VK_IMAGE_LAYOUT_UNDEFINED,
							  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		EndOneTimeCommands(cmd);
		tex->current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	textures_.insert(tex->id, tex);
	return QVariant::fromValue(tex->id);
}

void VulkanRenderer::DestroyNativeTexture(QVariant texture)
{
	QMutexLocker lock(&mutex_);
	quint64 id = texture.value<quint64>();
	VulkanTexture *tex = textures_.take(id);
	if (!tex) {
		return;
	}
	if (tex->framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(device_, tex->framebuffer, nullptr);
	}
	if (tex->view != VK_NULL_HANDLE) {
		vkDestroyImageView(device_, tex->view, nullptr);
	}
	if (tex->image != VK_NULL_HANDLE) {
		vkDestroyImage(device_, tex->image, nullptr);
	}
	if (tex->memory != VK_NULL_HANDLE) {
		vkFreeMemory(device_, tex->memory, nullptr);
	}
	delete tex;
}

void VulkanRenderer::UploadToTexture(const QVariant &handle,
								 const VideoParams &params, const void *data,
								 int linesize)
{
	QMutexLocker lock(&mutex_);
	quint64 id = handle.value<quint64>();
	VulkanTexture *tex = textures_.value(id);
	if (!tex || !data) {
		return;
	}

	int width = params.effective_width();
	int height = params.effective_height();
	int depth = params.effective_depth();
	int cpu_bytes_per_pixel = VideoParams::GetBytesPerPixel(params.format(),
														params.channel_count());
	int gpu_bytes_per_pixel = GetVkFormatBytesPerPixel(tex->vk_format);
	if (gpu_bytes_per_pixel == 0) {
		gpu_bytes_per_pixel = cpu_bytes_per_pixel;
	}
	VkDeviceSize image_size =
		static_cast<VkDeviceSize>(width) * height * depth * gpu_bytes_per_pixel;
	if (linesize == 0) {
		linesize = width * cpu_bytes_per_pixel;
	}

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;
	if (!CreateStagingBuffer(image_size, &staging_buffer, &staging_memory)) {
		return;
	}

	void *mapped;
	vkMapMemory(device_, staging_memory, 0, image_size, 0, &mapped);
	if (cpu_bytes_per_pixel == gpu_bytes_per_pixel) {
		if (linesize == width * cpu_bytes_per_pixel) {
			memcpy(mapped, data, static_cast<size_t>(image_size));
		} else {
			char *dst = static_cast<char *>(mapped);
			const char *src = static_cast<const char *>(data);
			for (int row = 0; row < height * depth; row++) {
				memcpy(dst + row * width * cpu_bytes_per_pixel,
					   src + row * linesize,
					   static_cast<size_t>(width * cpu_bytes_per_pixel));
			}
		}
	} else {
		QByteArray tmp(width * height * depth * cpu_bytes_per_pixel,
					   Qt::Uninitialized);
		if (linesize == width * cpu_bytes_per_pixel) {
			memcpy(tmp.data(), data, static_cast<size_t>(tmp.size()));
		} else {
			char *dst = tmp.data();
			const char *src = static_cast<const char *>(data);
			for (int row = 0; row < height * depth; row++) {
				memcpy(dst + row * width * cpu_bytes_per_pixel,
					   src + row * linesize,
					   static_cast<size_t>(width * cpu_bytes_per_pixel));
			}
		}
		int gpu_channels = gpu_bytes_per_pixel /
						   VideoParams::GetBytesPerChannel(params.format());
		CopyPixelsWithChannelConversion(tmp.constData(), mapped,
									width, height, depth,
									params.channel_count(), gpu_channels,
									params.format());
	}
	vkUnmapMemory(device_, staging_memory);

	VkCommandBuffer cmd = BeginOneTimeCommands();
	if (tex->current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		TransitionImageLayout(cmd, tex->image, tex->current_layout,
							  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}
	CopyBufferToImage(cmd, staging_buffer, tex->image,
					  static_cast<uint32_t>(width),
					  static_cast<uint32_t>(height),
					  static_cast<uint32_t>(depth));
	TransitionImageLayout(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	EndOneTimeCommands(cmd);

	DestroyStagingBuffer(staging_buffer, staging_memory);
	tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanRenderer::DownloadFromTexture(const QVariant &handle,
									 const VideoParams &params, void *data,
									 int linesize)
{
	QMutexLocker lock(&mutex_);
	quint64 id = handle.value<quint64>();
	VulkanTexture *tex = textures_.value(id);
	if (!tex || !data) {
		return;
	}

	int width = params.effective_width();
	int height = params.effective_height();
	int cpu_bytes_per_pixel = VideoParams::GetBytesPerPixel(params.format(),
														params.channel_count());
	int gpu_bytes_per_pixel = GetVkFormatBytesPerPixel(tex->vk_format);
	if (gpu_bytes_per_pixel == 0) {
		gpu_bytes_per_pixel = cpu_bytes_per_pixel;
	}
	if (linesize == 0) {
		linesize = width * cpu_bytes_per_pixel;
	}
	VkDeviceSize image_size =
		static_cast<VkDeviceSize>(width) * height * gpu_bytes_per_pixel;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;
	if (!CreateStagingBuffer(image_size, &staging_buffer, &staging_memory)) {
		return;
	}

	VkCommandBuffer cmd = BeginOneTimeCommands();
	if (tex->current_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		TransitionImageLayout(cmd, tex->image, tex->current_layout,
							  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	}
	CopyImageToBuffer(cmd, tex->image, staging_buffer,
					  static_cast<uint32_t>(width),
					  static_cast<uint32_t>(height));
	EndOneTimeCommands(cmd);

	void *mapped;
	vkMapMemory(device_, staging_memory, 0, image_size, 0, &mapped);
	if (cpu_bytes_per_pixel == gpu_bytes_per_pixel) {
		if (linesize == width * cpu_bytes_per_pixel) {
			memcpy(data, mapped, static_cast<size_t>(image_size));
		} else {
			char *dst = static_cast<char *>(data);
			const char *src = static_cast<const char *>(mapped);
			for (int row = 0; row < height; row++) {
				memcpy(dst + row * linesize,
					   src + row * width * cpu_bytes_per_pixel,
					   static_cast<size_t>(width * cpu_bytes_per_pixel));
			}
		}
	} else {
		int gpu_channels = gpu_bytes_per_pixel /
						   VideoParams::GetBytesPerChannel(params.format());
		QByteArray tmp(width * height * gpu_bytes_per_pixel, Qt::Uninitialized);
		memcpy(tmp.data(), mapped, static_cast<size_t>(tmp.size()));
		CopyPixelsWithChannelConversion(tmp.constData(), data,
									width, height, 1,
									gpu_channels, params.channel_count(),
									params.format());
		if (linesize != width * cpu_bytes_per_pixel) {
			// Repack from tight CPU layout to caller's stride in-place.
			QByteArray tight(static_cast<const char *>(data),
							 width * height * cpu_bytes_per_pixel);
			char *dst = static_cast<char *>(data);
			for (int row = 0; row < height; row++) {
				memcpy(dst + row * linesize,
					   tight.constData() + row * width * cpu_bytes_per_pixel,
					   static_cast<size_t>(width * cpu_bytes_per_pixel));
			}
		}
	}
	vkUnmapMemory(device_, staging_memory);

	DestroyStagingBuffer(staging_buffer, staging_memory);
	tex->current_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

void VulkanRenderer::Flush()
{
	if (device_ != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(device_);
	}
}

void VulkanRenderer::ClearDestination(olive::Texture *texture, double r, double g,
									  double b, double a)
{
	QMutexLocker lock(&mutex_);

	VkCommandBuffer cmd = BeginOneTimeCommands();
	if (texture) {
		quint64 id = texture->id().value<quint64>();
		VulkanTexture *tex = textures_.value(id);
		if (!tex) {
			EndOneTimeCommands(cmd);
			return;
		}
		if (tex->current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			TransitionImageLayout(cmd, tex->image, tex->current_layout,
								  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		}
		VkClearColorValue clear_color = {};
		clear_color.float32[0] = static_cast<float>(r);
		clear_color.float32[1] = static_cast<float>(g);
		clear_color.float32[2] = static_cast<float>(b);
		clear_color.float32[3] = static_cast<float>(a);
		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;
		vkCmdClearColorImage(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							 &clear_color, 1, &range);
		TransitionImageLayout(cmd, tex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		tex->current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	EndOneTimeCommands(cmd);
}

Color VulkanRenderer::GetPixelFromTexture(olive::Texture *texture,
										const QPointF &pt)
{
	if (!texture) {
		return Color();
	}
	int cpu_bytes_per_pixel = VideoParams::GetBytesPerPixel(texture->format(),
														texture->channel_count());
	QByteArray data(cpu_bytes_per_pixel, Qt::Uninitialized);

	quint64 id = texture->id().value<quint64>();
	QMutexLocker lock(&mutex_);
	VulkanTexture *tex = textures_.value(id);
	if (!tex) {
		return Color();
	}

	uint32_t px = static_cast<uint32_t>(qBound(0.0, pt.x(), double(tex->width - 1)));
	uint32_t py = static_cast<uint32_t>(qBound(0.0, pt.y(), double(tex->height - 1)));

	int gpu_bytes_per_pixel = GetVkFormatBytesPerPixel(tex->vk_format);
	if (gpu_bytes_per_pixel == 0) {
		gpu_bytes_per_pixel = cpu_bytes_per_pixel;
	}

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;
	if (!CreateStagingBuffer(gpu_bytes_per_pixel, &staging_buffer, &staging_memory)) {
		return Color();
	}

	VkCommandBuffer cmd = BeginOneTimeCommands();
	if (tex->current_layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		TransitionImageLayout(cmd, tex->image, tex->current_layout,
							  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	}
	CopyImageToBuffer(cmd, tex->image, staging_buffer, 1, 1, px, py);
	EndOneTimeCommands(cmd);

	void *mapped;
	vkMapMemory(device_, staging_memory, 0, gpu_bytes_per_pixel, 0, &mapped);
	if (cpu_bytes_per_pixel == gpu_bytes_per_pixel) {
		memcpy(data.data(), mapped, static_cast<size_t>(cpu_bytes_per_pixel));
	} else {
		int gpu_channels = gpu_bytes_per_pixel /
						   VideoParams::GetBytesPerChannel(texture->format());
		QByteArray gpu_pixel(gpu_bytes_per_pixel, Qt::Uninitialized);
		memcpy(gpu_pixel.data(), mapped, static_cast<size_t>(gpu_bytes_per_pixel));
		CopyPixelsWithChannelConversion(gpu_pixel.constData(), data.data(),
									1, 1, 1,
									gpu_channels, texture->channel_count(),
									texture->format());
	}
	vkUnmapMemory(device_, staging_memory);

	DestroyStagingBuffer(staging_buffer, staging_memory);
	tex->current_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	return Color::fromData(data.data(), texture->format(),
						   texture->channel_count());
}

// ------------------------------------------------------------------
// Shader compilation (GLSL -> SPIR-V via shaderc)
// ------------------------------------------------------------------

static bool IsSamplerType(const QString &type)
{
	static const QRegularExpression sampler_re(
		QStringLiteral(R"(sampler\d*D|samplerCube|sampler2DArray|sampler3D)"));
	return sampler_re.match(type).hasMatch();
}

QString VulkanRenderer::EnsureGlslVersion450(const QString &glsl) const
{
	QString result = glsl.trimmed();
	if (result.startsWith(QStringLiteral("#version"))) {
		int end = result.indexOf(QChar('\n'));
		QString rest = (end >= 0) ? result.mid(end + 1) : QString();
		return QStringLiteral("#version 450 core\n") + rest;
	}
	return QStringLiteral("#version 450 core\n") + result;
}

QString VulkanRenderer::ConvertGlslToVulkan(const QString &glsl,
													VkShaderStageFlagBits stage)
{
	QString result = glsl;

	// Olive uses the legacy texture2D/texture3D names in some shaders.
	result.replace(QStringLiteral("texture2D("), QStringLiteral("texture("));
	result.replace(QStringLiteral("texture3D("), QStringLiteral("texture("));

	// Ensure fragment shader output has layout
	if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
		result.replace(QStringLiteral("out vec4 frag_color;"),
					   QStringLiteral("layout(location = 0) out vec4 frag_color;"));
		result.replace(QStringLiteral("in vec2 ove_texcoord;"),
					   QStringLiteral("layout(location = 0) in vec2 ove_texcoord;"));
	}

	// Add layout to vertex attributes and varyings
	if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
		result.replace(QStringLiteral("in vec4 a_position;"),
					   QStringLiteral("layout(location = 0) in vec4 a_position;"));
		result.replace(QStringLiteral("in vec2 a_texcoord;"),
					   QStringLiteral("layout(location = 1) in vec2 a_texcoord;"));
		result.replace(QStringLiteral("out vec2 ove_texcoord;"),
					   QStringLiteral("layout(location = 0) out vec2 ove_texcoord;"));
	}

	return result;
}

VkDeviceSize VulkanRenderer::GetStd140Size(const QString &type) const
{
	if (type == QStringLiteral("float")) return 4;
	if (type == QStringLiteral("vec2")) return 8;
	if (type == QStringLiteral("vec3")) return 12;
	if (type == QStringLiteral("vec4")) return 16;
	if (type == QStringLiteral("mat4")) return 64;
	if (type == QStringLiteral("int") || type == QStringLiteral("bool")) return 4;
	return 4;
}

VkDeviceSize VulkanRenderer::GetStd140Alignment(const QString &type) const
{
	if (type == QStringLiteral("float")) return 4;
	if (type == QStringLiteral("vec2")) return 8;
	if (type == QStringLiteral("vec3")) return 16;
	if (type == QStringLiteral("vec4")) return 16;
	if (type == QStringLiteral("mat4")) return 16;
	if (type == QStringLiteral("int") || type == QStringLiteral("bool")) return 4;
	return 4;
}

void VulkanRenderer::ExtractUniforms(const QString &glsl,
										 QVector<UniformInfo> *out_uniforms,
										 QVector<QString> *out_samplers) const
{
	static const QRegularExpression re(
		QStringLiteral(R"(^\s*uniform\s+(\w+)\s+(\w+)\s*;)"),
		QRegularExpression::MultilineOption);

	QRegularExpressionMatchIterator it = re.globalMatch(glsl);
	while (it.hasNext()) {
		QRegularExpressionMatch m = it.next();
		QString type = m.captured(1);
		QString name = m.captured(2);

		if (IsSamplerType(type)) {
			if (out_samplers && !out_samplers->contains(name)) {
				out_samplers->append(name);
			}
		} else if (out_uniforms) {
			bool exists = false;
			for (const UniformInfo &u : *out_uniforms) {
				if (u.name == name) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				UniformInfo info;
				info.name = name;
				info.type = type;
				info.offset = 0;
				info.size = 0;
				out_uniforms->append(info);
			}
		}
	}
}

void VulkanRenderer::ComputeUniformLayout(QVector<UniformInfo> *uniforms) const
{
	VkDeviceSize offset = 0;
	for (UniformInfo &info : *uniforms) {
		VkDeviceSize align = GetStd140Alignment(info.type);
		offset = AlignSize(offset, align);
		info.offset = offset;
		info.size = GetStd140Size(info.type);
		offset += info.size;
	}
}

QString VulkanRenderer::BuildUboBlock(const QVector<UniformInfo> &uniforms) const
{
	if (uniforms.isEmpty()) {
		return QString();
	}

	QString ubo = QStringLiteral("\nlayout(set = 0, binding = 0) uniform UniformBuffer {\n");
	for (const UniformInfo &info : uniforms) {
		ubo += QStringLiteral("    %1 %2;\n").arg(info.type, info.name);
	}
	ubo += QStringLiteral("} ubo;\n\n");
	return ubo;
}

QString VulkanRenderer::RewriteShaderWithUbo(
	const QString &glsl,
	const QVector<UniformInfo> &all_uniforms,
	const QHash<QString, int> &sampler_bindings) const
{
	QString result = glsl;

	static const QRegularExpression re(
		QStringLiteral(R"(^\s*uniform\s+(\w+)\s+(\w+)\s*;)"),
		QRegularExpression::MultilineOption);

	// First pass: replace sampler declarations with explicit bindings and remove
	// plain uniform declarations. Process in reverse so indices stay valid.
	QRegularExpressionMatchIterator it = re.globalMatch(result);
	QVector<QRegularExpressionMatch> matches;
	while (it.hasNext()) {
		matches.append(it.next());
	}
	for (int i = matches.size() - 1; i >= 0; --i) {
		const QRegularExpressionMatch &m = matches[i];
		QString type = m.captured(1);
		QString name = m.captured(2);

		if (IsSamplerType(type)) {
			int binding = sampler_bindings.value(name, -1);
			if (binding >= 0) {
				QString new_decl = QStringLiteral(
					"layout(set = 0, binding = %1) uniform %2 %3;")
						.arg(binding).arg(type, name);
				result.replace(m.capturedStart(), m.capturedLength(), new_decl);
			}
		} else {
			result.remove(m.capturedStart(), m.capturedLength());
		}
	}

	// Second pass: rewrite bare uniform names to ubo.name. The UBO block has not
	// been inserted yet, so we cannot accidentally rewrite names inside it.
	for (const UniformInfo &info : all_uniforms) {
		QString old_name = QStringLiteral("\\b%1\\b").arg(info.name);
		QString new_name = QStringLiteral("ubo.%1").arg(info.name);
		result.replace(QRegularExpression(old_name), new_name);
	}

	// Third pass: insert the shared UBO block after the #version line.
	if (!all_uniforms.isEmpty()) {
		QString ubo = BuildUboBlock(all_uniforms);
		int version_end = result.indexOf(QChar('\n'));
		if (version_end >= 0 && result.startsWith(QStringLiteral("#version"))) {
			result.insert(version_end + 1, ubo);
		} else {
			result.prepend(ubo);
		}
	}

	return result;
}


bool VulkanRenderer::CompileGlslToSpv(const QString &glsl,
									  VkShaderStageFlagBits stage,
									  QByteArray *out_spv)
{
#ifdef OAK_HAS_SHADERC
	shaderc_compiler_t compiler = shaderc_compiler_initialize();
	if (!compiler) {
		qWarning() << "Failed to initialize shaderc compiler";
		return false;
	}

	shaderc_compile_options_t options = shaderc_compile_options_initialize();
	// Bindings are set explicitly by RewriteShaderWithUbo; do not let shaderc
	// reassign them.
	shaderc_compile_options_set_optimization_level(
		options, shaderc_optimization_level_performance);

	shaderc_shader_kind kind;
	switch (stage) {
	case VK_SHADER_STAGE_VERTEX_BIT:
		kind = shaderc_glsl_vertex_shader;
		break;
	case VK_SHADER_STAGE_FRAGMENT_BIT:
		kind = shaderc_glsl_fragment_shader;
		break;
	default:
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);
		return false;
	}

	QString converted = ConvertGlslToVulkan(glsl, stage);
	QByteArray source_utf8 = converted.toUtf8();

	shaderc_compilation_result_t compile_result = shaderc_compile_into_spv(
		compiler, source_utf8.constData(), source_utf8.size(), kind,
		"shader.glsl", "main", options);

	shaderc_compile_options_release(options);

	if (shaderc_result_get_compilation_status(compile_result) !=
		shaderc_compilation_status_success) {
		fprintf(stderr, "shaderc compilation failed: %s\n",
				shaderc_result_get_error_message(compile_result));
		shaderc_result_release(compile_result);
		shaderc_compiler_release(compiler);
		return false;
	}

	size_t spv_size = shaderc_result_get_length(compile_result);
	const char *spv_data = shaderc_result_get_bytes(compile_result);
	out_spv->resize(static_cast<int>(spv_size));
	memcpy(out_spv->data(), spv_data, spv_size);

	shaderc_result_release(compile_result);
	shaderc_compiler_release(compiler);
	return true;
#else
	Q_UNUSED(glsl)
	Q_UNUSED(stage)
	Q_UNUSED(out_spv)
	qWarning() << "shaderc not available, cannot compile GLSL to SPIR-V";
	return false;
#endif
}

QVariant VulkanRenderer::CreateNativeShader(olive::ShaderCode code)
{
	QMutexLocker lock(&mutex_);

	QByteArray vert_spv;
	QByteArray frag_spv;

	QString vert_code = code.vert_code();
	QString frag_code = code.frag_code();

	// Use default shaders if empty
	if (vert_code.isEmpty()) {
		vert_code = FileFunctions::ReadFileAsString(
			QStringLiteral(":/shaders/default.vert"));
	}
	if (frag_code.isEmpty()) {
		frag_code = FileFunctions::ReadFileAsString(
			QStringLiteral(":/shaders/default.frag"));
	}

	// Make sure both stages declare a Vulkan-compatible version.
	vert_code = EnsureGlslVersion450(vert_code);
	frag_code = EnsureGlslVersion450(frag_code);

	// Extract uniforms and samplers from both stages. We build a single shared
	// UBO layout and a single sampler binding table so both vertex and fragment
	// shaders see the same descriptor set.
	QVector<UniformInfo> vert_uniforms;
	QVector<UniformInfo> frag_uniforms;
	QVector<QString> vert_samplers;
	QVector<QString> frag_samplers;
	ExtractUniforms(vert_code, &vert_uniforms, &vert_samplers);
	ExtractUniforms(frag_code, &frag_uniforms, &frag_samplers);

	QVector<UniformInfo> all_uniforms = vert_uniforms;
	for (const UniformInfo &fu : frag_uniforms) {
		bool exists = false;
		for (const UniformInfo &u : all_uniforms) {
			if (u.name == fu.name) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			all_uniforms.append(fu);
		}
	}
	ComputeUniformLayout(&all_uniforms);

	QVector<QString> all_samplers = vert_samplers;
	for (const QString &name : frag_samplers) {
		if (!all_samplers.contains(name)) {
			all_samplers.append(name);
		}
	}
	QHash<QString, int> sampler_bindings;
	for (int i = 0; i < all_samplers.size(); ++i) {
		sampler_bindings[all_samplers[i]] = 1 + i;
	}

	QString converted_vert = RewriteShaderWithUbo(vert_code, all_uniforms, sampler_bindings);
	QString converted_frag = RewriteShaderWithUbo(frag_code, all_uniforms, sampler_bindings);

	converted_vert = ConvertGlslToVulkan(converted_vert, VK_SHADER_STAGE_VERTEX_BIT);
	converted_frag = ConvertGlslToVulkan(converted_frag, VK_SHADER_STAGE_FRAGMENT_BIT);

	if (!CompileGlslToSpv(converted_vert, VK_SHADER_STAGE_VERTEX_BIT, &vert_spv)) {
		fprintf(stderr, "Failed to compile Vulkan vertex shader:\n%s\n",
				converted_vert.toUtf8().constData());
		return QVariant();
	}
	if (!CompileGlslToSpv(converted_frag, VK_SHADER_STAGE_FRAGMENT_BIT, &frag_spv)) {
		fprintf(stderr, "Failed to compile Vulkan fragment shader:\n%s\n",
				converted_frag.toUtf8().constData());
		return QVariant();
	}

	VulkanShader *sh = new VulkanShader();
	sh->id = next_shader_id_++;
	sh->uniforms = all_uniforms;
	sh->sampler_count = all_samplers.size();
	sh->ubo_size = 0;
	for (const UniformInfo &u : all_uniforms) {
		sh->ubo_size = qMax(sh->ubo_size, u.offset + u.size);
	}

	VkShaderModuleCreateInfo vert_info = {};
	vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vert_info.codeSize = static_cast<size_t>(vert_spv.size());
	vert_info.pCode = reinterpret_cast<const uint32_t *>(vert_spv.constData());

	VkResult result = vkCreateShaderModule(device_, &vert_info, nullptr,
										   &sh->vert_module);
	if (result != VK_SUCCESS) {
		delete sh;
		return QVariant();
	}

	VkShaderModuleCreateInfo frag_info = {};
	frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	frag_info.codeSize = static_cast<size_t>(frag_spv.size());
	frag_info.pCode = reinterpret_cast<const uint32_t *>(frag_spv.constData());

	result = vkCreateShaderModule(device_, &frag_info, nullptr, &sh->frag_module);
	if (result != VK_SUCCESS) {
		vkDestroyShaderModule(device_, sh->vert_module, nullptr);
		delete sh;
		return QVariant();
	}

	// Create descriptor set layout: binding 0 = shared UBO, binding 1..N = samplers
	QVector<VkDescriptorSetLayoutBinding> bindings;
	bindings.reserve(1 + all_samplers.size());

	if (!all_uniforms.isEmpty()) {
		VkDescriptorSetLayoutBinding ubo_binding = {};
		ubo_binding.binding = 0;
		ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubo_binding.descriptorCount = 1;
		ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
								 VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings.append(ubo_binding);
	}

	for (int i = 0; i < all_samplers.size(); ++i) {
		VkDescriptorSetLayoutBinding sampler_binding = {};
		sampler_binding.binding = 1 + i;
		sampler_binding.descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_binding.descriptorCount = 1;
		sampler_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
									  VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings.append(sampler_binding);
	}

	VkDescriptorSetLayoutCreateInfo ds_layout_info = {};
	ds_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	ds_layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
	ds_layout_info.pBindings = bindings.constData();

	result = vkCreateDescriptorSetLayout(device_, &ds_layout_info, nullptr,
									 &sh->descriptor_layout);
	if (result != VK_SUCCESS) {
		vkDestroyShaderModule(device_, sh->frag_module, nullptr);
		vkDestroyShaderModule(device_, sh->vert_module, nullptr);
		delete sh;
		return QVariant();
	}

	VkPipelineLayoutCreateInfo layout_info = {};
	layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &sh->descriptor_layout;

	result = vkCreatePipelineLayout(device_, &layout_info, nullptr,
								&sh->pipeline_layout);
	if (result != VK_SUCCESS) {
		vkDestroyDescriptorSetLayout(device_, sh->descriptor_layout, nullptr);
		vkDestroyShaderModule(device_, sh->frag_module, nullptr);
		vkDestroyShaderModule(device_, sh->vert_module, nullptr);
		delete sh;
		return QVariant();
	}

	shaders_.insert(sh->id, sh);
	return QVariant::fromValue(sh->id);
}


void VulkanRenderer::DestroyNativeShader(QVariant shader)
{
	QMutexLocker lock(&mutex_);
	quint64 id = shader.value<quint64>();
	VulkanShader *sh = shaders_.take(id);
	if (!sh) {
		return;
	}
	for (auto it = sh->pipeline_cache.begin(); it != sh->pipeline_cache.end(); ++it) {
		if (it.value() != VK_NULL_HANDLE) {
			vkDestroyPipeline(device_, it.value(), nullptr);
		}
	}
	sh->pipeline_cache.clear();
	if (sh->pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(device_, sh->pipeline_layout, nullptr);
	}
	if (sh->descriptor_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(device_, sh->descriptor_layout, nullptr);
	}
	if (sh->vert_module != VK_NULL_HANDLE) {
		vkDestroyShaderModule(device_, sh->vert_module, nullptr);
	}
	if (sh->frag_module != VK_NULL_HANDLE) {
		vkDestroyShaderModule(device_, sh->frag_module, nullptr);
	}
	delete sh;
}

bool VulkanRenderer::CreatePipelineForShader(VulkanShader *shader,
											 const VideoParams &dest_params,

												 VkFormat render_pass_format)
{
	if (shader->pipeline_cache.contains(render_pass_format)) {
		return true;
	}

	VkPipelineShaderStageCreateInfo vert_stage = {};
	vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vert_stage.module = shader->vert_module;
	vert_stage.pName = "main";

	VkPipelineShaderStageCreateInfo frag_stage = {};
	frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	frag_stage.module = shader->frag_module;
	frag_stage.pName = "main";

	VkPipelineShaderStageCreateInfo stages[] = { vert_stage, frag_stage };

	VkPipelineVertexInputStateCreateInfo vertex_input = {};
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkVertexInputBindingDescription binding_desc = {};
	binding_desc.binding = 0;
	binding_desc.stride = 5 * sizeof(float);
	binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[2] = {};
	attrs[0].binding = 0;
	attrs[0].location = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attrs[0].offset = 0;
	attrs[1].binding = 0;
	attrs[1].location = 1;
	attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
	attrs[1].offset = 3 * sizeof(float);

	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &binding_desc;
	vertex_input.vertexAttributeDescriptionCount = 2;
	vertex_input.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport = {};
	viewport.width = static_cast<float>(dest_params.effective_width());
	viewport.height = static_cast<float>(dest_params.effective_height());
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.extent.width = static_cast<uint32_t>(dest_params.effective_width());
	scissor.extent.height = static_cast<uint32_t>(dest_params.effective_height());

	VkPipelineViewportStateCreateInfo viewport_state = {};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState color_blend = {};
	color_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
								 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	color_blend.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo color_blending = {};
	color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blending.attachmentCount = 1;
	color_blending.pAttachments = &color_blend;

	VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT,
									VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic_state = {};
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.dynamicStateCount = 2;
	dynamic_state.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = stages;
	pipeline_info.pVertexInputState = &vertex_input;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pColorBlendState = &color_blending;
	pipeline_info.pDynamicState = &dynamic_state;
	pipeline_info.layout = shader->pipeline_layout;
	pipeline_info.renderPass = GetOrCreateRenderPass(render_pass_format, false);
	pipeline_info.subpass = 0;

	VkPipeline new_pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1,
											&pipeline_info, nullptr,
											&new_pipeline);
	if (result != VK_SUCCESS) {
		qWarning() << "Failed to create Vulkan graphics pipeline:" << result;
		return false;
	}

	shader->pipeline_cache.insert(render_pass_format, new_pipeline);
	return true;
}

void VulkanRenderer::BlitPass(VulkanShader *shader, VulkanTexture *dest_tex,
							  const QVector<TextureBinding> &bindings,
							  const QByteArray &ubo_data,
							  const VideoParams &destination_params,
							  bool clear_destination, int iteration)
{
	(void)iteration;

	if (!dest_tex) {
		return;
	}

	VkFormat render_pass_format = dest_tex->vk_format;
	VkRenderPass render_pass = GetOrCreateRenderPass(render_pass_format, clear_destination);
	if (render_pass == VK_NULL_HANDLE) {
		return;
	}

	if (!CreatePipelineForShader(shader, destination_params, render_pass_format)) {
		return;
	}

	VkPipeline pipeline = shader->pipeline_cache.value(render_pass_format);

	// Lazily create a per-texture framebuffer. The framebuffer is compatible
	// with any render pass that uses the same format and sample count, so we
	// build it once with the non-clear variant and reuse it.
	if (dest_tex->framebuffer == VK_NULL_HANDLE) {
		VkFramebufferCreateInfo fb_info = {};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.renderPass = GetOrCreateRenderPass(render_pass_format, false);
		fb_info.attachmentCount = 1;
		fb_info.pAttachments = &dest_tex->view;
		fb_info.width = static_cast<uint32_t>(dest_tex->width);
		fb_info.height = static_cast<uint32_t>(dest_tex->height);
		fb_info.layers = 1;
		VkResult fb_result = vkCreateFramebuffer(device_, &fb_info, nullptr,
											 &dest_tex->framebuffer);
		if (fb_result != VK_SUCCESS) {
			qWarning() << "Failed to create Vulkan framebuffer:" << fb_result;
			return;
		}
	}
	VkFramebuffer framebuffer = dest_tex->framebuffer;

	// Create UBO buffer if needed
	VkBuffer ubo_buffer = VK_NULL_HANDLE;
	VkDeviceMemory ubo_memory = VK_NULL_HANDLE;
	if (shader->ubo_size > 0 && !ubo_data.isEmpty()) {
		if (CreateStagingBuffer(shader->ubo_size, &ubo_buffer, &ubo_memory)) {
			void *mapped;
			vkMapMemory(device_, ubo_memory, 0, shader->ubo_size, 0, &mapped);
			memcpy(mapped, ubo_data.constData(), static_cast<size_t>(shader->ubo_size));
			vkUnmapMemory(device_, ubo_memory);
		}
	}

	// Allocate and update descriptors before recording commands so we can bail
	// out cleanly if descriptor allocation fails.
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	VkDescriptorBufferInfo buffer_info = {};
	QVector<VkDescriptorImageInfo> image_infos;
	bool descriptors_needed = (shader->ubo_size > 0 || !bindings.isEmpty());
	if (descriptors_needed) {
		VkDescriptorSetAllocateInfo ds_alloc = {};
		ds_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ds_alloc.descriptorPool = descriptor_pool_;
		ds_alloc.descriptorSetCount = 1;
		ds_alloc.pSetLayouts = &shader->descriptor_layout;

		VkResult result = vkAllocateDescriptorSets(device_, &ds_alloc, &descriptor_set);
		if (result != VK_SUCCESS) {
			qWarning() << "Failed to allocate Vulkan descriptor set:" << result;
			if (ubo_buffer != VK_NULL_HANDLE) {
				DestroyStagingBuffer(ubo_buffer, ubo_memory);
			}
			return;
		}

		QVector<VkWriteDescriptorSet> writes;

		// UBO binding
		if (ubo_buffer != VK_NULL_HANDLE) {
			buffer_info.buffer = ubo_buffer;
			buffer_info.offset = 0;
			buffer_info.range = shader->ubo_size;

			VkWriteDescriptorSet write = {};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = descriptor_set;
			write.dstBinding = 0;
			write.dstArrayElement = 0;
			write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			write.descriptorCount = 1;
			write.pBufferInfo = &buffer_info;
			writes.append(write);
		}

		// Texture samplers binding
		if (!bindings.isEmpty()) {
			image_infos.reserve(qMin(bindings.size(), 16));
			for (int i = 0; i < bindings.size() && i < 16; i++) {
				const TextureBinding &tb = bindings.at(i);
				VkDescriptorImageInfo img_info = {};
				img_info.sampler = GetSampler(tb.interp);
				img_info.imageView = tb.tex ? tb.tex->view : VK_NULL_HANDLE;
				img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				image_infos.append(img_info);

				VkWriteDescriptorSet write = {};
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = descriptor_set;
				write.dstBinding = 1 + i;
				write.dstArrayElement = 0;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				write.descriptorCount = 1;
				write.pImageInfo = &image_infos.last();
				writes.append(write);
			}
		}

		if (!writes.isEmpty()) {
			vkUpdateDescriptorSets(device_, writes.size(), writes.constData(), 0,
								   nullptr);
		}
	}

	VkCommandBuffer cmd = BeginOneTimeCommands();

	if (dest_tex->current_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		TransitionImageLayout(cmd, dest_tex->image, dest_tex->current_layout,
							  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		dest_tex->current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	for (const TextureBinding &tb : bindings) {
		if (tb.tex && tb.tex->current_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			TransitionImageLayout(cmd, tb.tex->image, tb.tex->current_layout,
								  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			tb.tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
	}

	VkRenderPassBeginInfo rp_begin = {};
	rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.renderPass = render_pass;
	rp_begin.framebuffer = framebuffer;
	rp_begin.renderArea.extent.width =
		static_cast<uint32_t>(destination_params.effective_width());
	rp_begin.renderArea.extent.height =
		static_cast<uint32_t>(destination_params.effective_height());

	if (clear_destination) {
		VkClearValue clear_val = {};
		clear_val.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		rp_begin.clearValueCount = 1;
		rp_begin.pClearValues = &clear_val;
	}

	vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

	// Set viewport
	VkViewport viewport = {};
	viewport.width = static_cast<float>(destination_params.effective_width());
	viewport.height = static_cast<float>(destination_params.effective_height());
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	// Set scissor
	VkRect2D scissor = {};
	scissor.extent.width =
		static_cast<uint32_t>(destination_params.effective_width());
	scissor.extent.height =
		static_cast<uint32_t>(destination_params.effective_height());
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Bind vertex buffer
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer_, offsets);

	// Bind descriptor set
	if (descriptor_set != VK_NULL_HANDLE) {
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
								shader->pipeline_layout, 0, 1, &descriptor_set, 0,
								nullptr);
	}

	// Draw
	vkCmdDraw(cmd, 6, 1, 0, 0);

	vkCmdEndRenderPass(cmd);

	// Leave the destination in a shader-readable state so it can be sampled or
	// downloaded without an extra layout transition on the caller side.
	TransitionImageLayout(cmd, dest_tex->image,
						  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
						  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	dest_tex->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	EndOneTimeCommands(cmd);

	if (descriptor_set != VK_NULL_HANDLE) {
		vkFreeDescriptorSets(device_, descriptor_pool_, 1, &descriptor_set);
	}
	if (ubo_buffer != VK_NULL_HANDLE) {
		DestroyStagingBuffer(ubo_buffer, ubo_memory);
	}
}

void VulkanRenderer::Blit(QVariant shader_variant, olive::AcceleratedJob &a_job,
						  olive::Texture *destination,
						  VideoParams destination_params,
						  bool clear_destination)
{
	ShaderJob *s_job = dynamic_cast<ShaderJob *>(&a_job);
	if (!s_job) {
		return;
	}
	ShaderJob job(*s_job);

	quint64 shader_id = shader_variant.value<quint64>();

	// Iterative shaders require ping-pong textures. Create them before locking
	// the renderer mutex because CreateTexture also locks it.
	int real_iteration_count = 1;
	if (job.GetIterationCount() > 1 && !job.GetIterativeInput().isEmpty()) {
		real_iteration_count = job.GetIterationCount();
	}

	struct PingPongTexture {
		TexturePtr texture;
		VulkanTexture *native = nullptr;
	};

	PingPongTexture output_tex, input_tex, final_tex;
	if (real_iteration_count > 1) {
		output_tex.texture = CreateTexture(destination_params);
		if (real_iteration_count > 2) {
			input_tex.texture = CreateTexture(destination_params);
		}
	}

	if (!destination) {
		final_tex.texture = CreateTexture(destination_params);
	}

	QMutexLocker lock(&mutex_);

	VulkanShader *shader = shaders_.value(shader_id);
	if (!shader) {
		return;
	}

	VulkanTexture *dest_tex = nullptr;
	if (destination) {
		quint64 dest_id = destination->id().value<quint64>();
		dest_tex = textures_.value(dest_id);
		if (!dest_tex) {
			return;
		}
	} else {
		quint64 final_id = final_tex.texture->id().value<quint64>();
		final_tex.native = textures_.value(final_id);
		if (!final_tex.native) {
			qWarning() << "VulkanRenderer::Blit failed to resolve temporary destination texture";
			return;
		}
		dest_tex = final_tex.native;
	}

	if (output_tex.texture) {
		quint64 id = output_tex.texture->id().value<quint64>();
		output_tex.native = textures_.value(id);
	}
	if (input_tex.texture) {
		quint64 id = input_tex.texture->id().value<quint64>();
		input_tex.native = textures_.value(id);
	}

	// Collect textures to bind and build base UBO data
	QVector<TextureBinding> base_bindings;
	QByteArray base_ubo_data;
	if (shader->ubo_size > 0) {
		base_ubo_data.resize(static_cast<int>(shader->ubo_size));
		base_ubo_data.fill(0);
	}

	for (auto it = job.GetValues().constBegin(); it != job.GetValues().constEnd();
		 ++it) {
		const NodeValue &value = it.value();
		if (value.type() == NodeValue::kTexture) {
			TexturePtr texture = value.toTexture();
			VulkanTexture *vtex = nullptr;
			if (texture) {
				quint64 tid = texture->id().value<quint64>();
				vtex = textures_.value(tid);
			}
			base_bindings.append({ it.key(), vtex,
								   job.GetInterpolation(it.key()) });
		} else if (!shader->uniforms.isEmpty() && shader->ubo_size > 0) {
			// Find matching uniform
			for (const UniformInfo &u : shader->uniforms) {
				if (u.name != it.key())
					continue;
				char *dst = base_ubo_data.data() + static_cast<int>(u.offset);
				switch (value.type()) {
				case NodeValue::kFloat:
					*reinterpret_cast<float *>(dst) = static_cast<float>(value.toDouble());
					break;
				case NodeValue::kInt:
					*reinterpret_cast<int *>(dst) = static_cast<int>(value.toInt());
					break;
				case NodeValue::kBoolean:
					*reinterpret_cast<int *>(dst) = value.toBool() ? 1 : 0;
					break;
				case NodeValue::kVec2: {
					QVector2D v = value.toVec2();
					memcpy(dst, &v, sizeof(float) * 2);
					break;
				}
				case NodeValue::kVec3: {
					QVector3D v = value.toVec3();
					memcpy(dst, &v, sizeof(float) * 3);
					break;
				}
				case NodeValue::kVec4: {
					QVector4D v = value.toVec4();
					memcpy(dst, &v, sizeof(float) * 4);
					break;
				}
				case NodeValue::kMatrix: {
					QMatrix4x4 m = value.toMatrix();
					memcpy(dst, m.constData(), sizeof(float) * 16);
					break;
				}
				case NodeValue::kColor: {
					Color c = value.toColor();
					float col[4] = { static_cast<float>(c.red()),
									 static_cast<float>(c.green()),
									 static_cast<float>(c.blue()),
									 static_cast<float>(c.alpha()) };
					memcpy(dst, col, sizeof(float) * 4);
					break;
				}
				case NodeValue::kCombo:
					*reinterpret_cast<int *>(dst) = value.toInt();
					break;
				default:
					break;
				}
				break;
			}
		}
	}

	// Handle special uniforms that may not be in job values
	if (shader->ubo_size > 0) {
		for (const UniformInfo &u : shader->uniforms) {
			char *dst = base_ubo_data.data() + static_cast<int>(u.offset);
			if (u.name == QStringLiteral("ove_mvpmat")) {
				QMatrix4x4 m = job.Get(QStringLiteral("ove_mvpmat")).toMatrix();
				memcpy(dst, m.constData(), sizeof(float) * 16);
			} else if (u.name == QStringLiteral("ove_cropmatrix")) {
				QMatrix4x4 m = job.Get(QStringLiteral("ove_cropmatrix")).toMatrix();
				memcpy(dst, m.constData(), sizeof(float) * 16);
			} else if (u.name == QStringLiteral("ove_maintex_alpha")) {
				*reinterpret_cast<int *>(dst) = job.Get(QStringLiteral("ove_maintex_alpha")).toInt();
			} else if (u.name == QStringLiteral("ove_force_opaque")) {
				*reinterpret_cast<int *>(dst) = job.Get(QStringLiteral("ove_force_opaque")).toBool() ? 1 : 0;
			}
		}
	}

	// Set texture-enable flags for shaders that declare uniform bool NAME_enabled.
	if (shader->ubo_size > 0) {
		for (const TextureBinding &tb : base_bindings) {
			QString enabled_name = tb.name + QStringLiteral("_enabled");
			for (const UniformInfo &u : shader->uniforms) {
				if (u.name == enabled_name && u.size == sizeof(int)) {
					char *dst = base_ubo_data.data() + static_cast<int>(u.offset);
					*reinterpret_cast<int *>(dst) = tb.tex ? 1 : 0;
					break;
				}
			}
		}
	}

	for (int iteration = 0; iteration < real_iteration_count; ++iteration) {
		QVector<TextureBinding> pass_bindings = base_bindings;
		QByteArray pass_ubo_data = base_ubo_data;

		// Set iteration number
		if (shader->ubo_size > 0) {
			for (const UniformInfo &u : shader->uniforms) {
				if (u.name == QStringLiteral("ove_iteration")) {
					char *dst = pass_ubo_data.data() + static_cast<int>(u.offset);
					*reinterpret_cast<int *>(dst) = iteration;
					break;
				}
			}
		}

		// Replace iterative input
		VulkanTexture *pass_dest = dest_tex;
		bool pass_clear = clear_destination;
		if (iteration != real_iteration_count - 1) {
			pass_dest = output_tex.native;
			pass_clear = true;
		}

		if (iteration > 0) {
			const QString &iterative_input = job.GetIterativeInput();
			for (TextureBinding &tb : pass_bindings) {
				if (tb.name == iterative_input) {
					tb.tex = input_tex.native;
					break;
				}
			}
		}

		BlitPass(shader, pass_dest, pass_bindings, pass_ubo_data,
				 destination_params, pass_clear, iteration);

		if (iteration != real_iteration_count - 1) {
			std::swap(output_tex, input_tex);
		}
	}
}


} // namespace olive
