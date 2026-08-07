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

#include "../src/projectcopier.h"

namespace
{

olive::ProjectCopier *impl(OakRenderProjectCopier *h)
{
	return reinterpret_cast<olive::ProjectCopier *>(h);
}

} // namespace

OakRenderProjectCopier *oakrender_project_copier_create(void)
{
	try {
		return reinterpret_cast<OakRenderProjectCopier *>(
			new olive::ProjectCopier());
	} catch (...) {
		return NULL;
	}
}

void oakrender_project_copier_free(OakRenderProjectCopier *copier)
{
	delete impl(copier);
}

int oakrender_project_copier_set_project(OakRenderProjectCopier *copier,
										 OakNodeProject *project)
{
	if (!copier || !project) {
		return OAKRENDER_E_INVALID;
	}
	try {
		impl(copier)->set_project(
			reinterpret_cast<olive::Project *>(project));
		return OAKRENDER_OK;
	} catch (...) {
		return OAKRENDER_E_FAILED;
	}
}

OakNodeNode *oakrender_project_copier_get_copy(
	OakRenderProjectCopier *copier, OakNodeNode *original)
{
	if (!copier || !original) {
		return NULL;
	}
	return reinterpret_cast<OakNodeNode *>(
		impl(copier)->get_copy(reinterpret_cast<olive::Node *>(original)));
}

OakNodeProject *oakrender_project_copier_get_copied_project(
	OakRenderProjectCopier *copier)
{
	if (!copier) {
		return NULL;
	}
	return reinterpret_cast<OakNodeProject *>(
		impl(copier)->get_copied_project());
}
