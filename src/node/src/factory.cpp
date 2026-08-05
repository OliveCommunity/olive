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

#include "factory.h"

#include <cstdio>
#include <map>
#include <set>

#include "audio/pan/pan.h"
#include "audio/volume/volume.h"
#include "block/clip/clip.h"
#include "block/gap/gap.h"
#include "block/subtitle/subtitle.h"
#include "block/transition/crossdissolve/crossdissolvetransition.h"
#include "block/transition/diptocolor/diptocolortransition.h"
#include "color/displaytransform/displaytransform.h"
#include "color/ociogradingtransformlinear/ociogradingtransformlinear.h"
#include "color/ociogradingtransformlog/ociogradingtransformlog.h"
#include "color/ociolut/ociolut.h"
#include "color/threewaycolor/threewaycolor.h"
#include "color/whitebalance/whitebalance.h"
#include "current.h"
#include "distort/cornerpin/cornerpindistortnode.h"
#include "distort/crop/cropdistortnode.h"
#include "distort/flip/flipdistortnode.h"
#include "distort/mask/mask.h"
#include "distort/ripple/rippledistortnode.h"
#include "distort/swirl/swirldistortnode.h"
#include "distort/tile/tiledistortnode.h"
#include "distort/transform/transformdistortnode.h"
#include "distort/wave/wavedistortnode.h"
#include "effect/opacity/opacityeffect.h"
#include "filter/blur/blur.h"
#include "filter/dropshadow/dropshadowfilter.h"
#include "filter/mosaic/mosaicfilternode.h"
#include "filter/stroke/stroke.h"
#include "generator/matrix/matrix.h"
#include "generator/noise/noise.h"
#include "generator/polygon/polygon.h"
#include "generator/shape/shapenode.h"
#include "generator/solid/solid.h"
#include "generator/text/textv1.h"
#include "generator/text/textv2.h"
#include "generator/text/textv3.h"
#include "input/multicam/multicamnode.h"
#include "input/time/timeinput.h"
#include "input/value/valuenode.h"
#include "keying/chromakey/chromakey.h"
#include "keying/colordifferencekey/colordifferencekey.h"
#include "keying/despill/despill.h"
#include "math/math/math.h"
#include "math/merge/merge.h"
#include "math/trigonometry/trigonometry.h"
#include "output/track/track.h"
#include "output/viewer/viewer.h"
#include "pluginSupport/olivehost.h"
#include "plugins/plugin.h"
#include "project/folder/folder.h"
#include "project/footage/footage.h"
#include "project/sequence/sequence.h"
#include "time/timeformat/timeformat.h"
#include "time/timeoffset/timeoffsetnode.h"
#include "time/timeremap/timeremap.h"

