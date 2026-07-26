#include <gtest/gtest.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "olive/core/oakcore/color.h"

enum { PF_U8 = 0, PF_U10 = 1, PF_U16 = 2, PF_F16 = 3, PF_F32 = 4 };

static bool feq(float a, float b) { return fabsf(a - b) < 1e-5f; }

TEST(OakcoreColor, CreateAndChannels)
{
	OakColor *c = oakcore_color_create();
	ASSERT_NE(c, nullptr);
	EXPECT_TRUE(feq(oakcore_color_red(c), 0.0f));
	EXPECT_TRUE(feq(oakcore_color_green(c), 0.0f));
	EXPECT_TRUE(feq(oakcore_color_blue(c), 0.0f));
	EXPECT_TRUE(feq(oakcore_color_alpha(c), 0.0f));
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(0.25f, 0.5f, 0.75f, 1.0f);
	EXPECT_TRUE(feq(oakcore_color_red(c), 0.25f));
	EXPECT_TRUE(feq(oakcore_color_green(c), 0.5f));
	EXPECT_TRUE(feq(oakcore_color_blue(c), 0.75f));
	EXPECT_TRUE(feq(oakcore_color_alpha(c), 1.0f));

	oakcore_color_set_red(c, -0.5f);
	oakcore_color_set_green(c, 2.0f);
	oakcore_color_set_blue(c, 0.0f);
	oakcore_color_set_alpha(c, 0.125f);
	EXPECT_TRUE(feq(oakcore_color_red(c), -0.5f));
	EXPECT_TRUE(feq(oakcore_color_green(c), 2.0f));
	EXPECT_TRUE(feq(oakcore_color_blue(c), 0.0f));
	EXPECT_TRUE(feq(oakcore_color_alpha(c), 0.125f));
	oakcore_color_free(c);
}

TEST(OakcoreColor, CopyAndOwnership)
{
	OakColor *c = oakcore_color_create_rgba(0.1f, 0.2f, 0.3f, 0.4f);
	OakColor *copy = oakcore_color_copy(c);
	ASSERT_NE(copy, nullptr);
	EXPECT_TRUE(feq(oakcore_color_red(copy), 0.1f));
	EXPECT_TRUE(feq(oakcore_color_alpha(copy), 0.4f));

	oakcore_color_set_red(copy, 0.9f);
	EXPECT_TRUE(feq(oakcore_color_red(copy), 0.9f));
	EXPECT_TRUE(feq(oakcore_color_red(c), 0.1f));

	oakcore_color_free(copy);
	oakcore_color_free(c);
	oakcore_color_free(NULL);
}

TEST(OakcoreColor, HSV)
{
	OakColor *c = oakcore_color_from_hsv(0.0f, 1.0f, 1.0f);
	EXPECT_TRUE(feq(oakcore_color_red(c), 1.0f));
	EXPECT_TRUE(feq(oakcore_color_green(c), 0.0f));
	EXPECT_TRUE(feq(oakcore_color_blue(c), 0.0f));
	EXPECT_TRUE(feq(oakcore_color_alpha(c), 1.0f));
	oakcore_color_free(c);

	c = oakcore_color_from_hsv(120.0f, 1.0f, 1.0f);
	EXPECT_TRUE(feq(oakcore_color_red(c), 0.0f));
	EXPECT_TRUE(feq(oakcore_color_green(c), 1.0f));
	oakcore_color_free(c);

	c = oakcore_color_from_hsv(240.0f, 1.0f, 0.5f);
	EXPECT_TRUE(feq(oakcore_color_blue(c), 0.5f));
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(1.0f, 0.0f, 0.0f, 1.0f);
	float h, s, v;
	oakcore_color_to_hsv(c, &h, &s, &v);
	EXPECT_TRUE(feq(h, 0.0f));
	EXPECT_TRUE(feq(s, 1.0f));
	EXPECT_TRUE(feq(v, 1.0f));
	EXPECT_TRUE(feq(oakcore_color_hsv_hue(c), h));
	oakcore_color_free(c);

	// Roundtrip
	c = oakcore_color_create_rgba(0.2f, 0.4f, 0.8f, 1.0f);
	oakcore_color_to_hsv(c, &h, &s, &v);
	EXPECT_TRUE(feq(h, 220.0f));
	EXPECT_TRUE(feq(s, 0.75f));
	EXPECT_TRUE(feq(v, 0.8f));
	OakColor *rt = oakcore_color_from_hsv(h, s, v);
	EXPECT_TRUE(feq(oakcore_color_red(rt), 0.2f));
	EXPECT_TRUE(feq(oakcore_color_green(rt), 0.4f));
	EXPECT_TRUE(feq(oakcore_color_blue(rt), 0.8f));
	oakcore_color_free(rt);
	oakcore_color_free(c);
}

