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

namespace
{

/**
 * @brief No-op addref/release for the singleton: it is never destroyed.
 */
void singleton_noop(void *ctx)
{
	(void)ctx;
}

/**
 * @brief Recover the Current singleton from a handle (NULL-safe).
 */
Current *current_of(OakCurrent self)
{
	return static_cast<Current *>(self.ctx);
}

} // namespace

OakCurrent oakcommon_current_instance(void)
{
	OakCurrent h = {};
	h.ctx = &Current::get_instance();
	h.addref = &singleton_noop;
	h.release = &singleton_noop;
	h.abi_version = OAKCOMMON_ABI_VERSION;
	return h;
}

void oakcommon_current_free(OakCurrent *self)
{
	// No-op: the handle wraps a process-wide singleton whose release()
	// intentionally never destroys anything.
	(void)self;
}

static int current_set(Current *current,
		       void (Current::*set_fn)(std::shared_ptr<void>),
		       void *obj, OakDestroyFn destroy)
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

int oakcommon_current_set_video_params(OakCurrent self, void *obj,
				       OakDestroyFn destroy)
{
	if (!current_of(self))
		return OAKCOMMON_E_INVALID;
	return current_set(current_of(self), &Current::set_current_video_params,
			   obj, destroy);
}

int oakcommon_current_set_audio_params(OakCurrent self, void *obj,
				       OakDestroyFn destroy)
{
	if (!current_of(self))
		return OAKCOMMON_E_INVALID;
	return current_set(current_of(self), &Current::set_current_audio_params,
			   obj, destroy);
}

int oakcommon_current_set_plugin_host(OakCurrent self, void *obj,
				      OakDestroyFn destroy)
{
	if (!current_of(self))
		return OAKCOMMON_E_INVALID;
	return current_set(current_of(self), &Current::set_plugin_host, obj,
			   destroy);
}

int oakcommon_current_set_plugin_cache(OakCurrent self, void *obj,
				       OakDestroyFn destroy)
{
	if (!current_of(self))
		return OAKCOMMON_E_INVALID;
	return current_set(current_of(self), &Current::set_plugin_cache, obj,
			   destroy);
}

int oakcommon_current_get_video_params(OakCurrent self, void **out)
{
	if (!current_of(self) || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(current_of(self), &Current::current_video_params,
			   out);
}

int oakcommon_current_get_audio_params(OakCurrent self, void **out)
{
	if (!current_of(self) || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(current_of(self), &Current::current_audio_params,
			   out);
}

int oakcommon_current_get_plugin_host(OakCurrent self, void **out)
{
	if (!current_of(self) || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(current_of(self), &Current::plugin_host, out);
}

int oakcommon_current_get_plugin_cache(OakCurrent self, void **out)
{
	if (!current_of(self) || !out)
		return OAKCOMMON_E_INVALID;
	return current_get(current_of(self), &Current::plugin_cache, out);
}

int oakcommon_current_is_interactive(OakCurrent self, int *out)
{
	if (!current_of(self) || !out)
		return OAKCOMMON_E_INVALID;
	try {
		*out = current_of(self)->interactive() ? 1 : 0;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
	return OAKCOMMON_OK;
}