namespace olive
{

std::vector<Node *> NodeFactory::library;

void NodeFactory::initialize()
{
	destroy();

	// Add internal types
	for (int i = 0; i < k_internal_node_count; i++) {
		Node *created_node = create_from_factory_index(static_cast<InternalID>(i));

		library.push_back(created_node);
	}

	register_plugin_nodes();
}

void NodeFactory::destroy()
{
	for (Node *n : library) {
		delete n;
	}
	library.clear();
}

std::string NodeFactory::get_name_from_id(const std::string &id)
{
	if (!id.empty()) {
		for (Node *n : library) {
			if (n->id() == id) {
				return n->name();
			}
		}
	}

	return std::string();
}

Node *NodeFactory::create_from_id(const std::string &id)
{
	std::string resolved_id = id;

	// Node IDs renamed after older project files were written
	static const std::map<std::string, std::string> k_legacy_i_ds = {
		{ "org.oliveeditor.Olive.flip",
		  "org.olivevideoeditor.Olive.flip" },
		{ "org.oliveeditor.Olive.ripple",
		  "org.olivevideoeditor.Olive.ripple" },
		{ "org.oliveeditor.Olive.swirl",
		  "org.olivevideoeditor.Olive.swirl" },
		{ "org.oliveeditor.Olive.tile",
		  "org.olivevideoeditor.Olive.tile" },
		{ "org.oliveeditor.Olive.wave",
		  "org.olivevideoeditor.Olive.wave" },
	};
	auto legacy_it = k_legacy_i_ds.find(id);
	if (legacy_it != k_legacy_i_ds.end()) {
		resolved_id = legacy_it->second;
	}

	for (Node *n : library) {
		if (n->id() == resolved_id) {
			return n->copy();
		}
	}

	return nullptr;
}

void NodeFactory::register_plugin_nodes()
{
	std::set<std::string> existing_ids;
	for (Node *node : library) {
		existing_ids.insert(node->id());
	}

	for (auto plugin : OFX::Host::PluginCache::getPluginCache()->getPlugins()) {
		auto *image_effect =
			dynamic_cast<OFX::Host::ImageEffect::ImageEffectPlugin *>(plugin);
		if (!image_effect) {
			continue;
		}

		const std::string plugin_id = image_effect->getIdentifier();
		if (existing_ids.count(plugin_id)) {
			continue;
		}

		const auto &contexts = image_effect->getContexts();
		if (contexts.empty()) {
			fprintf(stderr, "Skipping OFX plugin with no contexts: %s\n",
					plugin_id.c_str());
			continue;
		}
		std::string context = kOfxImageEffectContextFilter;
		if (contexts.find(kOfxImageEffectContextFilter) == contexts.end()) {
			context = *contexts.begin();
		}

		auto *instance = image_effect->createInstance(context, nullptr);
		if (!instance) {
			continue;
		}

		plugin::PluginNode *plugin_node = new plugin::PluginNode(instance);
		library.push_back(plugin_node);
		existing_ids.insert(plugin_id);
	}
}

Node *NodeFactory::create_from_factory_index(const NodeFactory::InternalID &id)
{
	switch (id) {
	case k_clip_block:
		return new ClipBlock();
	case k_gap_block:
		return new GapBlock();
	case k_polygon_generator:
		return new PolygonGenerator();
	case k_matrix_generator:
		return new MatrixGenerator();
	case k_transform_distort:
		return new TransformDistortNode();
	case k_track_output:
		return new Track();
	case k_viewer_output:
		return new ViewerOutput();
	case k_audio_volume:
		return new VolumeNode();
	case k_audio_panning:
		return new PanNode();
	case k_math:
		return new MathNode();
	case k_trigonometry:
		return new TrigonometryNode();
	case k_time:
		return new TimeInput();
	case k_blur_filter:
		return new BlurFilterNode();
	case k_solid_generator:
		return new SolidGenerator();
	case k_merge:
		return new MergeNode();
	case k_stroke_filter:
		return new StrokeFilterNode();
	case k_text_generator_v1:
		return new TextGeneratorV1();
	case k_text_generator_v2:
		return new TextGeneratorV2();
	case k_text_generator_v3:
		return new TextGeneratorV3();
	case k_cross_dissolve_transition:
		return new CrossDissolveTransition();
	case k_dip_to_color_transition:
		return new DipToColorTransition();
	case k_mosaic_filter:
		return new MosaicFilterNode();
	case k_crop_distort:
		return new CropDistortNode();
	case k_project_footage:
		return new Footage();
	case k_project_folder:
		return new Folder();
	case k_project_sequence:
		return new Sequence();
	case k_value_node:
		return new ValueNode();
	case k_time_remap_node:
		return new TimeRemapNode();
	case k_subtitle_block:
		return new SubtitleBlock();
	case k_shape_generator:
		return new ShapeNode();
	case k_color_difference_key_keying:
		return new ColorDifferenceKeyNode();
	case k_despill_keying:
		return new DespillNode();
	case k_group_node:
		return new NodeGroup();
	case k_opacity_effect:
		return new OpacityEffect();
	case k_flip_distort:
		return new FlipDistortNode();
	case k_noise_generator:
		return new NoiseGeneratorNode();
	case k_time_offset_node:
		return new TimeOffsetNode();
	case k_corner_pin_distort:
		return new CornerPinDistortNode();
	case k_display_transform:
		return new DisplayTransformNode();
	case k_ocio_grading_transform_linear:
		return new OCIOGradingTransformLinearNode();
	case k_ocio_grading_transform_log:
		return new OCIOGradingTransformLogNode();
	case k_white_balance:
		return new WhiteBalanceNode();
	case k_ocio_lut:
		return new OCIOLutNode();
	case k_three_way_color:
		return new ThreeWayColorNode();
	case k_chroma_key:
		return new ChromaKeyNode();
	case k_mask_distort:
		return new MaskDistortNode();
	case k_drop_shadow_filter:
		return new DropShadowFilter();
	case k_time_format:
		return new TimeFormatNode();
	case k_wave_distort:
		return new WaveDistortNode();
	case k_tile_distort:
		return new TileDistortNode();
	case k_swirl_distort:
		return new SwirlDistortNode();
	case k_ripple_distort:
		return new RippleDistortNode();
	case k_multicam_node:
		return new MultiCamNode();

	case k_internal_node_count:
		break;
	}

	return nullptr;
}

}
