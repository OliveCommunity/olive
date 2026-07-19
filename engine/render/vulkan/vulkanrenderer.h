/***************************************************************************

  Oak Video Editor
  Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***************************************************************************/

#ifndef OAK_VULKANRENDERER_H
#define OAK_VULKANRENDERER_H

#include <vulkan/vulkan.h>

#include <QMutex>
#include <QObject>
#include <QVariant>

#include "render/renderer.h"

namespace olive
{

class VulkanRenderer : public Renderer {
	Q_OBJECT
public:
	// Creates a renderer object; Vulkan objects are created lazily in Init().
	explicit VulkanRenderer(QObject *parent = nullptr);
	// Releases Vulkan objects through the normal Renderer destruction path.
	virtual ~VulkanRenderer() override;

	// Creates the Vulkan instance, logical device, command pool, and descriptor
	// pool required for offscreen rendering.
	virtual bool init() override;
	// Creates reusable GPU resources that require a fully initialized device.
	virtual void post_init() override;
	// Reserved for symmetry with OpenGLRenderer; Vulkan cleanup is handled by
	// DestroyInternal().
	virtual void post_destroy() override;

	// Clears either a texture render target or the currently bound output target.
	virtual void clear_destination(olive::Texture *texture = nullptr,
								  double r = 0.0, double g = 0.0,
								  double b = 0.0, double a = 0.0) override;

	// Compiles GLSL to SPIR-V, creates shader modules, and prepares descriptor
	// metadata for later blits.
	virtual QVariant create_native_shader(olive::ShaderCode code) override;
	// Destroys shader modules, descriptor layout, pipeline layout, and cached
	// pipelines associated with a shader handle.
	virtual void destroy_native_shader(QVariant shader) override;

	// Uploads CPU pixel data to a Vulkan image via a staging buffer.
	virtual void upload_to_texture(const QVariant &handle,
								 const VideoParams &params, const void *data,
								 int linesize) override;
	// Downloads a Vulkan image to CPU memory via a staging buffer.
	virtual void download_from_texture(const QVariant &handle,
									 const VideoParams &params, void *data,
									 int linesize) override;

	// Waits for outstanding device work to complete.
	virtual void flush() override;

	virtual bool is_vulkan() const override
	{
		return true;
	}

	// Reads a single texture pixel using a one-pixel transfer readback.
	virtual Color get_pixel_from_texture(olive::Texture *texture,
									  const QPointF &pt) override;

	bool is_available() const
	{
		return device_ != VK_NULL_HANDLE;
	}

protected:
	// Runs one or more fullscreen shader passes into the destination texture.
	virtual void blit(QVariant shader, olive::AcceleratedJob &job,
					  olive::Texture *destination,
					  VideoParams destination_params,
					  bool clear_destination) override;
	// Creates a Vulkan image/view/memory bundle and optionally uploads initial
	// pixel data.
	virtual QVariant create_native_texture(int width, int height, int depth,
										 PixelFormat format, int channel_count,
										 const void *data = nullptr,
										 int linesize = 0) override;
	// Releases a Vulkan texture bundle.
	virtual void destroy_native_texture(QVariant texture) override;
	// Releases all Vulkan device resources owned by this renderer.
	virtual void destroy_internal() override;

private:
	struct VulkanTexture;
	struct VulkanShader;
	struct UniformInfo;
	struct StagingBuffer;

	// Creates the Vulkan instance used for all offscreen work.
	bool create_instance();
	// Creates the debug messenger when validation layers are available.
	bool create_debug_messenger();
	// Destroys the debug messenger before the instance is destroyed.
	void destroy_debug_messenger();
	// Validation layer callback; logs errors/warnings so synchronization issues
	// are visible before they become GPU hangs.
	static VKAPI_ATTR VkBool32 VKAPI_CALL
	debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
				  VkDebugUtilsMessageTypeFlagsEXT message_type,
				  const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
				  void *p_user_data);
	// Chooses a graphics-capable physical device and creates the logical device.
	bool create_device();
	// Creates a command pool for short-lived command buffers.
	bool create_command_pool();
	// Creates the descriptor pool used for per-blit UBO/sampler sets.
	bool create_descriptor_pool();
	// Uploads the fullscreen quad vertex buffer used by BlitPass().
	bool create_vertex_buffer();
	// Creates the persistent linear sampler.
	bool create_linear_sampler();
	// Creates the persistent nearest-neighbor sampler.
	bool create_nearest_sampler();
	// Returns the persistent sampler matching the requested interpolation mode.
	VkSampler get_sampler(Texture::Interpolation interpolation) const;
	// Allocates a host-visible staging buffer for upload/download transfers.
	bool create_staging_buffer(VkDeviceSize size, VkBuffer *out_buffer,
							 VkDeviceMemory *out_memory);
	// Destroys a staging buffer pair allocated by CreateStagingBuffer().
	void destroy_staging_buffer(VkBuffer buffer, VkDeviceMemory memory);

	// Begins a one-shot command buffer and records it immediately.
	VkCommandBuffer begin_one_time_commands();
	// Submits and waits for a one-shot command buffer.
	void end_one_time_commands(VkCommandBuffer cmd);

	// Emits an image memory barrier for the subset of layouts this renderer uses.
	void transition_image_layout(VkCommandBuffer cmd, VkImage image,
							   VkImageLayout old_layout,
							   VkImageLayout new_layout);
	// Records a tightly packed buffer-to-image copy.
	void copy_buffer_to_image(VkCommandBuffer cmd, VkBuffer buffer, VkImage image,
						   uint32_t width, uint32_t height, uint32_t depth);
	// Records an image-to-buffer copy, optionally reading one pixel offset.
	void copy_image_to_buffer(VkCommandBuffer cmd, VkImage image, VkBuffer buffer,
						   uint32_t width, uint32_t height,
						   uint32_t offset_x = 0, uint32_t offset_y = 0);

