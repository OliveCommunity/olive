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

#include "node/project.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

#include "node/node.h"
#include "node/folder.h"

#include "../src/project.h"
#include "../src/project/folder/folder.h"

#include "nodehandle.h"

namespace
{

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

using oaknode_c_api::delete_as;
using oaknode_c_api::make_handle;
using oaknode_c_api::to_native;

OakNodeProject oaknode_project_init(void)
{
	try {
		return make_handle<OakNodeProject>(new (std::nothrow) olive::Project(),
										   true,
										   &delete_as<olive::Project>);
	} catch (...) {
		return OakNodeProject{};
	}
}

void oaknode_project_free(OakNodeProject *project)
{
	oaknode_c_api::free_handle(project);
}

int oaknode_project_initialize(OakNodeProject project)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Project *p = to_native<olive::Project>(project);
		if (p->root()) {
			return OAKNODE_E_STATE;
		}
		p->initialize();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_clear(OakNodeProject project)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_native<olive::Project>(project)->clear();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeFolder oaknode_project_root(OakNodeProject project)
{
	if (!project.ctx) {
		return OakNodeFolder{};
	}

	try {
		return make_handle<OakNodeFolder>(
			to_native<olive::Project>(project)->root(), false,
			&delete_as<olive::Folder>);
	} catch (...) {
		return OakNodeFolder{};
	}
}

int oaknode_project_name(OakNodeProject project, char *buf, int buf_size)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_native<olive::Project>(project)->name(), buf,
						   buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_filename(OakNodeProject project, char *buf,
							 int buf_size)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_native<olive::Project>(project)->filename(), buf,
						   buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_pretty_filename(OakNodeProject project, char *buf,
									int buf_size)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_native<olive::Project>(project)->pretty_filename(),
						   buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_set_filename(OakNodeProject project, const char *filename)
{
	if (!project.ctx || !filename) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_native<olive::Project>(project)->set_filename(filename);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_is_modified(OakNodeProject project)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	return to_native<olive::Project>(project)->is_modified() ? 1 : 0;
}

int oaknode_project_set_modified(OakNodeProject project, int modified)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_native<olive::Project>(project)->set_modified(modified != 0);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_is_new(OakNodeProject project)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	return to_native<olive::Project>(project)->is_new() ? 1 : 0;
}

int oaknode_project_cache_path(OakNodeProject project, char *buf,
							   int buf_size)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_native<olive::Project>(project)->cache_path(),
						   buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_get_cache_location_setting(OakNodeProject project)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(
			to_native<olive::Project>(project)->get_cache_location_setting());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_set_cache_location_setting(OakNodeProject project,
											   int setting)
{
	if (!project.ctx || setting < 0 ||
		setting > static_cast<int>(olive::Project::k_cache_custom_path)) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_native<olive::Project>(project)->set_cache_location_setting(
			static_cast<olive::Project::CacheSetting>(setting));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_get_custom_cache_path(OakNodeProject project,
										  char *buf, int buf_size)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(
			to_native<olive::Project>(project)->get_custom_cache_path(), buf,
			buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_set_custom_cache_path(OakNodeProject project,
										  const char *path)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_native<olive::Project>(project)->set_custom_cache_path(
			path ? path : "");
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_get_uuid(OakNodeProject project, char *buf,
							 int buf_size)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_native<olive::Project>(project)->get_uuid(), buf,
						   buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_add_node(OakNodeProject project, OakNodeNode node)
{
	if (!project.ctx || !node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_native<olive::Project>(project)->add_node(
			to_native<olive::Node>(node));
		// The graph now owns the node; releasing `node` must not delete it.
		oaknode_c_api::mark_container_owned(node);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_remove_node(OakNodeProject project, OakNodeNode node)
{
	if (!project.ctx || !node.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Project *p = to_native<olive::Project>(project);
		olive::Node *n = to_native<olive::Node>(node);
		const auto &nodes = p->nodes();
		if (std::find(nodes.begin(), nodes.end(), n) == nodes.end()) {
			return OAKNODE_E_NOT_FOUND;
		}
		// Detach without deleting (legacy semantics); the caller's handle
		// keeps its current ownership state.
		p->remove_node(n);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_node_count(OakNodeProject project)
{
	if (!project.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(
			to_native<olive::Project>(project)->nodes().size());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode oaknode_project_node_at(OakNodeProject project, int index)
{
	if (!project.ctx || index < 0) {
		return OakNodeNode{};
	}

	try {
		const auto &nodes = to_native<olive::Project>(project)->nodes();
		if (static_cast<size_t>(index) >= nodes.size()) {
			return OakNodeNode{};
		}
		return make_handle<OakNodeNode>(nodes[static_cast<size_t>(index)],
										false, &delete_as<olive::Node>);
	} catch (...) {
		return OakNodeNode{};
	}
}

int oaknode_project_copy_settings(OakNodeProject dst,
								  OakNodeProject src)
{
	if (!dst.ctx || !src.ctx) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Project::copy_settings(to_native<olive::Project>(src),
									  to_native<olive::Project>(dst));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
