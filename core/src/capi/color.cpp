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

#include "oakcore/color.h"

#include "util/color.h"

namespace
{

olive::core::internal::Color *impl(OakColor *h)
{
	return reinterpret_cast<olive::core::internal::Color *>(h);
}

const olive::core::internal::Color *impl(const OakColor *h)
{
	return reinterpret_cast<const olive::core::internal::Color *>(h);
}

OakColor *wrap(olive::core::internal::Color *c)
{
	return reinterpret_cast<OakColor *>(c);
}

olive::core::PixelFormat to_format(int f)
{
	return olive::core::PixelFormat(
		static_cast<olive::core::PixelFormat::Format>(f));
}

} // namespace

extern "C"
{

OakColor *oakcore_color_create(void)
{
	return wrap(new olive::core::internal::Color());
}

OakColor *oakcore_color_create_rgba(float r, float g, float b, float a)
{
	return wrap(new olive::core::internal::Color(r, g, b, a));
}

OakColor *oakcore_color_copy(const OakColor *self)
{
	return wrap(new olive::core::internal::Color(*impl(self)));
}

void oakcore_color_free(OakColor *self)
{
	delete impl(self);
}

OakColor *oakcore_color_from_hsv(float h, float s, float v)
{
	return wrap(
		new olive::core::internal::Color(olive::core::internal::Color::from_hsv(h, s, v)));
}

OakColor *oakcore_color_from_data(const char *data, int format, int nb_channels)
{
	return wrap(new olive::core::internal::Color(
		olive::core::internal::Color::from_data(data, to_format(format),
												static_cast<unsigned int>(nb_channels))));
}

float oakcore_color_red(const OakColor *self)
{
	return impl(self)->red();
}

float oakcore_color_green(const OakColor *self)
{
	return impl(self)->green();
}

float oakcore_color_blue(const OakColor *self)
{
	return impl(self)->blue();
}

float oakcore_color_alpha(const OakColor *self)
{
	return impl(self)->alpha();
}

void oakcore_color_set_red(OakColor *self, float red)
{
	impl(self)->set_red(red);
}

void oakcore_color_set_green(OakColor *self, float green)
{
	impl(self)->set_green(green);
}

void oakcore_color_set_blue(OakColor *self, float blue)
{
	impl(self)->set_blue(blue);
}

void oakcore_color_set_alpha(OakColor *self, float alpha)
{
	impl(self)->set_alpha(alpha);
}

void oakcore_color_to_hsv(const OakColor *self, float *hue, float *sat, float *val)
{
	impl(self)->to_hsv(hue, sat, val);
}

float oakcore_color_hsv_hue(const OakColor *self)
{
	return impl(self)->hsv_hue();
}

float oakcore_color_hsv_saturation(const OakColor *self)
{
	return impl(self)->hsv_saturation();
}

float oakcore_color_value(const OakColor *self)
{
	return impl(self)->value();
}

void oakcore_color_to_hsl(const OakColor *self, float *hue, float *sat,
						  float *lightness)
{
	impl(self)->to_hsl(hue, sat, lightness);
}

float oakcore_color_hsl_hue(const OakColor *self)
{
	return impl(self)->hsl_hue();
}

float oakcore_color_hsl_saturation(const OakColor *self)
{
	return impl(self)->hsl_saturation();
}

float oakcore_color_lightness(const OakColor *self)
{
	return impl(self)->lightness();
}

float *oakcore_color_data(OakColor *self)
{
	return impl(self)->data();
}

const float *oakcore_color_const_data(const OakColor *self)
{
	return impl(self)->data();
}

void oakcore_color_to_data(const OakColor *self, char *out, int format,
						   int nb_channels)
{
	impl(self)->to_data(out, to_format(format),
						static_cast<unsigned int>(nb_channels));
}

float oakcore_color_get_rough_luminance(const OakColor *self)
{
	return impl(self)->get_rough_luminance();
}

void oakcore_color_add_assign(OakColor *self, const OakColor *other)
{
	*impl(self) += *impl(other);
}

void oakcore_color_sub_assign(OakColor *self, const OakColor *other)
{
	*impl(self) -= *impl(other);
}

void oakcore_color_add_scalar_assign(OakColor *self, float value)
{
	*impl(self) += value;
}

void oakcore_color_sub_scalar_assign(OakColor *self, float value)
{
	*impl(self) -= value;
}

void oakcore_color_mul_scalar_assign(OakColor *self, float value)
{
	*impl(self) *= value;
}

void oakcore_color_div_scalar_assign(OakColor *self, float value)
{
	*impl(self) /= value;
}

} // extern "C"
