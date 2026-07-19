/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

***/

/**
 * @file oakcore_color_test.cpp
 * @brief Pure C API test for the OakColor ABI
 *
 * Exercises every function of oakcore/color.h through the C boundary only:
 * no C++ wrapper, no test framework, just main() + assert().
 */

#include "olive/core/oakcore/color.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* PixelFormat::Format values (render/pixelformat.h) */
enum { PF_U8 = 0, PF_U10 = 1, PF_U16 = 2, PF_F16 = 3, PF_F32 = 4 };

static int feq(float a, float b)
{
	return fabsf(a - b) < 1e-5f;
}

static void test_create_and_channels(void)
{
	/* Default construction: all channels zero */
	OakColor *c = oakcore_color_create();
	assert(c != NULL);
	assert(feq(oakcore_color_red(c), 0.0f));
	assert(feq(oakcore_color_green(c), 0.0f));
	assert(feq(oakcore_color_blue(c), 0.0f));
	assert(feq(oakcore_color_alpha(c), 0.0f));
	oakcore_color_free(c);

	/* RGBA construction + getters */
	c = oakcore_color_create_rgba(0.25f, 0.5f, 0.75f, 1.0f);
	assert(feq(oakcore_color_red(c), 0.25f));
	assert(feq(oakcore_color_green(c), 0.5f));
	assert(feq(oakcore_color_blue(c), 0.75f));
	assert(feq(oakcore_color_alpha(c), 1.0f));

	/* Setters, including out-of-gamut HDR values */
	oakcore_color_set_red(c, -0.5f);
	oakcore_color_set_green(c, 2.0f);
	oakcore_color_set_blue(c, 0.0f);
	oakcore_color_set_alpha(c, 0.125f);
	assert(feq(oakcore_color_red(c), -0.5f));
	assert(feq(oakcore_color_green(c), 2.0f));
	assert(feq(oakcore_color_blue(c), 0.0f));
	assert(feq(oakcore_color_alpha(c), 0.125f));
	oakcore_color_free(c);
}

static void test_copy_and_ownership(void)
{
	OakColor *c = oakcore_color_create_rgba(0.1f, 0.2f, 0.3f, 0.4f);
	OakColor *copy = oakcore_color_copy(c);
	assert(copy != NULL);
	assert(feq(oakcore_color_red(copy), 0.1f));
	assert(feq(oakcore_color_green(copy), 0.2f));
	assert(feq(oakcore_color_blue(copy), 0.3f));
	assert(feq(oakcore_color_alpha(copy), 0.4f));

	/* The copy is independent from the original */
	oakcore_color_set_red(copy, 0.9f);
	assert(feq(oakcore_color_red(copy), 0.9f));
	assert(feq(oakcore_color_red(c), 0.1f));

	oakcore_color_free(copy);
	oakcore_color_free(c);

	/* Releasing a NULL handle must be safe */
	oakcore_color_free(NULL);
}

static void test_hsv(void)
{
	/* Primary colors from HSV */
	OakColor *c = oakcore_color_from_hsv(0.0f, 1.0f, 1.0f);
	assert(feq(oakcore_color_red(c), 1.0f));
	assert(feq(oakcore_color_green(c), 0.0f));
	assert(feq(oakcore_color_blue(c), 0.0f));
	assert(feq(oakcore_color_alpha(c), 1.0f));
	oakcore_color_free(c);

	c = oakcore_color_from_hsv(120.0f, 1.0f, 1.0f);
	assert(feq(oakcore_color_red(c), 0.0f));
	assert(feq(oakcore_color_green(c), 1.0f));
	assert(feq(oakcore_color_blue(c), 0.0f));
	oakcore_color_free(c);

	c = oakcore_color_from_hsv(240.0f, 1.0f, 0.5f);
	assert(feq(oakcore_color_red(c), 0.0f));
	assert(feq(oakcore_color_green(c), 0.0f));
	assert(feq(oakcore_color_blue(c), 0.5f));
	oakcore_color_free(c);

	/* to_hsv of pure red and gray */
	c = oakcore_color_create_rgba(1.0f, 0.0f, 0.0f, 1.0f);
	float h = -1.0f, s = -1.0f, v = -1.0f;
	oakcore_color_to_hsv(c, &h, &s, &v);
	assert(feq(h, 0.0f));
	assert(feq(s, 1.0f));
	assert(feq(v, 1.0f));
	assert(feq(oakcore_color_hsv_hue(c), h));
	assert(feq(oakcore_color_hsv_saturation(c), s));
	assert(feq(oakcore_color_value(c), v));
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(0.5f, 0.5f, 0.5f, 1.0f);
	oakcore_color_to_hsv(c, &h, &s, &v);
	assert(feq(h, 0.0f));
	assert(feq(s, 0.0f));
	assert(feq(v, 0.5f));
	oakcore_color_free(c);

	/* Roundtrip: color -> hsv -> color */
	c = oakcore_color_create_rgba(0.2f, 0.4f, 0.8f, 1.0f);
	oakcore_color_to_hsv(c, &h, &s, &v);
	assert(feq(h, 220.0f));
	assert(feq(s, 0.75f));
	assert(feq(v, 0.8f));
	OakColor *rt = oakcore_color_from_hsv(h, s, v);
	assert(feq(oakcore_color_red(rt), 0.2f));
	assert(feq(oakcore_color_green(rt), 0.4f));
	assert(feq(oakcore_color_blue(rt), 0.8f));
	oakcore_color_free(rt);
	oakcore_color_free(c);
}

