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

struct OakCommonColorTransform {
	olive::ColorTransform impl;
};

static int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = (int)value.size() + 1;
	if (buf && buf_size >= needed)
		memcpy(buf, value.c_str(), needed);
	return needed;
}

OakCommonColorTransform *oakcommon_colortransform_init_output(
	const char *output)
{
	if (!output)
		return nullptr;
	try {
		return new OakCommonColorTransform{
			olive::ColorTransform(std::string(output))};
	} catch (...) {
		return nullptr;
	}
}

OakCommonColorTransform *oakcommon_colortransform_init_display(
	const char *display, const char *view, const char *look)
{
	if (!display || !view || !look)
		return nullptr;
	try {
		return new OakCommonColorTransform{olive::ColorTransform(
			std::string(display), std::string(view), std::string(look))};
	} catch (...) {
		return nullptr;
	}
}

void oakcommon_colortransform_free(OakCommonColorTransform *transform)
{
	delete transform;
}

int oakcommon_colortransform_is_display(OakCommonColorTransform *transform,
										int *is_display)
{
	if (!transform || !is_display)
		return OAKCOMMON_E_INVALID;
	*is_display = transform->impl.is_display() ? 1 : 0;
	return OAKCOMMON_OK;
}

int oakcommon_colortransform_get_display(OakCommonColorTransform *transform,
										 char *buf, int buf_size)
{
	if (!transform)
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(transform->impl.display(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_colortransform_get_output(OakCommonColorTransform *transform,
										char *buf, int buf_size)
{
	if (!transform)
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(transform->impl.output(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_colortransform_get_view(OakCommonColorTransform *transform,
									  char *buf, int buf_size)
{
	if (!transform)
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(transform->impl.view(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_colortransform_get_look(OakCommonColorTransform *transform,
									  char *buf, int buf_size)
{
	if (!transform)
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(transform->impl.look(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
