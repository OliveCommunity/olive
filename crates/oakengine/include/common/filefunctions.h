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
#include "common/handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Neutral by-value handle for the filefunctions family
 *
 * File functions are stateless; the handle only exists to keep the C API
 * shape uniform across oakcommon families. Ownership/count semantics
 * follow the convention in common/handle.h: init returns a handle whose
 * (empty) object has reference count 1, addref(ctx)/release(ctx) adjust
 * it atomically, and release destroys it at zero. abi_version is always
 * OAKCOMMON_ABI_VERSION.
 */
typedef struct OakFileFunctions {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakFileFunctions;

/**
 * @brief Creates a filefunctions handle
 *
 * @return Handle with reference count 1; ctx is NULL on failure.
 */
OakFileFunctions oakcommon_filefunctions_init(void);

/**
 * @brief Releases one reference to a filefunctions handle
 *
 * Convenience wrapper around handle.release(handle.ctx): decrements the
 * atomic reference count and destroys the object when it reaches zero.
 * No-op when self is NULL or self->ctx is NULL.
 */
void oakcommon_filefunctions_free(OakFileFunctions *self);

/**
 * @brief Returns a deterministic identifier string for a file
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code. Returns an empty string (required size 1) if
 * the file does not exist.
 */
int oakcommon_filefunctions_get_unique_file_identifier(
	OakFileFunctions self, const char *filename, char *buf,
	int buf_size);

int oakcommon_filefunctions_get_configuration_location(
	OakFileFunctions self, char *buf, int buf_size);

int oakcommon_filefunctions_get_application_path(
	OakFileFunctions self, char *buf, int buf_size);

int oakcommon_filefunctions_get_temp_file_path(
	OakFileFunctions self, char *buf, int buf_size);

int oakcommon_filefunctions_get_auto_recovery_root(
	OakFileFunctions self, char *buf, int buf_size);

/**
 * @brief Checks whether `source` can be copied to `dest` without
 * overwriting anything
 *
 * @param out Receives 1 (safe) or 0 (would overwrite).
 */
int oakcommon_filefunctions_can_copy_directory_without_overwriting(
	OakFileFunctions self, const char *source, const char *dest,
	int *out);

/**
 * @brief Recursively copies a directory
 */
int oakcommon_filefunctions_copy_directory(OakFileFunctions self,
					   const char *source,
					   const char *dest, int overwrite);

/**
 * @brief Checks whether a directory exists, optionally creating it
 *
 * @param out Receives 1 (valid) or 0 (invalid).
 */
int oakcommon_filefunctions_directory_is_valid(
	OakFileFunctions self, const char *dir,
	int try_to_create_if_not_exists, int *out);

/**
 * @brief Ensures a filename ends with the given extension (no dot)
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code.
 */
int oakcommon_filefunctions_ensure_filename_extension(
	OakFileFunctions self, const char *filename,
	const char *extension, char *buf, int buf_size);

/**
 * @brief Reads an entire file into a string
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code. Returns an empty string (required size 1) if
 * the file cannot be read.
 */
int oakcommon_filefunctions_read_file_as_string(
	OakFileFunctions self, const char *filename, char *buf,
	int buf_size);

/**
 * @brief Returns a non-existing temporary variant of `original`
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code.
 */
int oakcommon_filefunctions_get_safe_temporary_filename(
	OakFileFunctions self, const char *original, char *buf,
	int buf_size);

/**
 * @brief Renames `from` to `to`, deleting `to` first if it exists
 *
 * @param out Receives 1 (renamed) or 0 (failed).
 */
int oakcommon_filefunctions_rename_file_allow_overwrite(
	OakFileFunctions self, const char *from, const char *to,
	int *out);

/**
 * @brief Appends the platform executable suffix (".exe" on Windows)
 *
 * @return Required buffer size in bytes (including NUL), or a negative
 * OAKCOMMON_E_* error code.
 */
int oakcommon_filefunctions_get_formatted_executable_for_platform(
	OakFileFunctions self, const char *unformatted, char *buf,
	int buf_size);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_FILEFUNCTIONS_H
