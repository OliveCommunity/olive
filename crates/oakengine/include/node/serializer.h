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

#ifndef OAK_EDITOR_NODE_SERIALIZER_H
#define OAK_EDITOR_NODE_SERIALIZER_H

#include <stdint.h>

#include "node/error.h"
#include "node/node.h"
#include "node/project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file serializer.h
 * @brief C ABI for olive::ProjectSerializer (oaknode), in-memory form
 *
 * Clipboard copy/paste and node-graph XML round trips without touching the
 * filesystem: "copy" is oaknode_serializer_save_to_xml() (serialize a
 * SaveData to an XML string), "paste" is oaknode_serializer_load_from_xml()
 * (parse an XML string into a project, exposing the resulting LoadData).
 * System-clipboard integration and on-disk .ove save/load live in the
 * facade / oakstorage layers (M9/M10), not here.
 *
 * oaknode_serializer_initialize() must be called before any save/load; it
 * registers the versioned serializers and the node factory the loaders use
 * to instantiate nodes by id.
 */

/** @brief Load type: a whole project. */
#define OAKNODE_SERIALIZER_LOAD_PROJECT 0
/** @brief Load type: only nodes (clipboard node-graph paste). */
#define OAKNODE_SERIALIZER_LOAD_ONLY_NODES 1
/** @brief Load type: only clips (timeline family). */
#define OAKNODE_SERIALIZER_LOAD_ONLY_CLIPS 2
/** @brief Load type: only markers (timeline family). */
#define OAKNODE_SERIALIZER_LOAD_ONLY_MARKERS 3
/** @brief Load type: only keyframes (keyframe family). */
#define OAKNODE_SERIALIZER_LOAD_ONLY_KEYFRAMES 4

/** @brief Serializer result code: success. */
#define OAKNODE_SERIALIZER_OK 0
/** @brief Serializer result code: data written by a too-old format. */
#define OAKNODE_SERIALIZER_TOO_OLD 1
/** @brief Serializer result code: data written by a too-new format. */
#define OAKNODE_SERIALIZER_TOO_NEW 2
/** @brief Serializer result code: unrecognizable format version. */
#define OAKNODE_SERIALIZER_UNKNOWN_VERSION 3
/** @brief Serializer result code: file I/O error (unused in-memory). */
#define OAKNODE_SERIALIZER_FILE_ERROR 4
/** @brief Serializer result code: XML parse error. */
#define OAKNODE_SERIALIZER_XML_ERROR 5
/** @brief Serializer result code: overwrite error (unused in-memory). */
#define OAKNODE_SERIALIZER_OVERWRITE_ERROR 6
/** @brief Serializer result code: no data to load. */
#define OAKNODE_SERIALIZER_NO_DATA 7

/**
 * @brief Reference-counted save descriptor (wraps
 * olive::ProjectSerializer::SaveData).
 *
 * oaknode_serializer_savedata_create() returns a handle whose object has
 * reference count 1; release it with oaknode_serializer_savedata_free().
 */
