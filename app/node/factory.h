/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_NODEFACTORY_H
#define OAK_NODEFACTORY_H

#include <QList>

#include "node.h"

namespace olive
{

class NodeFactory {
public:
	enum InternalID {
		k_viewer_output,
		k_clip_block,
		k_gap_block,
		k_polygon_generator,
		k_matrix_generator,
		k_transform_distort,
		k_track_output,
		k_audio_volume,
		k_audio_panning,
		k_math,
		k_time,
		k_trigonometry,
		k_blur_filter,
		k_solid_generator,
		k_merge,
		k_stroke_filter,
		k_text_generator_v1,
		k_text_generator_v2,
		k_text_generator_v3,
		k_cross_dissolve_transition,
		k_dip_to_color_transition,
		k_mosaic_filter,
		k_crop_distort,
		k_project_footage,
		k_project_folder,
		k_project_sequence,
		k_value_node,
		k_time_remap_node,
		k_subtitle_block,
		k_shape_generator,
		k_color_difference_key_keying,
		k_despill_keying,
		k_group_node,
		k_opacity_effect,
		k_flip_distort,
		k_noise_generator,
		k_time_offset_node,
		k_corner_pin_distort,
		k_display_transform,
		k_ocio_grading_transform_linear,
		k_ocio_lut,
		k_three_way_color,
		k_chroma_key,
		k_mask_distort,
		k_drop_shadow_filter,
		k_time_format,
		k_wave_distort,
		k_ripple_distort,
		k_tile_distort,
		k_swirl_distort,
		k_multicam_node,
		k_ocio_grading_transform_log,
		k_white_balance,

		// Count value
		k_internal_node_count
	};

	NodeFactory() = default;

	static void initialize();

	static void destroy();

	static QString get_name_from_id(const QString &id);

	static Node *create_from_id(const QString &id);
	static void register_plugin_nodes();

	static Node *create_from_factory_index(const InternalID &id);

	/**
	 * @brief Access the internal node library
	 *
	 * Exposed for the UI layer (e.g. widget/menu/factorymenu), which builds
	 * node creation menus from the library. The library itself stays
	 * UI-independent.
	 */
	static const QList<Node *> &get_library()
	{
		return library;
	}

private:
	static QList<Node *> library;
};

}

#endif // OAK_NODEFACTORY_H
