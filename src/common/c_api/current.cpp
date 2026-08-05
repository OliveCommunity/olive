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

#include "common/current.h"

#include "../src/current.h"

struct OakCommonCurrent {
	Current *current;
};

OakCommonCurrent *oakcommon_current_instance(void)
{
	static OakCommonCurrent handle = { &Current::get_instance() };
	return &handle;
}

void oakcommon_current_free(OakCommonCurrent *self)
{
	// No-op: the handle wraps a process-wide singleton.
	(void)self;
}

static int current_set(Current *current,
		       void (Current::*set_fn)(std::shared_ptr<void>),
		       void *obj, OakCommonDestroyFn destroy)
{
	try {
		std::shared_ptr<void> value;
		if (obj) {
			if (destroy)
				value = std::shared_ptr<void>(obj, destroy);
			else
				value = std::shared_ptr<void>(
					obj, [](void *) {});
		}
		(current->*set_fn)(value);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}

static int current_get(Current *current,
		       std::shared_ptr<void> (Current::*get_fn)() const,
		       void **out)
{
	try {
		*out = (current->*get_fn)().get();
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}

int oakcommon_current_set_video_params(OakCommonCurrent *self, void *obj,
				       OakCommonDestroyFn destroy)
{
	if (!self)
		return OAKCOMMON_E_INVALID;
	return current_set(self->current, &Current::set_current_video_params,
			   obj, destroy);
}

int oakcommon_current_set_audio_params(OakCommonCurrent *self, void *obj,
				       OakCommonDestroyFn destroy)
{
	if (!self)
		return OAKCOMMON_E_INVALID;
	return current_set(self->current, &Current::set_current_audio_params,
			   obj, destroy);
}

int oakcommon_current_set_plugin_host(OakCommonCurrent *self, void *obj,
				      OakCommonDestroyFn destroy)
{
	if (!self)
		return OAKCOMMON_E_INVALID;
	return current_set(self->current, &Current::set_plugin_host, obj,
			   destroy);
}

int oakcommon_current_set_plugin_cache(OakCommonCurrent *self, void *obj,
				       OakCommonDestroyFn destroy)
{
	if (!self)
		return OAKCOMMON_E_INVALID;
	return current_set(self->current, &Current::set_plugin_cache, obj,
			   destroy);
}

int oakcommon_current_get_video_params(OakCommonCurrent *self, void **out)
{
	if (!self || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(self->current, &Current::current_video_params,
			   out);
}

int oakcommon_current_get_audio_params(OakCommonCurrent *self, void **out)
{
	if (!self || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(self->current, &Current::current_audio_params,
			   out);
}

int oakcommon_current_get_plugin_host(OakCommonCurrent *self, void **out)
{
	if (!self || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(self->current, &Current::plugin_host, out);
}

int oakcommon_current_get_plugin_cache(OakCommonCurrent *self, void **out)
{
	if (!self || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(self->current, &Current::plugin_cache, out);
}

int oakcommon_current_is_interactive(OakCommonCurrent *self, int *out)
{
	if (!self || !out)
		return OAKCOMMON_E_INVALID;
	try {
		*out = self->current->interactive() ? 1 : 0;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}