static void test_hsl(void)
{
	/* Pure red: l = 0.5, s = 1, h = 0 */
	OakColor *c = oakcore_color_create_rgba(1.0f, 0.0f, 0.0f, 1.0f);
	float h = -1.0f, s = -1.0f, l = -1.0f;
	oakcore_color_to_hsl(c, &h, &s, &l);
	assert(feq(h, 0.0f));
	assert(feq(s, 1.0f));
	assert(feq(l, 0.5f));
	assert(feq(oakcore_color_hsl_hue(c), h));
	assert(feq(oakcore_color_hsl_saturation(c), s));
	assert(feq(oakcore_color_lightness(c), l));
	oakcore_color_free(c);

	/* Gray: zero saturation */
	c = oakcore_color_create_rgba(0.5f, 0.5f, 0.5f, 1.0f);
	oakcore_color_to_hsl(c, &h, &s, &l);
	assert(feq(h, 0.0f));
	assert(feq(s, 0.0f));
	assert(feq(l, 0.5f));
	oakcore_color_free(c);

	/* Arbitrary color */
	c = oakcore_color_create_rgba(0.2f, 0.4f, 0.8f, 1.0f);
	oakcore_color_to_hsl(c, &h, &s, &l);
	assert(feq(h, 220.0f));
	assert(feq(s, 0.6f));
	assert(feq(l, 0.5f));
	oakcore_color_free(c);
}

static void test_data_access(void)
{
	OakColor *c = oakcore_color_create_rgba(0.1f, 0.2f, 0.3f, 0.4f);

	/* Const read access */
	const float *cd = oakcore_color_const_data(c);
	assert(cd != NULL);
	assert(feq(cd[0], 0.1f));
	assert(feq(cd[1], 0.2f));
	assert(feq(cd[2], 0.3f));
	assert(feq(cd[3], 0.4f));

	/* Mutable write access */
	float *d = oakcore_color_data(c);
	assert(d != NULL);
	d[0] = 0.125f;
	d[3] = 1.0f;
	assert(feq(oakcore_color_red(c), 0.125f));
	assert(feq(oakcore_color_alpha(c), 1.0f));

	oakcore_color_free(c);
}

static void test_pixel_data(void)
{
	char buf[16];
	memset(buf, 0, sizeof(buf));

	/* u8 roundtrip, 4 channels */
	OakColor *c = oakcore_color_create_rgba(1.0f, 0.0f, 1.0f, 1.0f);
	oakcore_color_to_data(c, buf, PF_U8, 4);
	assert((uint8_t)buf[0] == 255);
	assert((uint8_t)buf[1] == 0);
	assert((uint8_t)buf[2] == 255);
	assert((uint8_t)buf[3] == 255);
	OakColor *back = oakcore_color_from_data(buf, PF_U8, 4);
	assert(feq(oakcore_color_red(back), 1.0f));
	assert(feq(oakcore_color_green(back), 0.0f));
	assert(feq(oakcore_color_blue(back), 1.0f));
	assert(feq(oakcore_color_alpha(back), 1.0f));
	oakcore_color_free(back);

	/* u8 with only 3 channels: alpha stays 0 */
	oakcore_color_to_data(c, buf, PF_U8, 3);
	back = oakcore_color_from_data(buf, PF_U8, 3);
	assert(feq(oakcore_color_red(back), 1.0f));
	assert(feq(oakcore_color_green(back), 0.0f));
	assert(feq(oakcore_color_blue(back), 1.0f));
	assert(feq(oakcore_color_alpha(back), 0.0f));
	oakcore_color_free(back);

	/* u16 roundtrip */
	oakcore_color_to_data(c, buf, PF_U16, 4);
	assert(((uint16_t *)buf)[0] == 65535);
	assert(((uint16_t *)buf)[1] == 0);
	back = oakcore_color_from_data(buf, PF_U16, 4);
	assert(feq(oakcore_color_red(back), 1.0f));
	assert(feq(oakcore_color_green(back), 0.0f));
	assert(feq(oakcore_color_alpha(back), 1.0f));
	oakcore_color_free(back);
	oakcore_color_free(c);

	/* f32 roundtrip, exact */
	c = oakcore_color_create_rgba(0.25f, 0.5f, 0.75f, 1.0f);
	oakcore_color_to_data(c, buf, PF_F32, 4);
	assert(feq(((float *)buf)[0], 0.25f));
	assert(feq(((float *)buf)[3], 1.0f));
	back = oakcore_color_from_data(buf, PF_F32, 4);
	assert(feq(oakcore_color_red(back), 0.25f));
	assert(feq(oakcore_color_green(back), 0.5f));
	assert(feq(oakcore_color_blue(back), 0.75f));
	assert(feq(oakcore_color_alpha(back), 1.0f));
	oakcore_color_free(back);

	/* f16 roundtrip: 1.0 = 0x3C00, 0.5 = 0x3800 in IEEE half */
	c = oakcore_color_create_rgba(1.0f, 0.5f, 0.0f, 1.0f);
	oakcore_color_to_data(c, buf, PF_F16, 4);
	assert(((uint16_t *)buf)[0] == 0x3C00);
	assert(((uint16_t *)buf)[1] == 0x3800);
	back = oakcore_color_from_data(buf, PF_F16, 4);
	assert(feq(oakcore_color_red(back), 1.0f));
	assert(feq(oakcore_color_green(back), 0.5f));
	assert(feq(oakcore_color_blue(back), 0.0f));
	assert(feq(oakcore_color_alpha(back), 1.0f));
	oakcore_color_free(back);

	/* u10 packed 4-channel roundtrip: all ones packs to 0xFFFFFFFF */
	c = oakcore_color_create_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	oakcore_color_to_data(c, buf, PF_U10, 4);
	assert(((uint32_t *)buf)[0] == 0xFFFFFFFFu);
	back = oakcore_color_from_data(buf, PF_U10, 4);
	assert(feq(oakcore_color_red(back), 1.0f));
	assert(feq(oakcore_color_green(back), 1.0f));
	assert(feq(oakcore_color_blue(back), 1.0f));
	assert(feq(oakcore_color_alpha(back), 1.0f));
	oakcore_color_free(back);
	oakcore_color_free(c);
}

