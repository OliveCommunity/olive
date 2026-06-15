#include <gtest/gtest.h>

#include <QByteArray>
#include <QMatrix4x4>

#include "node/value.h"
#include "render/backend/dynamicrenderer.h"
#include "render/job/shaderjob.h"
#include "render/shadercode.h"
#include "render/texture.h"
#include "render/videoparams.h"

TEST(DynamicRenderBackend, LoadsExperimentalOpenGLBackend)
{
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	GTEST_SKIP() << "Dynamic render backend is not enabled in this build";
#else
	olive::DynamicRenderer renderer(QStringLiteral("opengl"));
	ASSERT_TRUE(renderer.Load());
	EXPECT_EQ(renderer.OpenGLContext(), nullptr);
	OakRenderBackendInfo info = {};
	ASSERT_TRUE(renderer.GetBackendInfo(&info));
	EXPECT_EQ(info.abi_version, 1U);
	EXPECT_EQ(info.kind, OAK_RENDER_BACKEND_OPENGL);
	EXPECT_STREQ(info.name, "opengl");
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_TEXTURES);
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_SHADERS);
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_BLIT);
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_READBACK);
#endif
}

TEST(DynamicRenderBackend, LoadsExperimentalVulkanBackendWhenAvailable)
{
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	GTEST_SKIP() << "Dynamic render backend is not enabled in this build";
#else
	olive::DynamicRenderer renderer(QStringLiteral("vulkan"));
	ASSERT_TRUE(renderer.Load());
	OakRenderBackendInfo info = {};
	ASSERT_TRUE(renderer.GetBackendInfo(&info));
	if (info.kind != OAK_RENDER_BACKEND_VULKAN) {
		GTEST_SKIP() << "Vulkan backend is not available on this system";
	}
	EXPECT_EQ(renderer.backend_name(), QStringLiteral("vulkan"));
	EXPECT_EQ(renderer.OpenGLContext(), nullptr);
	EXPECT_STREQ(info.name, "vulkan");
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_TEXTURES);
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_SHADERS);
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_BLIT);
	EXPECT_TRUE(info.capabilities & OAK_RENDER_BACKEND_CAP_READBACK);
#endif
}

TEST(DynamicRenderBackend, FallsBackWhenExperimentalVulkanUnavailable)
{
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	GTEST_SKIP() << "Dynamic render backend is not enabled in this build";
#else
	olive::DynamicRenderer renderer(QStringLiteral("vulkan"));
	ASSERT_TRUE(renderer.Load());
	OakRenderBackendInfo info = {};
	ASSERT_TRUE(renderer.GetBackendInfo(&info));
	if (info.kind == OAK_RENDER_BACKEND_VULKAN) {
		GTEST_SKIP() << "Vulkan backend is available on this system; skip fallback test";
	}
	EXPECT_EQ(renderer.backend_name(), QStringLiteral("opengl"));
	EXPECT_EQ(renderer.OpenGLContext(), nullptr);
	EXPECT_EQ(info.kind, OAK_RENDER_BACKEND_OPENGL);
	EXPECT_STREQ(info.name, "opengl");
#endif
}

TEST(DynamicRenderBackend, VulkanUploadBlitDownload)
{
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	GTEST_SKIP() << "Dynamic render backend is not enabled in this build";
#else
	olive::DynamicRenderer renderer(QStringLiteral("vulkan"));
	ASSERT_TRUE(renderer.Load());
	OakRenderBackendInfo info = {};
	ASSERT_TRUE(renderer.GetBackendInfo(&info));
	if (info.kind != OAK_RENDER_BACKEND_VULKAN) {
		GTEST_SKIP() << "Vulkan backend is not available on this system";
	}

	ASSERT_TRUE(renderer.Init());
	renderer.PostInit();

	const int kSize = 64;
	olive::VideoParams params(kSize, kSize, olive::PixelFormat::U8,
							  olive::VideoParams::kRGBAChannelCount);

	olive::TexturePtr src = renderer.CreateTexture(params);
	ASSERT_NE(src, nullptr);
	ASSERT_FALSE(src->IsDummy());

	QByteArray src_data(kSize * kSize * 4, 0);
	for (int i = 0; i < kSize * kSize; ++i) {
		src_data[i * 4 + 0] = static_cast<char>(255); // R
		src_data[i * 4 + 1] = static_cast<char>(0);   // G
		src_data[i * 4 + 2] = static_cast<char>(0);   // B
		src_data[i * 4 + 3] = static_cast<char>(255); // A
	}
	src->Upload(src_data.data(), kSize * 4);

	olive::TexturePtr dst = renderer.CreateTexture(params);
	ASSERT_NE(dst, nullptr);
	ASSERT_FALSE(dst->IsDummy());

	const QString vert = QStringLiteral(
		"uniform mat4 ove_mvpmat;\n"
		"in vec4 a_position;\n"
		"in vec2 a_texcoord;\n"
		"out vec2 ove_texcoord;\n"
		"void main() {\n"
		"    gl_Position = ove_mvpmat * a_position;\n"
		"    ove_texcoord = a_texcoord;\n"
		"}\n");
	const QString frag = QStringLiteral(
		"uniform sampler2D ove_maintex;\n"
		"in vec2 ove_texcoord;\n"
		"out vec4 frag_color;\n"
		"void main() {\n"
		"    frag_color = texture(ove_maintex, ove_texcoord);\n"
		"}\n");
	QVariant shader = renderer.CreateNativeShader(olive::ShaderCode(frag, vert));
	ASSERT_FALSE(shader.isNull());

	olive::ShaderJob job;
	job.Insert(QStringLiteral("ove_maintex"),
			   olive::NodeValue(olive::NodeValue::kTexture,
								QVariant::fromValue(src)));
	job.Insert(QStringLiteral("ove_mvpmat"),
			   olive::NodeValue(olive::NodeValue::kMatrix, QMatrix4x4()));

	renderer.BlitToTexture(shader, job, dst.get(), true);

	QByteArray dst_data(kSize * kSize * 4, 0);
	dst->Download(dst_data.data(), kSize * 4);

	// The default pass-through shader should reproduce the red source pixel.
	EXPECT_EQ(static_cast<uint8_t>(dst_data[0]), 255u);
	EXPECT_EQ(static_cast<uint8_t>(dst_data[1]), 0u);
	EXPECT_EQ(static_cast<uint8_t>(dst_data[2]), 0u);
	EXPECT_EQ(static_cast<uint8_t>(dst_data[3]), 255u);
#endif
}
