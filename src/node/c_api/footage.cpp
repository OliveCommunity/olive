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

#include "node/footage.h"

#include "common/videoparams.h"
#include "../../../include/render/cancelatom.h"

#include <cstring>
#include <new>
#include <string>

#include "../src/project.h"
#include "../src/project/footage/footage.h"

namespace
{

olive::Footage *to_cpp(OakNodeFootage *footage)
{
	return reinterpret_cast<olive::Footage *>(footage);
}

const olive::Footage *to_cpp(const OakNodeFootage *footage)
{
	return reinterpret_cast<const olive::Footage *>(footage);
}

olive::Project *to_cpp(OakNodeProject *project)
{
	return reinterpret_cast<olive::Project *>(project);
}

/**
 * @brief Shared two-stage string getter.
 *
 * Returns the required buffer size in bytes (including the terminating
 * NUL) as a non-negative value.
 */
int copy_string(const std::string &value, char *buf, int buf_size)
{
	int required = static_cast<int>(value.size()) + 1;

	if (buf && buf_size > 0) {
		size_t copy_len = value.size();
		if (copy_len > static_cast<size_t>(buf_size) - 1) {
			copy_len = static_cast<size_t>(buf_size) - 1;
		}
		memcpy(buf, value.data(), copy_len);
		buf[copy_len] = '\0';
	}

	return required;
}

} // namespace

OakNodeFootage *oaknode_footage_create(OakNodeProject *project,
									   const char *filename)
{
	if (!project) {
		return NULL;
	}

	try {
		auto *footage = new (std::nothrow)
			olive::Footage(filename ? filename : "");
		if (!footage) {
			return NULL;
		}
		to_cpp(project)->add_node(footage);
		return reinterpret_cast<OakNodeFootage *>(footage);
	} catch (...) {
		return NULL;
	}
}

int oaknode_footage_filename(const OakNodeFootage *footage, char *buf,
							 int buf_size)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(footage)->filename(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_set_filename(OakNodeFootage *footage, const char *filename)
{
	if (!footage || !filename) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(footage)->set_filename(filename);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_is_valid(const OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	return to_cpp(footage)->is_valid() ? 1 : 0;
}

int oaknode_footage_timestamp(const OakNodeFootage *footage,
							  int64_t *out_timestamp)
{
	if (!footage || !out_timestamp) {
		return OAKNODE_E_INVALID;
	}

	try {
		*out_timestamp = to_cpp(footage)->timestamp();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_set_timestamp(OakNodeFootage *footage, int64_t timestamp)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(footage)->set_timestamp(timestamp);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_decoder(const OakNodeFootage *footage, char *buf,
							int buf_size)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(footage)->decoder(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_total_stream_count(const OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_cpp(footage)->get_total_stream_count();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_video_stream_count(const OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_cpp(footage)->get_video_stream_count();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_audio_stream_count(const OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_cpp(footage)->get_audio_stream_count();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_subtitle_stream_count(const OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return to_cpp(footage)->get_subtitle_stream_count();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_duration(const OakNodeFootage *footage, int *out_numerator,
							 int *out_denominator)
{
	if (!footage || !out_numerator || !out_denominator) {
		return OAKNODE_E_INVALID;
	}

	try {
		const olive::Rational &length = to_cpp(footage)->get_length();
		*out_numerator = length.numerator();
		*out_denominator = length.denominator();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_proxy_enabled(const OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	return to_cpp(footage)->proxy_enabled() ? 1 : 0;
}

int oaknode_footage_set_proxy_enabled(OakNodeFootage *footage, int enabled)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(footage)->set_proxy_enabled(enabled != 0);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_proxy_path(const OakNodeFootage *footage, char *buf,
							   int buf_size)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(footage)->proxy_path(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_proxy_state(const OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(to_cpp(footage)->proxy_state());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_set_proxy(OakNodeFootage *footage, const char *path,
							  int state, int video_stream_index,
							  int preset_version, int enabled)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(footage)->set_proxy(
			path ? path : "",
			static_cast<olive::ProxyManager::ProxyState>(state),
			video_stream_index, preset_version, enabled != 0);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_clear_proxy(OakNodeFootage *footage)
{
	if (!footage) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(footage)->clear_proxy();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_get_video_params(OakNodeFootage *footage, int index,
									 OakVideoParams *out)
{
	olive::Footage *f = to_cpp(footage);
	if (!f || !out || index < 0) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::VideoParams vp = f->get_video_params(index);
		if (!vp.is_valid() && index >= f->input_array_size(
				olive::ViewerOutput::k_video_params_input)) {
			return OAKNODE_E_NOT_FOUND;
		}
		*out = oakcommon_videoparams_init_from_native(&vp);
		return out->ctx ? OAKNODE_OK : OAKNODE_E_NOMEM;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_set_video_params(OakNodeFootage *footage, int index,
									 const OakVideoParams *params)
{
	olive::Footage *f = to_cpp(footage);
	if (!f || !params || !params->ctx) {
		return OAKNODE_E_INVALID;
	}

	const olive::VideoParams *native =
		oakcommon_videoparams_get_native(*params);
	if (!native) {
		return OAKNODE_E_INVALID;
	}

	try {
		f->set_video_params(*native, index);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_footage_get_video_length(OakNodeFootage *footage,
									 int64_t *out_num, int64_t *out_den)
{
	olive::Footage *f = to_cpp(footage);
	if (!f || !out_num || !out_den) {
		return OAKNODE_E_INVALID;
	}

	olive::core::Rational len = f->get_video_length();
	*out_num = len.numerator();
	*out_den = len.denominator();
	return OAKNODE_OK;
}

int oaknode_footage_set_cancel_atom(OakNodeFootage *footage,
									OakCancelAtom atom)
{
	olive::Footage *f = to_cpp(footage);
	if (!f) {
		return OAKNODE_E_INVALID;
	}

	f->set_cancel_pointer(
		atom.ctx ? oakrender_cancelatom_get_native(atom) : nullptr);
	return OAKNODE_OK;
}

OakNodeNode *oaknode_footage_as_node(OakNodeFootage *footage)
{
	return reinterpret_cast<OakNodeNode *>(footage);
}
