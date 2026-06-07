#include <gtest/gtest.h>

#include "render/backend/dynamicrenderer.h"

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

TEST(DynamicRenderBackend, FallsBackWhenExperimentalVulkanUnavailable)
{
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	GTEST_SKIP() << "Dynamic render backend is not enabled in this build";
#else
	olive::DynamicRenderer renderer(QStringLiteral("vulkan"));
	ASSERT_TRUE(renderer.Load());
	EXPECT_EQ(renderer.backend_name(), QStringLiteral("opengl"));
	EXPECT_EQ(renderer.OpenGLContext(), nullptr);
	OakRenderBackendInfo info = {};
	ASSERT_TRUE(renderer.GetBackendInfo(&info));
	EXPECT_EQ(info.kind, OAK_RENDER_BACKEND_OPENGL);
	EXPECT_STREQ(info.name, "opengl");
#endif
}
