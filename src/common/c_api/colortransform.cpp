/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "common/colortransform.h"

#include <cstring>

#include "../src/colortransform.h"
#include "refcounted.h"

/**
 * @brief Recover the boxed olive::ColorTransform from a handle (NULL-safe).
 */
static olive::ColorTransform *ct(OakColorTransform transform)
{
	return oakcommon::handle_impl<olive::ColorTransform>(transform.ctx);
}

static int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = (int)value.size() + 1;
	if (buf && buf_size >= needed)
		memcpy(buf, value.c_str(), needed);
	return needed;
}

OakColorTransform oakcommon_colortransform_init_output(
	const char *output)
{
	OakColorTransform h = {};
	if (!output)
		return h;
	try {
		return oakcommon::make_handle<OakColorTransform>(
			olive::ColorTransform(std::string(output)));
	} catch (...) {
		OakColorTransform empty = {};
		return empty;
	}
}

OakColorTransform oakcommon_colortransform_init_display(
	const char *display, const char *view, const char *look)
{
	OakColorTransform h = {};
	if (!display || !view || !look)
		return h;
	try {
		return oakcommon::make_handle<OakColorTransform>(
			olive::ColorTransform(std::string(display), std::string(view),
								  std::string(look)));
	} catch (...) {
		OakColorTransform empty = {};
		return empty;
	}
}

OakColorTransform oakcommon_colortransform_init_from_native(
	const olive::ColorTransform *src)
{
	if (!src) {
		OakColorTransform h = {};
		return h;
	}
	try {
		return oakcommon::make_handle<OakColorTransform>(
			olive::ColorTransform(*src));
	} catch (...) {
		OakColorTransform h = {};
		return h;
	}
}

const olive::ColorTransform *oakcommon_colortransform_get_native(
	OakColorTransform transform)
{
	return ct(transform);
}

void oakcommon_colortransform_free(OakColorTransform *transform)
{
	oakcommon::free_handle(transform);
}

int oakcommon_colortransform_is_display(OakColorTransform transform,
										int *is_display)
{
	if (!ct(transform) || !is_display)
		return OAKCOMMON_E_INVALID;
	*is_display = ct(transform)->is_display() ? 1 : 0;
	return OAKCOMMON_OK;
}

int oakcommon_colortransform_get_display(OakColorTransform transform,
										 char *buf, int buf_size)
{
	if (!ct(transform))
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(ct(transform)->display(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_colortransform_get_output(OakColorTransform transform,
										char *buf, int buf_size)
{
	if (!ct(transform))
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(ct(transform)->output(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_colortransform_get_view(OakColorTransform transform,
									  char *buf, int buf_size)
{
	if (!ct(transform))
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(ct(transform)->view(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_colortransform_get_look(OakColorTransform transform,
									  char *buf, int buf_size)
{
	if (!ct(transform))
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(ct(transform)->look(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