	// Converts Oak pixel format/channel metadata to a preferred Vulkan format.
	VkFormat pixel_format_to_vk_format(PixelFormat format, int channel_count) const;
	// Picks a color-attachment-capable format, falling back from RGB to RGBA
	// where drivers do not support 3-channel render targets.
	VkFormat pick_renderable_format(PixelFormat format, int channel_count) const;
	// Checks whether a format can be used as a render target.
	bool is_color_attachment_supported(VkFormat format) const;
	// Returns the packed byte size for supported VkFormat values.
	int get_vk_format_bytes_per_pixel(VkFormat format) const;
	// Returns the alpha fill value used when expanding RGB data to RGBA.
	float get_format_max_alpha(PixelFormat format) const;
	// Repackages tightly packed pixels when the requested CPU channel count
	// differs from the selected GPU format channel count.
	void copy_pixels_with_channel_conversion(const void *src, void *dst, int width,
										 int height, int depth,
										 int src_channels, int dst_channels,
										 PixelFormat format) const;
	// Rounds a size up to the requested alignment.
	VkDeviceSize align_size(VkDeviceSize size, VkDeviceSize alignment) const;

	// Finds a Vulkan memory type matching the requested properties.
	uint32_t find_memory_type(uint32_t type_filter,
							VkMemoryPropertyFlags properties) const;

	// Compiles GLSL source into SPIR-V using shaderc when available.
	bool compile_glsl_to_spv(const QString &glsl, VkShaderStageFlagBits stage,
						  QByteArray *out_spv);
	// Rewrites an Oak GLSL shader into Vulkan-compatible GLSL.
	QString convert_glsl_to_vulkan(const QString &glsl,
								VkShaderStageFlagBits stage);
	// Ensures a shader declares a Vulkan-compatible GLSL version.
	QString ensure_glsl_version450(const QString &glsl) const;
	// Extracts uniforms and sampler names from GLSL declarations.
	void extract_uniforms(const QString &glsl,
						 QVector<UniformInfo> *out_uniforms,
						 QVector<QString> *out_samplers) const;
	// Computes std140 offsets and total UBO size for extracted uniforms.
	void compute_uniform_layout(QVector<UniformInfo> *uniforms) const;
	// Builds the generated uniform block used by rewritten shaders.
	QString build_ubo_block(const QVector<UniformInfo> &uniforms) const;
	// Rewrites standalone uniforms and samplers into explicit UBO/sampler
	// bindings accepted by Vulkan GLSL.
	QString
	rewrite_shader_with_ubo(const QString &glsl,
						 const QVector<UniformInfo> &all_uniforms,
						 const QHash<QString, int> &sampler_bindings) const;
	// Returns std140 storage size for a supported GLSL type.
	VkDeviceSize get_std140_size(const QString &type) const;
	// Returns std140 alignment for a supported GLSL type.
	VkDeviceSize get_std140_alignment(const QString &type) const;

	// Creates or retrieves the graphics pipeline for a shader/render format pair.
	bool create_pipeline_for_shader(VulkanShader *shader,
								 const VideoParams &dest_params,
								 VkFormat render_pass_format);

	// Caches simple single-color-attachment render passes by format/clear mode.
	VkRenderPass get_or_create_render_pass(VkFormat format, bool clear);

	struct TextureBinding {
		QString name;
		VulkanTexture *tex;
		Texture::Interpolation interp;
	};

	// Executes one fullscreen pass with the provided texture bindings and UBO.
	void blit_pass(VulkanShader *shader, VulkanTexture *dest_tex,
				  const QVector<TextureBinding> &bindings,
				  const QByteArray &ubo_data,
				  const VideoParams &destination_params, bool clear_destination,
				  int iteration);

	VkInstance instance_ = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
	// Set to true after the first VK_ERROR_DEVICE_LOST so we stop submitting
	// work and don't flood the log with identical errors.
	bool device_lost_ = false;
	uint32_t physical_device_count_ = 0;
	VkDevice device_ = VK_NULL_HANDLE;
	VkQueue graphics_queue_ = VK_NULL_HANDLE;
	uint32_t graphics_queue_family_ = UINT32_MAX;
	VkCommandPool command_pool_ = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
	VkSampler linear_sampler_ = VK_NULL_HANDLE;
	VkSampler nearest_sampler_ = VK_NULL_HANDLE;

	QHash<quint64, VkRenderPass> render_pass_cache_;
	int descriptor_sets_since_reset_ = 0;

	VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
	VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;
	StagingBuffer *staging_buffer_ = nullptr;
	VkCommandBuffer reusable_command_buffer_ = VK_NULL_HANDLE;
	VkFence reusable_fence_ = VK_NULL_HANDLE;

	VkPhysicalDeviceMemoryProperties mem_properties_;
	VkPhysicalDeviceProperties device_properties_;

	QMutex mutex_;

	// Texture handle counter
	quint64 next_texture_id_ = 1;
	QHash<quint64, VulkanTexture *> textures_;

	// Shader handle counter
	quint64 next_shader_id_ = 1;
	QHash<quint64, VulkanShader *> shaders_;

	static const int k_max_descriptor_sets = 1024;
};

}

#endif // OAK_VULKANRENDERER_H
