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

#include "render/copier.h"

#include <new>

#include "../../node/c_api/nodehandle.h"
#include "../src/projectcopier.h"
#include "internalhandles.h"

namespace
{

olive::ProjectCopier *impl(OakRenderProjectCopier h)
{
	return oakrender_c_api::to_native<olive::ProjectCopier>(h);
}

} // namespace

OakRenderProjectCopier oakrender_project_copier_create(void)
{
	try {
		return oakrender_c_api::make_handle<OakRenderProjectCopier>(
			new olive::ProjectCopier(), true,
			&oakrender_c_api::delete_as<olive::ProjectCopier>);
	} catch (...) {
		return OakRenderProjectCopier{};
	}
}

void oakrender_project_copier_free(OakRenderProjectCopier *copier)
{
	oakrender_c_api::free_handle(copier);
}

int oakrender_project_copier_set_project(OakRenderProjectCopier copier,
										 OakNodeProject project)
{
	if (!copier.ctx || !project.ctx) {
		return OAKRENDER_E_INVALID;
	}
	try {
		impl(copier)->set_project(
			oaknode_c_api::to_native<olive::Project>(project));
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

OakNodeNode oakrender_project_copier_get_copy(
	OakRenderProjectCopier copier, OakNodeNode original)
{
	if (!copier.ctx || !original.ctx) {
		return OakNodeNode{};
	}
	// Borrowed handle: releasing it only destroys the handle box, never
	// the copied node (owned by the copier's copied project).
	return oaknode_c_api::make_handle<OakNodeNode>(
		impl(copier)->get_copy(oaknode_c_api::to_native<olive::Node>(original)),
		false, &oaknode_c_api::delete_as<olive::Node>);
}

OakNodeProject oakrender_project_copier_get_copied_project(
	OakRenderProjectCopier copier)
{
	if (!copier.ctx) {
		return OakNodeProject{};
	}
	// Borrowed handle: the copied project is owned by the copier.
	return oaknode_c_api::make_handle<OakNodeProject>(
		impl(copier)->get_copied_project(), false,
		&oaknode_c_api::delete_as<olive::Project>);
}
