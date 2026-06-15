#include "vulkanrenderer.h"

#include <QDebug>
#include <QFile>
#include <QRegularExpression>

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

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

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

	VkPipelineStageFlags source_stage;
	VkPipelineStageFlags destination_stage;

	if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
		new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
			   new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	} else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
			   new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else if (old_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
			   new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		source_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
			   new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
			   new_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		source_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		destination_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	} else {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = 0;
		source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destination_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
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

	result = vkCreateImageView(device_, &view_info, nullptr, &tex->view);
	if (result != VK_SUCCESS) {
		vkFreeMemory(device_, tex->memory, nullptr);
		vkDestroyImage(device_, tex->image, nullptr);
		delete tex;
		return QVariant();
	}

	// Upload initial data if provided
	if (data) {
		int bytes_per_pixel = VideoParams::GetBytesPerPixel(format, channel_count);
		VkDeviceSize image_size = static_cast<VkDeviceSize>(width) * height * depth *
								  bytes_per_pixel;
		if (linesize == 0) {
			linesize = width * bytes_per_pixel;
		}

		VkBuffer staging_buffer;
		VkDeviceMemory staging_memory;
		if (CreateStagingBuffer(image_size, &staging_buffer, &staging_memory)) {
			void *mapped;
			vkMapMemory(device_, staging_memory, 0, image_size, 0, &mapped);
			if (linesize == width * bytes_per_pixel) {
				memcpy(mapped, data, static_cast<size_t>(image_size));
			} else {
				char *dst = static_cast<char *>(mapped);
				const char *src = static_cast<const char *>(data);
				for (int row = 0; row < height * depth; row++) {
					memcpy(dst + row * width * bytes_per_pixel,
						   src + row * linesize,
						   static_cast<size_t>(width * bytes_per_pixel));
				}
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
	int bytes_per_pixel = VideoParams::GetBytesPerPixel(params.format(),
													params.channel_count());
	VkDeviceSize image_size =
		static_cast<VkDeviceSize>(width) * height * depth * bytes_per_pixel;
	if (linesize == 0) {
		linesize = width * bytes_per_pixel;
	}

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;
	if (!CreateStagingBuffer(image_size, &staging_buffer, &staging_memory)) {
		return;
	}

	void *mapped;
	vkMapMemory(device_, staging_memory, 0, image_size, 0, &mapped);
	if (linesize == width * bytes_per_pixel) {
		memcpy(mapped, data, static_cast<size_t>(image_size));
	} else {
		char *dst = static_cast<char *>(mapped);
		const char *src = static_cast<const char *>(data);
		for (int row = 0; row < height * depth; row++) {
			memcpy(dst + row * width * bytes_per_pixel,
				   src + row * linesize,
				   static_cast<size_t>(width * bytes_per_pixel));
		}
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
	int bytes_per_pixel = VideoParams::GetBytesPerPixel(params.format(),
													params.channel_count());
	if (linesize == 0) {
		linesize = width * bytes_per_pixel;
	}
	VkDeviceSize image_size =
		static_cast<VkDeviceSize>(width) * height * bytes_per_pixel;

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
	if (linesize == width * bytes_per_pixel) {
		memcpy(data, mapped, static_cast<size_t>(image_size));
	} else {
		char *dst = static_cast<char *>(data);
		const char *src = static_cast<const char *>(mapped);
		for (int row = 0; row < height; row++) {
			memcpy(dst + row * linesize,
				   src + row * width * bytes_per_pixel,
				   static_cast<size_t>(width * bytes_per_pixel));
		}
	}
	vkUnmapMemory(device_, staging_memory);

	DestroyStagingBuffer(staging_buffer, staging_memory);
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
	int bytes_per_pixel = VideoParams::GetBytesPerPixel(texture->format(),
													texture->channel_count());
	QByteArray data(bytes_per_pixel, Qt::Uninitialized);

	quint64 id = texture->id().value<quint64>();
	QMutexLocker lock(&mutex_);
	VulkanTexture *tex = textures_.value(id);
	if (!tex) {
		return Color();
	}

	uint32_t px = static_cast<uint32_t>(qBound(0.0, pt.x(), double(tex->width - 1)));
	uint32_t py = static_cast<uint32_t>(qBound(0.0, pt.y(), double(tex->height - 1)));

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;
	if (!CreateStagingBuffer(bytes_per_pixel, &staging_buffer, &staging_memory)) {
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
	vkMapMemory(device_, staging_memory, 0, bytes_per_pixel, 0, &mapped);
	memcpy(data.data(), mapped, static_cast<size_t>(bytes_per_pixel));
	vkUnmapMemory(device_, staging_memory);

	DestroyStagingBuffer(staging_buffer, staging_memory);

	return Color::fromData(data.data(), texture->format(),
						   texture->channel_count());
}

// ------------------------------------------------------------------
// Shader compilation (GLSL -> SPIR-V via shaderc)
// ------------------------------------------------------------------

QString VulkanRenderer::ConvertGlslToVulkan(const QString &glsl,
											VkShaderStageFlagBits stage)
{
	QString result = glsl;

	// Replace version 150 with 450 core
	if (result.contains(QStringLiteral("#version 150"))) {
		result.replace(QStringLiteral("#version 150"),
					   QStringLiteral("#version 450 core"));
	}

	// Ensure fragment shader output has layout
	if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
		result.replace(QStringLiteral("out vec4 frag_color;"),
					   QStringLiteral("layout(location = 0) out vec4 frag_color;"));
	}

	// Add layout to vertex attributes
	if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
		result.replace(QStringLiteral("in vec4 a_position;"),
					   QStringLiteral("layout(location = 0) in vec4 a_position;"));
		result.replace(QStringLiteral("in vec2 a_texcoord;"),
					   QStringLiteral("layout(location = 1) in vec2 a_texcoord;"));
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

QString VulkanRenderer::ConvertGlslUniformsToUbo(const QString &glsl,
												 QVector<UniformInfo> *out_uniforms,
												 int *out_sampler_count)
{
	QString result = glsl;
	QVector<UniformInfo> uniforms;

	// Regex to match uniform declarations like: uniform vec4 color;
	QRegularExpression re(QStringLiteral(R"(^\s*uniform\s+(\w+)\s+(\w+)\s*;)"),
						  QRegularExpression::MultilineOption);
	QRegularExpression sampler_re(QStringLiteral(R"(sampler\d*D|samplerCube|sampler2DArray)"));

	int offset = 0;
	int sampler_count = 0;
	QRegularExpressionMatchIterator it = re.globalMatch(result);
	QVector<QRegularExpressionMatch> matches;
	while (it.hasNext()) {
		matches.append(it.next());
	}

	// Process in reverse order so removal doesn't shift indices
	for (int i = matches.size() - 1; i >= 0; --i) {
		const QRegularExpressionMatch &m = matches[i];
		QString type = m.captured(1);
		QString name = m.captured(2);

		if (sampler_re.match(type).hasMatch()) {
			// Assign explicit binding per sampler. Binding 0 is reserved for the
			// UBO, so samplers start at binding 1.
			const int binding = 1 + sampler_count;
			QString new_decl = QStringLiteral(
				"layout(set = 0, binding = %1) uniform %2 %3;")
					.arg(binding).arg(type, name);
			result.replace(m.capturedStart(), m.capturedLength(), new_decl);
			sampler_count++;
			continue;
		}

		UniformInfo info;
		info.name = name;
		info.type = type;
		VkDeviceSize align = GetStd140Alignment(type);
		offset = static_cast<int>(AlignSize(offset, align));
		info.offset = offset;
		info.size = GetStd140Size(type);
		offset += static_cast<int>(info.size);
		uniforms.prepend(info);

		// Remove the original declaration
		result.remove(m.capturedStart(), m.capturedLength());
	}

	if (!uniforms.isEmpty()) {
		// Build UBO block
		QString ubo = QStringLiteral("\nlayout(set = 0, binding = 0) uniform UniformBuffer {\n");
		for (const UniformInfo &info : uniforms) {
			ubo += QStringLiteral("    %1 %2;\n").arg(info.type, info.name);
		}
		ubo += QStringLiteral("} ubo;\n\n");

		// Insert UBO after #version line or at the beginning
		int version_end = result.indexOf(QStringLiteral("\n"));
		if (version_end >= 0 && result.startsWith(QStringLiteral("#version"))) {
			result.insert(version_end + 1, ubo);
		} else {
			result.prepend(ubo);
		}

		// Replace bare uniform names with ubo.name in the rest of the shader
		// We do this carefully to avoid replacing the names inside the UBO block itself
		for (const UniformInfo &info : uniforms) {
			QString old_name = QStringLiteral("\\b%1\\b").arg(info.name);
			QString new_name = QStringLiteral("ubo.%1").arg(info.name);
			result.replace(QRegularExpression(old_name), new_name);
		}
		// Fix double ubo.ubo. that may have been created
		result.replace(QStringLiteral("ubo.ubo."), QStringLiteral("ubo."));
	}

	if (out_uniforms) {
		*out_uniforms = uniforms;
	}
	if (out_sampler_count) {
		*out_sampler_count = sampler_count;
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
	shaderc_compile_options_set_auto_bind_uniforms(options, true);
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
		qWarning() << "shaderc compilation failed:"
				   << shaderc_result_get_error_message(compile_result);
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

	// Convert fragment shader uniforms to UBO before compiling
	QVector<UniformInfo> frag_uniforms;
	int sampler_count = 0;
	QString converted_frag = ConvertGlslUniformsToUbo(frag_code, &frag_uniforms,
												  &sampler_count);

	if (!CompileGlslToSpv(vert_code, VK_SHADER_STAGE_VERTEX_BIT, &vert_spv)) {
		return QVariant();
	}
	if (!CompileGlslToSpv(converted_frag, VK_SHADER_STAGE_FRAGMENT_BIT, &frag_spv)) {
		return QVariant();
	}

	VulkanShader *sh = new VulkanShader();
	sh->id = next_shader_id_++;
	sh->uniforms = frag_uniforms;
	sh->sampler_count = sampler_count;
	sh->ubo_size = 0;
	for (const UniformInfo &u : frag_uniforms) {
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

	// Create descriptor set layout: binding 0 = UBO, binding 1..N = samplers
	QVector<VkDescriptorSetLayoutBinding> bindings;
	bindings.reserve(1 + sampler_count);

	VkDescriptorSetLayoutBinding ubo_binding = {};
	ubo_binding.binding = 0;
	ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	ubo_binding.descriptorCount = 1;
	ubo_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	bindings.append(ubo_binding);

	for (int i = 0; i < sampler_count; ++i) {
		VkDescriptorSetLayoutBinding sampler_binding = {};
		sampler_binding.binding = 1 + i;
		sampler_binding.descriptorType =
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_binding.descriptorCount = 1;
		sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
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

void VulkanRenderer::Blit(QVariant shader_variant, olive::AcceleratedJob &a_job,
							  olive::Texture *destination,
							  VideoParams destination_params,
							  bool clear_destination)
{
	QMutexLocker lock(&mutex_);

	ShaderJob *s_job = dynamic_cast<ShaderJob *>(&a_job);
	if (!s_job) {
		return;
	}
	ShaderJob job(*s_job);

	quint64 shader_id = shader_variant.value<quint64>();
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
	}

	VkFormat render_pass_format = dest_tex ? dest_tex->vk_format : VK_FORMAT_R32G32B32A32_SFLOAT;
	VkRenderPass render_pass = GetOrCreateRenderPass(render_pass_format, clear_destination);
	if (render_pass == VK_NULL_HANDLE) {
		return;
	}

	if (!CreatePipelineForShader(shader, destination_params, render_pass_format)) {
		return;
	}

	VkPipeline pipeline = shader->pipeline_cache.value(render_pass_format);

	// Collect textures to bind and build UBO data
	struct TextureBinding {
		QString name;
		VulkanTexture *tex;
		Texture::Interpolation interp;
	};
	QVector<TextureBinding> bindings;

	QByteArray ubo_data;
	if (shader->ubo_size > 0) {
		ubo_data.resize(static_cast<int>(shader->ubo_size));
		ubo_data.fill(0);
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
			bindings.append({ it.key(), vtex,
							  job.GetInterpolation(it.key()) });
		} else if (!shader->uniforms.isEmpty() && shader->ubo_size > 0) {
			// Find matching uniform
			for (const UniformInfo &u : shader->uniforms) {
				if (u.name != it.key())
					continue;
				char *dst = ubo_data.data() + static_cast<int>(u.offset);
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
			char *dst = ubo_data.data() + static_cast<int>(u.offset);
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
			} else if (u.name == QStringLiteral("ove_iteration")) {
				*reinterpret_cast<int *>(dst) = job.Get(QStringLiteral("ove_iteration")).toInt();
			}
		}
	}

	// Create framebuffer and render pass for this blit
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	if (dest_tex) {
		VkFramebufferCreateInfo fb_info = {};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.renderPass = render_pass;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments = &dest_tex->view;
		fb_info.width = static_cast<uint32_t>(destination_params.effective_width());
		fb_info.height = static_cast<uint32_t>(destination_params.effective_height());
		fb_info.layers = 1;
		vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffer);
	}

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

	VkCommandBuffer cmd = BeginOneTimeCommands();

	if (dest_tex && dest_tex->current_layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
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

	// Create and update descriptor set
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	VkSampler sampler = VK_NULL_HANDLE;
	VkDescriptorBufferInfo buffer_info = {};
	QVector<VkDescriptorImageInfo> image_infos;

	if (shader->ubo_size > 0 || !bindings.isEmpty()) {
		VkDescriptorSetAllocateInfo ds_alloc = {};
		ds_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ds_alloc.descriptorPool = descriptor_pool_;
		ds_alloc.descriptorSetCount = 1;
		ds_alloc.pSetLayouts = &shader->descriptor_layout;

		VkResult result = vkAllocateDescriptorSets(device_, &ds_alloc, &descriptor_set);
		if (result == VK_SUCCESS) {
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
				VkSamplerCreateInfo sampler_info = {};
				sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
				sampler_info.magFilter = VK_FILTER_LINEAR;
				sampler_info.minFilter = VK_FILTER_LINEAR;
				sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
				vkCreateSampler(device_, &sampler_info, nullptr, &sampler);

				image_infos.reserve(qMin(bindings.size(), 16));
				for (int i = 0; i < bindings.size() && i < 16; i++) {
					const TextureBinding &tb = bindings.at(i);
					VkDescriptorImageInfo img_info = {};
					img_info.sampler = sampler;
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

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
								shader->pipeline_layout, 0, 1, &descriptor_set, 0,
								nullptr);
		}
	}

	// Draw
	vkCmdDraw(cmd, 6, 1, 0, 0);

	vkCmdEndRenderPass(cmd);

	EndOneTimeCommands(cmd);

	if (sampler != VK_NULL_HANDLE) {
		vkDestroySampler(device_, sampler, nullptr);
	}
	if (descriptor_set != VK_NULL_HANDLE) {
		vkFreeDescriptorSets(device_, descriptor_pool_, 1, &descriptor_set);
	}
	if (framebuffer != VK_NULL_HANDLE) {
		vkDestroyFramebuffer(device_, framebuffer, nullptr);
	}
	if (ubo_buffer != VK_NULL_HANDLE) {
		DestroyStagingBuffer(ubo_buffer, ubo_memory);
	}
}

} // namespace olive
