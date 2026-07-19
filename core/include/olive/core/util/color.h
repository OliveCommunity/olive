/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef OAK_LIBOLIVECORE_COLOR_H
#define OAK_LIBOLIVECORE_COLOR_H

#include "olive/core/oakcore/color.h"
#include "../render/pixelformat.h"

namespace olive::core
{

/**
 * @brief High precision 32-bit DataType based RGBA color value
 *
 * Consumer-side wrapper over the liboakcore C ABI: the object only holds an
 * opaque OakColor handle and forwards every call across the C boundary.
 * The public API is unchanged from the original implementation.
 */
class Color {
public:
	using DataType = float;
	static constexpr unsigned int rgba = 4;

	Color()
		: handle_(oakcore_color_create())
	{
	}

	Color(const DataType &r, const DataType &g, const DataType &b,
		  const DataType &a = 1.0f)
		: handle_(oakcore_color_create_rgba(r, g, b, a))
	{
	}

	Color(const char *data, const PixelFormat &format, int ch_layout)
		: handle_(oakcore_color_from_data(data, format, ch_layout))
	{
	}

	Color(const Color &rhs)
		: handle_(oakcore_color_copy(rhs.handle_))
	{
	}

	Color(Color &&rhs) noexcept
		: handle_(rhs.handle_)
	{
		rhs.handle_ = nullptr;
	}

	~Color()
	{
		oakcore_color_free(handle_);
	}

	Color &operator=(const Color &rhs)
	{
		if (this != &rhs) {
			oakcore_color_free(handle_);
			handle_ = oakcore_color_copy(rhs.handle_);
		}
		return *this;
	}

	Color &operator=(Color &&rhs) noexcept
	{
		if (this != &rhs) {
			oakcore_color_free(handle_);
			handle_ = rhs.handle_;
			rhs.handle_ = nullptr;
		}
		return *this;
	}

	/**
   * @brief Creates a Color struct from hue/saturation/value
   *
   * Hue expects a value between 0.0 and 360.0. Saturation and Value expect a value between 0.0 and 1.0.
   */
	static Color from_hsv(const DataType &h, const DataType &s,
						 const DataType &v)
	{
		return from_handle(oakcore_color_from_hsv(h, s, v));
	}

	DataType red() const
	{
		return oakcore_color_red(handle_);
	}
	DataType green() const
	{
		return oakcore_color_green(handle_);
	}
	DataType blue() const
	{
		return oakcore_color_blue(handle_);
	}
	DataType alpha() const
	{
		return oakcore_color_alpha(handle_);
	}

	void to_hsv(DataType *hue, DataType *sat, DataType *val) const
	{
		oakcore_color_to_hsv(handle_, hue, sat, val);
	}
	DataType hsv_hue() const
	{
		return oakcore_color_hsv_hue(handle_);
	}
	DataType hsv_saturation() const
	{
		return oakcore_color_hsv_saturation(handle_);
	}
	DataType value() const
	{
		return oakcore_color_value(handle_);
	}

	void to_hsl(DataType *hue, DataType *sat, DataType *lightness) const
	{
		oakcore_color_to_hsl(handle_, hue, sat, lightness);
	}
	DataType hsl_hue() const
	{
		return oakcore_color_hsl_hue(handle_);
	}
	DataType hsl_saturation() const
	{
		return oakcore_color_hsl_saturation(handle_);
	}
	DataType lightness() const
	{
		return oakcore_color_lightness(handle_);
	}

	void set_red(const DataType &red)
	{
		oakcore_color_set_red(handle_, red);
	}
	void set_green(const DataType &green)
	{
		oakcore_color_set_green(handle_, green);
	}
	void set_blue(const DataType &blue)
	{
		oakcore_color_set_blue(handle_, blue);
	}
	void set_alpha(const DataType &alpha)
	{
		oakcore_color_set_alpha(handle_, alpha);
	}

	DataType *data()
	{
		return oakcore_color_data(handle_);
	}
	const DataType *data() const
	{
		return oakcore_color_const_data(handle_);
	}

	void to_data(char *out, const PixelFormat &format,
				unsigned int nb_channels) const
	{
		oakcore_color_to_data(handle_, out, format, int(nb_channels));
	}

	static Color from_data(const char *in, const PixelFormat &format,
						  unsigned int nb_channels)
	{
		return from_handle(oakcore_color_from_data(in, format, int(nb_channels)));
	}

	// Suuuuper rough luminance value mostly used for UI (determining whether to overlay with black
	// or white text)
	DataType get_rough_luminance() const
	{
		return oakcore_color_get_rough_luminance(handle_);
	}

	// Assignment math operators
	Color &operator+=(const Color &rhs)
	{
		oakcore_color_add_assign(handle_, rhs.handle_);
		return *this;
	}

	Color &operator-=(const Color &rhs)
	{
		oakcore_color_sub_assign(handle_, rhs.handle_);
		return *this;
	}

	Color &operator+=(const DataType &rhs)
	{
		oakcore_color_add_scalar_assign(handle_, rhs);
		return *this;
	}

	Color &operator-=(const DataType &rhs)
	{
		oakcore_color_sub_scalar_assign(handle_, rhs);
		return *this;
	}

	Color &operator*=(const DataType &rhs)
	{
		oakcore_color_mul_scalar_assign(handle_, rhs);
		return *this;
	}

	Color &operator/=(const DataType &rhs)
	{
		oakcore_color_div_scalar_assign(handle_, rhs);
		return *this;
	}

	// Binary math operators
	Color operator+(const Color &rhs) const
	{
		Color c(*this);
		c += rhs;
		return c;
	}

	Color operator-(const Color &rhs) const
	{
		Color c(*this);
		c -= rhs;
		return c;
	}

	Color operator+(const DataType &rhs) const
	{
		Color c(*this);
		c += rhs;
		return c;
	}

	Color operator-(const DataType &rhs) const
	{
		Color c(*this);
		c -= rhs;
		return c;
	}

	Color operator*(const DataType &rhs) const
	{
		Color c(*this);
		c *= rhs;
		return c;
	}

	Color operator/(const DataType &rhs) const
	{
		Color c(*this);
		c /= rhs;
		return c;
	}

	/**
	 * @brief The wrapped C handle, for cross-type wrappers and direct C API use
	 */
	OakColor *handle() const
	{
		return handle_;
	}

	/**
	 * @brief Wraps an owned C handle (takes ownership)
	 */
	static Color from_handle(OakColor *handle)
	{
		return Color(handle);
	}

private:
	explicit Color(OakColor *handle)
		: handle_(handle)
	{
	}

	OakColor *handle_;
};

}

#endif // OAK_LIBOLIVECORE_COLOR_H
