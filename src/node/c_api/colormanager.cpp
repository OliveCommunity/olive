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

#include "node/colormanager.h"

#include <cstring>

#include "alivecount.h"

#include "color/colormanager/colormanager.h"
#include "colortransform.h"
#include "project.h"

struct OakNodeColorManager {
	olive::ColorManager impl;
};

namespace
{

int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = int(value.size()) + 1;
	if (buf && buf_size >= needed) {
		memcpy(buf, value.c_str(), needed);
	}
	return needed;
}

bool has_config(olive::ColorManager *cm)
{
	return cm && cm->get_config() != nullptr;
}

int list_at(const olive::StringList &list, int index, char *buf, int buf_size)
{
	if (index < 0 || index >= int(list.size())) {
		return OAKNODE_E_NOT_FOUND;
	}
	return copy_string(list.at(index), buf, buf_size);
}

} // namespace

OakNodeColorManager *oaknode_colormanager_init(OakNodeProject *project)
{
	if (!project) {
		return nullptr;
	}
	try {
		auto *m = new OakNodeColorManager{
			olive::ColorManager(reinterpret_cast<olive::Project *>(project))};
		oaknode_c_api::alive_inc();
		return m;
	} catch (...) {
		return nullptr;
	}
}

void oaknode_colormanager_free(OakNodeColorManager *manager)
{
	if (!manager) {
		return;
	}
	delete manager;
	oaknode_c_api::alive_dec();
}

int oaknode_colormanager_initialize(OakNodeColorManager *manager)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	try {
		manager->impl.init();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_colormanager_set_up_default_config(void)
{
	try {
		olive::ColorManager::set_up_default_config();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_colormanager_get_config_filename(OakNodeColorManager *manager,
											 char *buf, int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	return copy_string(manager->impl.get_config_filename(), buf, buf_size);
}

int oaknode_colormanager_set_config_filename(OakNodeColorManager *manager,
											 const char *filename)
{
	if (!manager || !filename) {
		return OAKNODE_E_INVALID;
	}
	manager->impl.set_config_filename(filename);
	return OAKNODE_OK;
}

int oaknode_colormanager_update_config_from_filename(
	OakNodeColorManager *manager)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	try {
		manager->impl.update_config_from_filename();
	} catch (...) {
		return OAKNODE_E_FAILED;
	}
	return OAKNODE_OK;
}

int oaknode_colormanager_get_default_input_color_space(
	OakNodeColorManager *manager, char *buf, int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	return copy_string(manager->impl.get_default_input_color_space(), buf,
					   buf_size);
}

int oaknode_colormanager_set_default_input_color_space(
	OakNodeColorManager *manager, const char *colorspace)
{
	if (!manager || !colorspace) {
		return OAKNODE_E_INVALID;
	}
	manager->impl.set_default_input_color_space(colorspace);
	return OAKNODE_OK;
}

int oaknode_colormanager_get_reference_color_space(
	OakNodeColorManager *manager, char *buf, int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	return copy_string(manager->impl.get_reference_color_space(), buf,
					   buf_size);
}

int oaknode_colormanager_get_compliant_color_space(
	OakNodeColorManager *manager, const char *colorspace, char *buf,
	int buf_size)
{
	if (!manager || !colorspace) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return copy_string(manager->impl.get_compliant_color_space(colorspace), buf,
					   buf_size);
}

int oaknode_colormanager_get_colorspace_for_ffmpeg_tags(
	OakNodeColorManager *manager, int primaries, int trc, char *buf,
	int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return copy_string(
		manager->impl.get_colorspace_for_ffmpeg_tags(primaries, trc), buf,
		buf_size);
}

int oaknode_colormanager_get_display_count(OakNodeColorManager *manager,
										   int *count)
{
	if (!manager || !count) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	*count = int(manager->impl.list_available_displays().size());
	return OAKNODE_OK;
}

int oaknode_colormanager_get_display_at(OakNodeColorManager *manager,
										int index, char *buf, int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return list_at(manager->impl.list_available_displays(), index, buf,
				   buf_size);
}

int oaknode_colormanager_get_default_display(OakNodeColorManager *manager,
											 char *buf, int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return copy_string(manager->impl.get_default_display(), buf, buf_size);
}

int oaknode_colormanager_get_view_count(OakNodeColorManager *manager,
										const char *display, int *count)
{
	if (!manager || !display || !count) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	*count = int(manager->impl.list_available_views(display).size());
	return OAKNODE_OK;
}

int oaknode_colormanager_get_view_at(OakNodeColorManager *manager,
									 const char *display, int index, char *buf,
									 int buf_size)
{
	if (!manager || !display) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return list_at(manager->impl.list_available_views(display), index, buf,
				   buf_size);
}

int oaknode_colormanager_get_default_view(OakNodeColorManager *manager,
										  const char *display, char *buf,
										  int buf_size)
{
	if (!manager || !display) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return copy_string(manager->impl.get_default_view(display), buf, buf_size);
}

int oaknode_colormanager_get_look_count(OakNodeColorManager *manager,
										int *count)
{
	if (!manager || !count) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	*count = int(manager->impl.list_available_looks().size());
	return OAKNODE_OK;
}

int oaknode_colormanager_get_look_at(OakNodeColorManager *manager, int index,
									 char *buf, int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return list_at(manager->impl.list_available_looks(), index, buf, buf_size);
}

int oaknode_colormanager_get_colorspace_count(OakNodeColorManager *manager,
											  int *count)
{
	if (!manager || !count) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	*count = int(manager->impl.list_available_colorspaces().size());
	return OAKNODE_OK;
}

int oaknode_colormanager_get_colorspace_at(OakNodeColorManager *manager,
										   int index, char *buf,
										   int buf_size)
{
	if (!manager) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	return list_at(manager->impl.list_available_colorspaces(), index, buf,
				   buf_size);
}

int oaknode_colormanager_get_default_luma_coefs(OakNodeColorManager *manager,
												double rgb[3])
{
	if (!manager || !rgb) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	manager->impl.get_default_luma_coefs(rgb);
	return OAKNODE_OK;
}

int oaknode_colormanager_get_compliant_color_transform(
	OakNodeColorManager *manager, OakColorTransform transform,
	int force_display, OakColorTransform *out)
{
	if (!manager || !out) {
		return OAKNODE_E_INVALID;
	}
	const olive::ColorTransform *native =
		oakcommon_colortransform_get_native(transform);
	if (!native) {
		return OAKNODE_E_INVALID;
	}
	if (!has_config(&manager->impl)) {
		return OAKNODE_E_STATE;
	}
	try {
		const olive::ColorTransform compliant =
			manager->impl.get_compliant_color_space(*native,
													force_display != 0);
		*out = oakcommon_colortransform_init_from_native(&compliant);
	} catch (...) {
		return OAKNODE_E_NOMEM;
	}
	if (!out->ctx) {
		return OAKNODE_E_NOMEM;
	}
	return OAKNODE_OK;
}
