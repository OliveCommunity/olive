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

#include "../src/project.h"

namespace
{

olive::Project *to_cpp(OakNodeProject *project)
{
	return reinterpret_cast<olive::Project *>(project);
}

const olive::Project *to_cpp(const OakNodeProject *project)
{
	return reinterpret_cast<const olive::Project *>(project);
}

olive::Node *to_cpp(OakNodeNode *node)
{
	return reinterpret_cast<olive::Node *>(node);
}

OakNodeNode *to_c(olive::Node *node)
{
	return reinterpret_cast<OakNodeNode *>(node);
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

OakNodeProject *oaknode_project_init(void)
{
	try {
		return reinterpret_cast<OakNodeProject *>(
			new (std::nothrow) olive::Project());
	} catch (...) {
		return NULL;
	}
}

void oaknode_project_free(OakNodeProject *project)
{
	delete to_cpp(project);
}

int oaknode_project_initialize(OakNodeProject *project)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		if (to_cpp(project)->root()) {
			return OAKNODE_E_STATE;
		}
		to_cpp(project)->initialize();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_clear(OakNodeProject *project)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(project)->clear();
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeFolder *oaknode_project_root(OakNodeProject *project)
{
	if (!project) {
		return NULL;
	}

	try {
		return reinterpret_cast<OakNodeFolder *>(to_cpp(project)->root());
	} catch (...) {
		return NULL;
	}
}

int oaknode_project_name(const OakNodeProject *project, char *buf, int buf_size)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(project)->name(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_filename(const OakNodeProject *project, char *buf,
							 int buf_size)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(project)->filename(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_pretty_filename(const OakNodeProject *project, char *buf,
									int buf_size)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(project)->pretty_filename(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_set_filename(OakNodeProject *project, const char *filename)
{
	if (!project || !filename) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(project)->set_filename(filename);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_is_modified(const OakNodeProject *project)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	return to_cpp(project)->is_modified() ? 1 : 0;
}

int oaknode_project_set_modified(OakNodeProject *project, int modified)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(project)->set_modified(modified != 0);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_is_new(const OakNodeProject *project)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	return to_cpp(project)->is_new() ? 1 : 0;
}

int oaknode_project_cache_path(const OakNodeProject *project, char *buf,
							   int buf_size)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(project)->cache_path(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_get_cache_location_setting(const OakNodeProject *project)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(to_cpp(project)->get_cache_location_setting());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_set_cache_location_setting(OakNodeProject *project,
											   int setting)
{
	if (!project || setting < 0 ||
		setting > static_cast<int>(olive::Project::k_cache_custom_path)) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(project)->set_cache_location_setting(
			static_cast<olive::Project::CacheSetting>(setting));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_get_custom_cache_path(const OakNodeProject *project,
										  char *buf, int buf_size)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(project)->get_custom_cache_path(), buf,
						   buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_set_custom_cache_path(OakNodeProject *project,
										  const char *path)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(project)->set_custom_cache_path(path ? path : "");
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_get_uuid(const OakNodeProject *project, char *buf,
							 int buf_size)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return copy_string(to_cpp(project)->get_uuid(), buf, buf_size);
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_add_node(OakNodeProject *project, OakNodeNode *node)
{
	if (!project || !node) {
		return OAKNODE_E_INVALID;
	}

	try {
		to_cpp(project)->add_node(to_cpp(node));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_remove_node(OakNodeProject *project, OakNodeNode *node)
{
	if (!project || !node) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Project *p = to_cpp(project);
		olive::Node *n = to_cpp(node);
		const auto &nodes = p->nodes();
		if (std::find(nodes.begin(), nodes.end(), n) == nodes.end()) {
			return OAKNODE_E_NOT_FOUND;
		}
		p->remove_node(n);
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

int oaknode_project_node_count(const OakNodeProject *project)
{
	if (!project) {
		return OAKNODE_E_INVALID;
	}

	try {
		return static_cast<int>(to_cpp(project)->nodes().size());
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}

OakNodeNode *oaknode_project_node_at(const OakNodeProject *project, int index)
{
	if (!project || index < 0) {
		return NULL;
	}

	try {
		const auto &nodes = to_cpp(project)->nodes();
		if (static_cast<size_t>(index) >= nodes.size()) {
			return NULL;
		}
		return to_c(nodes[static_cast<size_t>(index)]);
	} catch (...) {
		return NULL;
	}
}

int oaknode_project_copy_settings(OakNodeProject *dst,
								  const OakNodeProject *src)
{
	if (!dst || !src) {
		return OAKNODE_E_INVALID;
	}

	try {
		olive::Project::copy_settings(const_cast<olive::Project *>(to_cpp(src)), to_cpp(dst));
		return OAKNODE_OK;
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
}