static void test_luminance(void)
{
	/* (2r + b + 3g) / 6 */
	OakColor *c = oakcore_color_create_rgba(1.0f, 1.0f, 1.0f, 1.0f);
	assert(feq(oakcore_color_get_rough_luminance(c), 1.0f));
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(0.5f, 0.5f, 0.5f, 1.0f);
	assert(feq(oakcore_color_get_rough_luminance(c), 0.5f));
	oakcore_color_free(c);

	c = oakcore_color_create_rgba(0.6f, 0.2f, 0.4f, 1.0f);
	assert(feq(oakcore_color_get_rough_luminance(c),
			   (2.0f * 0.6f + 0.4f + 3.0f * 0.2f) / 6.0f));
	oakcore_color_free(c);
}

static void test_math(void)
{
	OakColor *a = oakcore_color_create_rgba(0.1f, 0.2f, 0.3f, 0.4f);
	OakColor *b = oakcore_color_create_rgba(0.4f, 0.3f, 0.2f, 0.1f);

	/* Color +=/-= Color */
	oakcore_color_add_assign(a, b);
	assert(feq(oakcore_color_red(a), 0.5f));
	assert(feq(oakcore_color_green(a), 0.5f));
	assert(feq(oakcore_color_blue(a), 0.5f));
	assert(feq(oakcore_color_alpha(a), 0.5f));
	oakcore_color_sub_assign(a, b);
	assert(feq(oakcore_color_red(a), 0.1f));
	assert(feq(oakcore_color_green(a), 0.2f));
	assert(feq(oakcore_color_blue(a), 0.3f));
	assert(feq(oakcore_color_alpha(a), 0.4f));

	/* Scalar assign operators (apply to all channels) */
	oakcore_color_add_scalar_assign(a, 1.0f);
	assert(feq(oakcore_color_red(a), 1.1f));
	assert(feq(oakcore_color_alpha(a), 1.4f));
	oakcore_color_sub_scalar_assign(a, 0.6f);
	assert(feq(oakcore_color_red(a), 0.5f));
	assert(feq(oakcore_color_alpha(a), 0.8f));
	oakcore_color_mul_scalar_assign(a, 2.0f);
	assert(feq(oakcore_color_red(a), 1.0f));
	assert(feq(oakcore_color_green(a), 1.2f));
	assert(feq(oakcore_color_blue(a), 1.4f));
	assert(feq(oakcore_color_alpha(a), 1.6f));
	oakcore_color_div_scalar_assign(a, 4.0f);
	assert(feq(oakcore_color_red(a), 0.25f));
	assert(feq(oakcore_color_green(a), 0.3f));
	assert(feq(oakcore_color_blue(a), 0.35f));
	assert(feq(oakcore_color_alpha(a), 0.4f));

	oakcore_color_free(b);
	oakcore_color_free(a);
}

int main(void)
{
	test_create_and_channels();
	test_copy_and_ownership();
	test_hsv();
	test_hsl();
	test_data_access();
	test_pixel_data();
	test_luminance();
	test_math();

	printf("oakcore_color_test: all assertions passed\n");
	return 0;
}