TEST(OakcoreColor, HSL)
{
	OakColor *c = oakcore_color_create_rgba(1.0f, 0.0f, 0.0f, 1.0f);
	float h, s, l;
	oakcore_color_to_hsl(c, &h, &s, &l);
	EXPECT_TRUE(feq(h, 0.0f));
	EXPECT_TRUE(feq(s, 1.0f));
	EXPECT_TRUE(feq(l, 0.5f));
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(0.2f, 0.4f, 0.8f, 1.0f);
	oakcore_color_to_hsl(c, &h, &s, &l);
	EXPECT_TRUE(feq(h, 220.0f));
	EXPECT_TRUE(feq(s, 0.6f));
	EXPECT_TRUE(feq(l, 0.5f));
	oakcore_color_free(c);
}

TEST(OakcoreColor, DataAccess)
{
	OakColor *c = oakcore_color_create_rgba(0.1f, 0.2f, 0.3f, 0.4f);
	const float *cd = oakcore_color_const_data(c);
	ASSERT_NE(cd, nullptr);
	EXPECT_TRUE(feq(cd[0], 0.1f));
	EXPECT_TRUE(feq(cd[3], 0.4f));

	float *d = oakcore_color_data(c);
	ASSERT_NE(d, nullptr);
	d[0] = 0.125f;
	d[3] = 1.0f;
	EXPECT_TRUE(feq(oakcore_color_red(c), 0.125f));
	EXPECT_TRUE(feq(oakcore_color_alpha(c), 1.0f));
	oakcore_color_free(c);
}

TEST(OakcoreColor, PixelDataU8)
{
	char buf[16];
	memset(buf, 0, sizeof(buf));
	OakColor *c = oakcore_color_create_rgba(1.0f, 0.0f, 1.0f, 1.0f);
	oakcore_color_to_data(c, buf, PF_U8, 4);
	EXPECT_EQ((uint8_t)buf[0], 255);
	EXPECT_EQ((uint8_t)buf[1], 0);
	EXPECT_EQ((uint8_t)buf[2], 255);

	OakColor *back = oakcore_color_from_data(buf, PF_U8, 4);
	EXPECT_TRUE(feq(oakcore_color_red(back), 1.0f));
	EXPECT_TRUE(feq(oakcore_color_green(back), 0.0f));
	oakcore_color_free(back);
	oakcore_color_free(c);
}

TEST(OakcoreColor, PixelDataF32AndF16)
{
	char buf[16];
	OakColor *c = oakcore_color_create_rgba(0.25f, 0.5f, 0.75f, 1.0f);
	oakcore_color_to_data(c, buf, PF_F32, 4);
	EXPECT_TRUE(feq(((float *)buf)[0], 0.25f));
	OakColor *back = oakcore_color_from_data(buf, PF_F32, 4);
	EXPECT_TRUE(feq(oakcore_color_green(back), 0.5f));
	oakcore_color_free(back);
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(1.0f, 0.5f, 0.0f, 1.0f);
	oakcore_color_to_data(c, buf, PF_F16, 4);
	EXPECT_EQ(((uint16_t *)buf)[0], 0x3C00);
	EXPECT_EQ(((uint16_t *)buf)[1], 0x3800);
	back = oakcore_color_from_data(buf, PF_F16, 4);
	EXPECT_TRUE(feq(oakcore_color_red(back), 1.0f));
	EXPECT_TRUE(feq(oakcore_color_green(back), 0.5f));
	oakcore_color_free(back);
	oakcore_color_free(c);
}

TEST(OakcoreColor, Luminance)
{
	OakColor *c = oakcore_color_create_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	EXPECT_TRUE(feq(oakcore_color_get_rough_luminance(c), 1.0f));
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(0.6f, 0.2f, 0.4f, 1.0f);
	EXPECT_TRUE(feq(oakcore_color_get_rough_luminance(c),
					(2.0f * 0.6f + 0.4f + 3.0f * 0.2f) / 6.0f));
	oakcore_color_free(c);
}

TEST(OakcoreColor, MathOps)
{
	OakColor *a = oakcore_color_create_rgba(0.1f, 0.2f, 0.3f, 0.4f);
	OakColor *b = oakcore_color_create_rgba(0.4f, 0.3f, 0.2f, 0.1f);

	oakcore_color_add_assign(a, b);
	EXPECT_TRUE(feq(oakcore_color_red(a), 0.5f));
	EXPECT_TRUE(feq(oakcore_color_alpha(a), 0.5f));
	oakcore_color_sub_assign(a, b);
	EXPECT_TRUE(feq(oakcore_color_red(a), 0.1f));

	oakcore_color_mul_scalar_assign(a, 2.0f);
	EXPECT_TRUE(feq(oakcore_color_red(a), 0.2f));
	EXPECT_TRUE(feq(oakcore_color_green(a), 0.4f));
	oakcore_color_div_scalar_assign(a, 2.0f);
	EXPECT_TRUE(feq(oakcore_color_red(a), 0.1f));

	oakcore_color_free(b);
	oakcore_color_free(a);
}
