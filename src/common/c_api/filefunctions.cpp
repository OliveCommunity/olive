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

#include "common/filefunctions.h"

#include <cstring>
#include <string>

#include "../src/filefunctions.h"
#include "refcounted.h"

namespace
{

/**
 * @brief Stateless family; the boxed object is empty and only exists so
 *        the handle has something to reference-count.
 */
struct FileFunctionsState {
	int unused;
};

} // namespace

namespace
{

/**
 * @brief Copies `value` into the caller's two-stage buffer
 *
 * @return Required buffer size in bytes (including the terminating NUL).
 * Always non-negative; error codes are handled by the callers.
 */
int write_string_result(const std::string &value, char *buf, int buf_size)
{
	int required = static_cast<int>(value.size()) + 1;
	if (buf != nullptr && buf_size >= required) {
		memcpy(buf, value.c_str(), required);
	}
	return required;
}

bool is_valid_string_out(const char *buf, int buf_size)
{
	return buf_size >= 0 && (buf_size == 0 || buf != nullptr);
}

} // namespace

OakFileFunctions oakcommon_filefunctions_init(void)
{
	try {
		return oakcommon::make_handle<OakFileFunctions>(
			FileFunctionsState{0});
	} catch (...) {
		OakFileFunctions h = {};
		return h;
	}
}

void oakcommon_filefunctions_free(OakFileFunctions *self)
{
	oakcommon::free_handle(self);
}

int oakcommon_filefunctions_get_unique_file_identifier(
	OakFileFunctions self, const char *filename, char *buf,
	int buf_size)
{
	if (self.ctx == nullptr || filename == nullptr ||
	    !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(
			FileFunctions::get_unique_file_identifier(filename), buf,
			buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_get_configuration_location(
	OakFileFunctions self, char *buf, int buf_size)
{
	if (self.ctx == nullptr || !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(
			FileFunctions::get_configuration_location(), buf,
			buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_get_application_path(
	OakFileFunctions self, char *buf, int buf_size)
{
	if (self.ctx == nullptr || !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(FileFunctions::get_application_path(),
					   buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_get_temp_file_path(
	OakFileFunctions self, char *buf, int buf_size)
{
	if (self.ctx == nullptr || !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(FileFunctions::get_temp_file_path(),
					   buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_get_auto_recovery_root(
	OakFileFunctions self, char *buf, int buf_size)
{
	if (self.ctx == nullptr || !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(
			FileFunctions::get_auto_recovery_root(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_can_copy_directory_without_overwriting(
	OakFileFunctions self, const char *source, const char *dest,
	int *out)
{
	if (self.ctx == nullptr || source == nullptr || dest == nullptr ||
	    out == nullptr) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		*out = FileFunctions::can_copy_directory_without_overwriting(
			       source, dest)
			       ? 1
			       : 0;
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_copy_directory(OakFileFunctions self,
					   const char *source,
					   const char *dest, int overwrite)
{
	if (self.ctx == nullptr || source == nullptr || dest == nullptr) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		FileFunctions::copy_directory(source, dest, overwrite != 0);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_directory_is_valid(
	OakFileFunctions self, const char *dir,
	int try_to_create_if_not_exists, int *out)
{
	if (self.ctx == nullptr || dir == nullptr || out == nullptr) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		*out = FileFunctions::directory_is_valid(
			       dir, try_to_create_if_not_exists != 0)
			       ? 1
			       : 0;
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_ensure_filename_extension(
	OakFileFunctions self, const char *filename,
	const char *extension, char *buf, int buf_size)
{
	if (self.ctx == nullptr || filename == nullptr || extension == nullptr ||
	    !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(
			FileFunctions::ensure_filename_extension(filename,
								 extension),
			buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_read_file_as_string(
	OakFileFunctions self, const char *filename, char *buf,
	int buf_size)
{
	if (self.ctx == nullptr || filename == nullptr ||
	    !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(
			FileFunctions::read_file_as_string(filename), buf,
			buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_get_safe_temporary_filename(
	OakFileFunctions self, const char *original, char *buf,
	int buf_size)
{
	if (self.ctx == nullptr || original == nullptr ||
	    !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(
			FileFunctions::get_safe_temporary_filename(original),
			buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_rename_file_allow_overwrite(
	OakFileFunctions self, const char *from, const char *to,
	int *out)
{
	if (self.ctx == nullptr || from == nullptr || to == nullptr ||
	    out == nullptr) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		*out = FileFunctions::rename_file_allow_overwrite(from, to) ? 1
									    : 0;
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_filefunctions_get_formatted_executable_for_platform(
	OakFileFunctions self, const char *unformatted, char *buf,
	int buf_size)
{
	if (self.ctx == nullptr || unformatted == nullptr ||
	    !is_valid_string_out(buf, buf_size)) {
		return OAKCOMMON_E_INVALID;
	}

	try {
		return write_string_result(
			FileFunctions::get_formatted_executable_for_platform(
				unformatted),
			buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
