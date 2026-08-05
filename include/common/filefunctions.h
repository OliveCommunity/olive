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

#ifndef OAK_EDITOR_FILEFUNCTIONS_H
#define OAK_EDITOR_FILEFUNCTIONS_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for the filefunctions family
 *
 * File functions are stateless; the handle only exists to keep the C API
 * shape uniform across oakcommon families.
 */
typedef struct OakCommonFileFunctions OakCommonFileFunctions;

/**
 * @brief Creates a filefunctions handle
 *
 * @return A new handle, or NULL on failure.
 */
OakCommonFileFunctions *oakcommon_filefunctions_init(void);

/**
 * @brief Destroys a filefunctions handle (NULL is a no-op)
 */
void oakcommon_filefunctions_free(OakCommonFileFunctions *self);

/**
 * @brief Returns a deterministic identifier string for a file
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code. Returns an empty string (required size 1) if
 * the file does not exist.
 */
int oakcommon_filefunctions_get_unique_file_identifier(
	OakCommonFileFunctions *self, const char *filename, char *buf,
	int buf_size);

int oakcommon_filefunctions_get_configuration_location(
	OakCommonFileFunctions *self, char *buf, int buf_size);

int oakcommon_filefunctions_get_application_path(
	OakCommonFileFunctions *self, char *buf, int buf_size);

int oakcommon_filefunctions_get_temp_file_path(
	OakCommonFileFunctions *self, char *buf, int buf_size);

int oakcommon_filefunctions_get_auto_recovery_root(
	OakCommonFileFunctions *self, char *buf, int buf_size);

/**
 * @brief Checks whether `source` can be copied to `dest` without
 * overwriting anything
 *
 * @param out Receives 1 (safe) or 0 (would overwrite).
 */
int oakcommon_filefunctions_can_copy_directory_without_overwriting(
	OakCommonFileFunctions *self, const char *source, const char *dest,
	int *out);

/**
 * @brief Recursively copies a directory
 */
int oakcommon_filefunctions_copy_directory(OakCommonFileFunctions *self,
					   const char *source,
					   const char *dest, int overwrite);

/**
 * @brief Checks whether a directory exists, optionally creating it
 *
 * @param out Receives 1 (valid) or 0 (invalid).
 */
int oakcommon_filefunctions_directory_is_valid(
	OakCommonFileFunctions *self, const char *dir,
	int try_to_create_if_not_exists, int *out);

/**
 * @brief Ensures a filename ends with the given extension (no dot)
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code.
 */
int oakcommon_filefunctions_ensure_filename_extension(
	OakCommonFileFunctions *self, const char *filename,
	const char *extension, char *buf, int buf_size);

/**
 * @brief Reads an entire file into a string
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code. Returns an empty string (required size 1) if
 * the file cannot be read.
 */
int oakcommon_filefunctions_read_file_as_string(
	OakCommonFileFunctions *self, const char *filename, char *buf,
	int buf_size);

/**
 * @brief Returns a non-existing temporary variant of `original`
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code.
 */
int oakcommon_filefunctions_get_safe_temporary_filename(
	OakCommonFileFunctions *self, const char *original, char *buf,
	int buf_size);

/**
 * @brief Renames `from` to `to`, deleting `to` first if it exists
 *
 * @param out Receives 1 (renamed) or 0 (failed).
 */
int oakcommon_filefunctions_rename_file_allow_overwrite(
	OakCommonFileFunctions *self, const char *from, const char *to,
	int *out);

/**
 * @brief Appends the platform executable suffix (".exe" on Windows)
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code.
 */
int oakcommon_filefunctions_get_formatted_executable_for_platform(
	OakCommonFileFunctions *self, const char *unformatted, char *buf,
	int buf_size);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_FILEFUNCTIONS_H
