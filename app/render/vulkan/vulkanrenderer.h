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

#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

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
	explicit VulkanRenderer(QObject *parent = nullptr);
	virtual ~VulkanRenderer() override;

	virtual bool Init() override;
	virtual void PostInit() override;
	virtual void PostDestroy() override;

	virtual void ClearDestination(olive::Texture *texture = nullptr,
							  double r = 0.0, double g = 0.0,
							  double b = 0.0, double a = 0.0) override;

	virtual QVariant CreateNativeShader(olive::ShaderCode code) override;
	virtual void DestroyNativeShader(QVariant shader) override;

	virtual void UploadToTexture(const QVariant &handle,
							 const VideoParams &params, const void *data,
							 int linesize) override;
	virtual void DownloadFromTexture(const QVariant &handle,
								 const VideoParams &params, void *data,
								 int linesize) override;

	virtual void Flush() override;

	virtual Color GetPixelFromTexture(olive::Texture *texture,
								  const QPointF &pt) override;

	bool IsAvailable() const
	{
		return device_ != VK_NULL_HANDLE;
	}

protected:
	virtual void Blit(QVariant shader, olive::AcceleratedJob &job,
				  olive::Texture *destination, VideoParams destination_params,
				  bool clear_destination) override;
	virtual QVariant CreateNativeTexture(int width, int height, int depth,
								 PixelFormat format, int channel_count,
								 const void *data = nullptr,
								 int linesize = 0) override;
	virtual void DestroyNativeTexture(QVariant texture) override;
	virtual void DestroyInternal() override;

private:
	struct VulkanTexture;
	struct VulkanShader;
	struct UniformInfo;

	bool CreateInstance();
	bool CreateDevice();
	bool CreateCommandPool();
	bool CreateDescriptorPool();
	bool CreateVertexBuffer();
	bool CreateLinearSampler();
	bool CreateStagingBuffer(VkDeviceSize size, VkBuffer *out_buffer,
							 VkDeviceMemory *out_memory);
	void DestroyStagingBuffer(VkBuffer buffer, VkDeviceMemory memory);

	VkCommandBuffer BeginOneTimeCommands();
	void EndOneTimeCommands(VkCommandBuffer cmd);

	void TransitionImageLayout(VkCommandBuffer cmd, VkImage image,
							   VkImageLayout old_layout,
							   VkImageLayout new_layout);
	void CopyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image,
						   uint32_t width, uint32_t height, uint32_t depth);
	void CopyImageToBuffer(VkCommandBuffer cmd, VkImage image, VkBuffer buffer,
						   uint32_t width, uint32_t height,
						   uint32_t offset_x = 0, uint32_t offset_y = 0);

	VkFormat PixelFormatToVkFormat(PixelFormat format, int channel_count) const;
	VkFormat PickRenderableFormat(PixelFormat format, int channel_count) const;
	bool IsColorAttachmentSupported(VkFormat format) const;
	VkDeviceSize AlignSize(VkDeviceSize size, VkDeviceSize alignment) const;

	uint32_t FindMemoryType(uint32_t type_filter,
							VkMemoryPropertyFlags properties) const;

	bool CompileGlslToSpv(const QString &glsl, VkShaderStageFlagBits stage,
						  QByteArray *out_spv);
	QString ConvertGlslToVulkan(const QString &glsl, VkShaderStageFlagBits stage);
	QString EnsureGlslVersion450(const QString &glsl) const;
	void ExtractUniforms(const QString &glsl, QVector<UniformInfo> *out_uniforms,
						 QVector<QString> *out_samplers) const;
	void ComputeUniformLayout(QVector<UniformInfo> *uniforms) const;
	QString BuildUboBlock(const QVector<UniformInfo> &uniforms) const;
	QString RewriteShaderWithUbo(const QString &glsl,
							   const QVector<UniformInfo> &all_uniforms,
							   const QHash<QString, int> &sampler_bindings) const;
	VkDeviceSize GetStd140Size(const QString &type) const;
	VkDeviceSize GetStd140Alignment(const QString &type) const;

	bool CreatePipelineForShader(VulkanShader *shader,
								 const VideoParams &dest_params,
								 VkFormat render_pass_format);

	VkRenderPass GetOrCreateRenderPass(VkFormat format, bool clear);

	VkInstance instance_ = VK_NULL_HANDLE;
	VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
	uint32_t physical_device_count_ = 0;
	VkDevice device_ = VK_NULL_HANDLE;
	VkQueue graphics_queue_ = VK_NULL_HANDLE;
	uint32_t graphics_queue_family_ = UINT32_MAX;
	VkCommandPool command_pool_ = VK_NULL_HANDLE;
	VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
	VkSampler linear_sampler_ = VK_NULL_HANDLE;

	QHash<quint64, VkRenderPass> render_pass_cache_;

	VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
	VkDeviceMemory vertex_buffer_memory_ = VK_NULL_HANDLE;

	VkPhysicalDeviceMemoryProperties mem_properties_;
	VkPhysicalDeviceProperties device_properties_;

	QMutex mutex_;

	// Texture handle counter
	quint64 next_texture_id_ = 1;
	QHash<quint64, VulkanTexture *> textures_;

	// Shader handle counter
	quint64 next_shader_id_ = 1;
	QHash<quint64, VulkanShader *> shaders_;

	static const int kMaxDescriptorSets = 1024;
};

}

#endif // VULKANRENDERER_H
