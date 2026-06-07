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
#endif
}
