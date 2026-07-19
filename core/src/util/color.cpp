/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2025 mikesolar

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

#include "util/color.h"

#include <algorithm>
#include <cmath>
#include <Imath/half.h>
#include <math.h>
#include <stdint.h>

namespace olive::core
{

Color Color::from_hsv(const DataType &h, const DataType &s, const DataType &v)
{
	DataType c = s * v;
	DataType x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
	DataType m = v - c;
	DataType rs, gs, bs;

	if (h >= 0.0 && h < 60.0) {
		rs = c;
		gs = x;
		bs = 0.0;
	} else if (h >= 60.0 && h < 120.0) {
		rs = x;
		gs = c;
		bs = 0.0;
	} else if (h >= 120.0 && h < 180.0) {
		rs = 0.0;
		gs = c;
		bs = x;
	} else if (h >= 180.0 && h < 240.0) {
		rs = 0.0;
		gs = x;
		bs = c;
	} else if (h >= 240.0 && h < 300.0) {
		rs = x;
		gs = 0.0;
		bs = c;
	} else {
		rs = c;
		gs = 0.0;
		bs = x;
	}

	return Color(rs + m, gs + m, bs + m);
}

Color::Color(const char *data, const PixelFormat &format, int ch_layout)
{
	*this = from_data(data, format, ch_layout);
}

void Color::to_hsv(DataType *hue, DataType *sat, DataType *val) const
{
	DataType f_c_max = std::max(std::max(red(), green()), blue());
	DataType f_c_min = std::min(std::min(red(), green()), blue());
	DataType f_delta = f_c_max - f_c_min;

	if (f_delta > 0) {
		if (f_c_max == red()) {
			*hue = 60 * (fmod(((green() - blue()) / f_delta), 6));
		} else if (f_c_max == green()) {
			*hue = 60 * (((blue() - red()) / f_delta) + 2);
		} else if (f_c_max == blue()) {
			*hue = 60 * (((red() - green()) / f_delta) + 4);
		}

		if (f_c_max > 0) {
			*sat = f_delta / f_c_max;
		} else {
			*sat = 0;
		}

		*val = f_c_max;
	} else {
		*hue = 0;
		*sat = 0;
		*val = f_c_max;
	}

	if (*hue < 0) {
		*hue = 360 + *hue;
	}
}

Color::DataType Color::hsv_hue() const
{
	DataType h, s, v;
	to_hsv(&h, &s, &v);
	return h;
}

Color::DataType Color::hsv_saturation() const
{
	DataType h, s, v;
	to_hsv(&h, &s, &v);
	return s;
}

Color::DataType Color::value() const
{
	DataType h, s, v;
	to_hsv(&h, &s, &v);
	return v;
}

void Color::to_hsl(DataType *hue, DataType *sat, DataType *lightness) const
{
	DataType f_c_min = std::min(red(), std::min(green(), blue()));
	DataType f_c_max = std::max(red(), std::max(green(), blue()));

	*lightness = 0.5 * (f_c_min + f_c_max);

	if (f_c_min == f_c_max) {
		*sat = 0;
		*hue = 0;
		return;

	} else if (*lightness < 0.5) {
		*sat = (f_c_max - f_c_min) / (f_c_max + f_c_min);
	} else {
		*sat = (f_c_max - f_c_min) / (2.0 - f_c_max - f_c_min);
	}

	if (f_c_max == red()) {
		*hue = 60 * (green() - blue()) / (f_c_max - f_c_min);
	}
	if (f_c_max == green()) {
		*hue = 60 * (blue() - red()) / (f_c_max - f_c_min) + 120;
	}
	if (f_c_max == blue()) {
		*hue = 60 * (red() - green()) / (f_c_max - f_c_min) + 240;
	}
	if (*hue < 0) {
		*hue = *hue + 360;
	}
}

Color::DataType Color::hsl_hue() const
{
	DataType h, s, l;
	to_hsl(&h, &s, &l);
	return h;
}

Color::DataType Color::hsl_saturation() const
{
	DataType h, s, l;
	to_hsl(&h, &s, &l);
	return s;
}

Color::DataType Color::lightness() const
{
	DataType h, s, l;
	to_hsl(&h, &s, &l);
	return l;
}

void Color::to_data(char *out, const PixelFormat &format,
				   unsigned int nb_channels) const
{
	unsigned int count = std::min(rgba, nb_channels);

	if (format == PixelFormat::u10 && count == 4) {
		const uint32_t r = static_cast<uint32_t>(std::clamp(data_[0], DataType(0.0), DataType(1.0)) * 1023.0 + 0.5);
		const uint32_t g = static_cast<uint32_t>(std::clamp(data_[1], DataType(0.0), DataType(1.0)) * 1023.0 + 0.5);
		const uint32_t b = static_cast<uint32_t>(std::clamp(data_[2], DataType(0.0), DataType(1.0)) * 1023.0 + 0.5);
		const uint32_t a = static_cast<uint32_t>(std::clamp(data_[3], DataType(0.0), DataType(1.0)) * 3.0 + 0.5);
		reinterpret_cast<uint32_t *>(out)[0] = r | (g << 10) | (b << 20) | (a << 30);
		return;
	}

	for (unsigned int i = 0; i < count; i++) {
		DataType f = data_[i];

		switch (format) {
		case PixelFormat::invalid:
		case PixelFormat::count:
			break;
		case PixelFormat::u8:
			reinterpret_cast<uint8_t *>(out)[i] = f * 255.0;
			break;
		case PixelFormat::u10:
			// handled above
			break;
		case PixelFormat::u16:
			reinterpret_cast<uint16_t *>(out)[i] = f * 65535.0;
			break;
		case PixelFormat::f16:
			reinterpret_cast<Imath::half *>(out)[i] = f;
			break;
		case PixelFormat::f32:
			reinterpret_cast<float *>(out)[i] = f;
			break;
		}
	}
}

Color Color::from_data(const char *in, const PixelFormat &format,
					  unsigned int nb_channels)
{
	Color c;

	unsigned int count = std::min(rgba, nb_channels);

	if (format == PixelFormat::u10 && count == 4) {
		const uint32_t word = reinterpret_cast<const uint32_t *>(in)[0];
		c.data_[0] = DataType((word & 0x3ff) / 1023.0);
		c.data_[1] = DataType(((word >> 10) & 0x3ff) / 1023.0);
		c.data_[2] = DataType(((word >> 20) & 0x3ff) / 1023.0);
		c.data_[3] = DataType(((word >> 30) & 0x3) / 3.0);
		return c;
	}

	for (unsigned int i = 0; i < count; i++) {
		DataType &f = c.data_[i];

		switch (format) {
		case PixelFormat::invalid:
		case PixelFormat::count:
			break;
		case PixelFormat::u8:
			f = DataType(reinterpret_cast<const uint8_t *>(in)[i]) / 255.0;
			break;
		case PixelFormat::u10:
			// handled above
			break;
		case PixelFormat::u16:
			f = DataType(reinterpret_cast<const uint16_t *>(in)[i]) / 65535.0;
			break;
		case PixelFormat::f16:
			f = DataType(reinterpret_cast<const Imath::half *>(in)[i]);
			break;
		case PixelFormat::f32:
			f = DataType(reinterpret_cast<const float *>(in)[i]);
			break;
		}
	}

	return c;
}

Color::DataType Color::get_rough_luminance() const
{
	return (2 * red() + blue() + 3 * green()) / 6.0;
}

Color &Color::operator+=(const Color &rhs)
{
	for (int i = 0; i < rgba; i++) {
		data_[i] += rhs.data_[i];
	}

	return *this;
}

Color &Color::operator-=(const Color &rhs)
{
	for (int i = 0; i < rgba; i++) {
		data_[i] -= rhs.data_[i];
	}

	return *this;
}

Color &Color::operator+=(const DataType &rhs)
{
	for (int i = 0; i < rgba; i++) {
		data_[i] += rhs;
	}

	return *this;
}

Color &Color::operator-=(const DataType &rhs)
{
	for (int i = 0; i < rgba; i++) {
		data_[i] -= rhs;
	}

	return *this;
}

Color &Color::operator*=(const DataType &rhs)
{
	for (int i = 0; i < rgba; i++) {
		data_[i] *= rhs;
	}

	return *this;
}

Color &Color::operator/=(const DataType &rhs)
{
	for (int i = 0; i < rgba; i++) {
		data_[i] /= rhs;
	}

	return *this;
}

}