typedef struct OakNodeSerializerSaveData {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeSerializerSaveData;

/**
 * @brief Reference-counted load result (wraps
 * olive::ProjectSerializer::LoadData).
 *
 * The handle returned through oaknode_serializer_load_from_xml() has
 * reference count 1; release it with oaknode_serializer_loaddata_free().
 * Node handles obtained from it are borrowed from the target project.
 */
typedef struct OakNodeSerializerLoadData {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeSerializerLoadData;

/**
 * @brief Register the versioned serializers and initialize the node factory.
 * Idempotent. Must be called before any save/load.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_serializer_initialize(void);

/**
 * @brief Tear down the serializers and the node factory registered by
 * oaknode_serializer_initialize(). Safe to call when not initialized.
 */
void oaknode_serializer_shutdown(void);

/**
 * @brief Create a save descriptor.
 *
 * @param load_type One of OAKNODE_SERIALIZER_LOAD_*; use
 *        OAKNODE_SERIALIZER_LOAD_ONLY_NODES for clipboard-style node copies.
 * @param project Context project (borrowed), may be an empty handle for
 *        load types that do not require it.
 *
 * @return Save-data handle with reference count 1 (release with
 *         oaknode_serializer_savedata_free()); ctx is NULL on failure.
 */
OakNodeSerializerSaveData oaknode_serializer_savedata_create(
	int load_type, OakNodeProject project);

/**
 * @brief Release the caller's reference to the save descriptor and null
 * out the handle. NULL and empty handles are a no-op; the object is
 * destroyed when its reference count reaches zero.
 */
void oaknode_serializer_savedata_free(OakNodeSerializerSaveData *save_data);

/**
 * @brief Restrict serialization to the given nodes
 * (SaveData::set_only_serialize_nodes()). `nodes` is an array of `count`
 * borrowed node handles.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_serializer_savedata_set_nodes(
	OakNodeSerializerSaveData save_data, const OakNodeNode *nodes, int count);

/**
 * @brief Attach a free-form (key, value) property to a node in the
 * serialized output (SaveData::set_properties()); used for graph positions
 * and clip metadata. Replaces the value if the (node, key) pair exists.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_serializer_savedata_set_property(
	OakNodeSerializerSaveData save_data, OakNodeNode node, const char *key,
	const char *value);

/**
 * @brief Serialize to an in-memory XML document ("copy"). Two-stage string
 * getter: pass buf == NULL to query the size.
 *
 * @return Required buffer size in bytes including the NUL, or a negative
 *         OAKNODE_E_* error code (OAKNODE_E_STATE if the serializers have
 *         not been initialized).
 */
int oaknode_serializer_save_to_xml(OakNodeSerializerSaveData save_data,
								   char *buf, int buf_size);

/**
 * @brief Parse an in-memory XML document into `project` ("paste").
 *
 * @param project Target project (borrowed), may be an empty handle for
 *        load types that do not attach nodes to a project.
 * @param xml Complete XML document text. Must not be NULL.
 * @param load_type One of OAKNODE_SERIALIZER_LOAD_*.
 * @param out_result Receives one of the OAKNODE_SERIALIZER_* result codes.
 *        Must not be NULL.
 * @param out_load_data Receives the load result on OAKNODE_SERIALIZER_OK
 *        (reference count 1, release with oaknode_serializer_loaddata_free();
 *        may be NULL if the caller does not need it; receives an empty
 *        handle on failure).
 * @param details_buf Optional human-readable error detail buffer
 *        (two-stage convention is NOT used; truncation is silent). May be
 *        NULL.
 * @param details_buf_size Size of details_buf.
 *
 * @return OAKNODE_OK if the call itself succeeded (inspect *out_result for
 *         the serializer outcome), or a negative OAKNODE_E_* error code.
 */
int oaknode_serializer_load_from_xml(OakNodeProject project, const char *xml,
									 int load_type, int *out_result,
									 OakNodeSerializerLoadData *out_load_data,
									 char *details_buf, int details_buf_size);

/**
 * @brief Release the caller's reference to the load result and null out
 * the handle. NULL and empty handles are a no-op.
 *
 * Does not delete the loaded nodes: they are newly created objects owned by
 * the CALLER until adopted into a project with oaknode_project_add_node()
 * (or attached under a folder); otherwise they leak.
 */
void oaknode_serializer_loaddata_free(OakNodeSerializerLoadData *load_data);

/**
 * @brief Number of nodes created by the load. Negative OAKNODE_E_* code on
 * an empty handle.
 */
int oaknode_serializer_loaddata_node_count(
	OakNodeSerializerLoadData load_data);

/**
 * @brief Borrowed handle of the loaded node at `index`, or an empty handle
 * when out of range.
 */
OakNodeNode oaknode_serializer_loaddata_node_at(
	OakNodeSerializerLoadData load_data, int index);

/**
 * @brief Look up a serialized property attached to a loaded node.
 * Two-stage string getter.
 *
 * @return Required buffer size in bytes including the NUL,
 *         OAKNODE_E_NOT_FOUND if the (node, key) pair is absent, or another
 *         negative OAKNODE_E_* error code.
 */
int oaknode_serializer_loaddata_get_property(
	OakNodeSerializerLoadData load_data, OakNodeNode node, const char *key,
	char *buf, int buf_size);

/**
 * @brief Number of promised (deferred) connections in the load result.
 * Negative OAKNODE_E_* code on an empty handle.
 */
int oaknode_serializer_loaddata_connection_count(
	OakNodeSerializerLoadData load_data);

/**
 * @brief Read the promised connection at `index`.
 *
 * All output parameters except the input-id buffer are required;
 * `input_id_buf` follows the two-stage string convention inside a
 * fixed call: pass NULL/0 to skip copying the id.
 *
 * @param out_output_node Receives the output (source) node (borrowed).
 * @param out_input_node Receives the input (destination) node (borrowed).
 * @param input_id_buf Receives the input id string, may be NULL.
 * @param input_id_buf_size Size of input_id_buf.
 * @param out_element Receives the input element index.
 *
 * @return OAKNODE_OK, OAKNODE_E_NOT_FOUND when out of range, or another
 *         negative OAKNODE_E_* error code.
 */
int oaknode_serializer_loaddata_connection_at(
	OakNodeSerializerLoadData load_data, int index,
	OakNodeNode *out_output_node, OakNodeNode *out_input_node,
	char *input_id_buf, int input_id_buf_size, int *out_element);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_SERIALIZER_H

/**
 * @brief Result codes for file-level save/load (mirror
 *        ProjectSerializer::ResultCode; pinned by test).
 */
enum OakNodeSerializerResultCode {
	OAKNODE_SERIALIZER_RESULT_SUCCESS = 0,
	OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_OLD = 1,
	OAKNODE_SERIALIZER_RESULT_PROJECT_TOO_NEW = 2,
	OAKNODE_SERIALIZER_RESULT_UNKNOWN_VERSION = 3,
	OAKNODE_SERIALIZER_RESULT_FILE_ERROR = 4,
	OAKNODE_SERIALIZER_RESULT_XML_ERROR = 5,
	OAKNODE_SERIALIZER_RESULT_OVERWRITE_ERROR = 6,
	OAKNODE_SERIALIZER_RESULT_NO_DATA = 7
};

/**
 * @brief Save a project to a file (ProjectSerializer::save(), project
 *        type, optional OVEC compression). Layout data is not serialized
 *        through this API (app-layer concern, see oakstorage/M10).
 *
 * @param out_code Receives an OakNodeSerializerResultCode (may be NULL).
 * @param details Optional two-stage buffer for the result details
 *        string (e.g. the fallback filename on overwrite errors).
 * @return OAKNODE_OK when the result code is
 *         OAKNODE_SERIALIZER_RESULT_SUCCESS, OAKNODE_E_FAILED otherwise
 *         (details in out_code/details), OAKNODE_E_INVALID for empty
 *         handles/NULL args.
 */
int oaknode_serializer_save_to_file(OakNodeProject project,
		const char *filename, int use_compression, int *out_code,
		char *details, int details_size);

/**
 * @brief Load a project from a file into `project`
 *        (ProjectSerializer::load(), project type).
 *
 * Same return/out-param convention as oaknode_serializer_save_to_file().
 */
int oaknode_serializer_load_from_file(OakNodeProject project,
		const char *filename, int *out_code, char *details,
		int details_size);
