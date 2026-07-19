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

#include <QCoreApplication>
#include <QHash>

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
#include "common/current.h"
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

QList<Node *> NodeFactory::library;

void NodeFactory::initialize()
{
	destroy();

	// Add internal types
	for (int i = 0; i < k_internal_node_count; i++) {
		Node *created_node = create_from_factory_index(static_cast<InternalID>(i));

		library.append(created_node);
	}

	register_plugin_nodes();
}

void NodeFactory::destroy()
{
	qDeleteAll(library);
	library.clear();
}

Menu *NodeFactory::create_menu(QWidget *parent, bool create_none_item,
							  Node::CategoryID restrict_to,
							  uint64_t restrict_flags)
{
	Menu *menu = new Menu(parent);
	menu->setToolTipsVisible(true);

	for (int i = 0; i < library.size(); i++) {
		Node *n = library.at(i);

		if (restrict_to != Node::k_category_unknown &&
			!n->category().contains(restrict_to)) {
			// Skip this node
			continue;
		}

		if (restrict_flags && !(n->get_flags() & restrict_flags)) {
			continue;
		}

		if (n->get_flags() & Node::k_dont_show_in_create_menu) {
			continue;
		}

		// Make sure nodes are up-to-date with the current translation
		n->retranslate();

		QString category_name = Node::get_category_name(
			n->category().isEmpty() ? Node::k_category_unknown :
									  n->category().first());

		// Find or create top-level category menu
		Menu *top_menu = nullptr;
		QList<QAction *> menu_actions = menu->actions();
		foreach (QAction *action, menu_actions) {
			if (action->menu() && action->menu()->title() == category_name) {
				top_menu = static_cast<Menu *>(action->menu());
				break;
			}
		}
		if (!top_menu) {
			top_menu = new Menu(category_name, menu);
			menu->insert_alphabetically(top_menu);
		}

		// Determine final destination (support secondary grouping)
		Menu *destination = top_menu;
		QString sub = n->sub_category();
		if (!sub.isEmpty() && n->category().contains(Node::k_category_open_fx)) {
			QList<QAction *> sub_actions = top_menu->actions();
			foreach (QAction *action, sub_actions) {
				if (action->menu() && action->menu()->title() == sub) {
					destination = static_cast<Menu *>(action->menu());
					break;
				}
			}
			if (destination == top_menu) {
				destination = new Menu(sub, top_menu);
				top_menu->insert_alphabetically(destination);
			}
		}

		// Add entry to menu
		QAction *a = destination->insert_alphabetically(n->name());
		a->setData(i);
		a->setToolTip(n->description());
	}

	if (create_none_item) {
		QAction *none_item = new QAction(
			QCoreApplication::translate("NodeFactory", "None"), menu);

		none_item->setData(-1);

		if (menu->actions().isEmpty()) {
			menu->addAction(none_item);
		} else {
			QAction *separator = menu->insertSeparator(menu->actions().first());
			menu->insertAction(separator, none_item);
		}
	}

	return menu;
}

Node *NodeFactory::CreateFromMenuAction(QAction *action)
{
	int index = action->data().toInt();

	if (index == -1) {
		return nullptr;
	}

	return library.at(index)->copy();
}

QString NodeFactory::GetIDFromMenuAction(QAction *action)
{
	int index = action->data().toInt();

	if (index == -1) {
		return QString();
	}

	return library.at(action->data().toInt())->id();
}

QString NodeFactory::get_name_from_id(const QString &id)
{
	if (!id.isEmpty()) {
		foreach (Node *n, library) {
			if (n->id() == id) {
				return n->name();
			}
		}
	}

	return QString();
}

Node *NodeFactory::create_from_id(const QString &id)
{
	QString resolved_id = id;

	// Node IDs renamed after older project files were written
	static const QHash<QString, QString> k_legacy_i_ds = {
		{ QStringLiteral("org.oliveeditor.Olive.flip"),
		  QStringLiteral("org.olivevideoeditor.Olive.flip") },
		{ QStringLiteral("org.oliveeditor.Olive.ripple"),
		  QStringLiteral("org.olivevideoeditor.Olive.ripple") },
		{ QStringLiteral("org.oliveeditor.Olive.swirl"),
		  QStringLiteral("org.olivevideoeditor.Olive.swirl") },
		{ QStringLiteral("org.oliveeditor.Olive.tile"),
		  QStringLiteral("org.olivevideoeditor.Olive.tile") },
		{ QStringLiteral("org.oliveeditor.Olive.wave"),
		  QStringLiteral("org.olivevideoeditor.Olive.wave") },
	};
	resolved_id = k_legacy_i_ds.value(id, id);

	foreach (Node *n, library) {
		if (n->id() == resolved_id) {
			return n->copy();
		}
	}

	return nullptr;
}

void NodeFactory::register_plugin_nodes()
{
	QSet<QString> existing_ids;
	for (Node *node : library) {
		existing_ids.insert(node->id());
	}

	for (auto plugin : OFX::Host::PluginCache::getPluginCache()->getPlugins()) {
		auto *image_effect =
			dynamic_cast<OFX::Host::ImageEffect::ImageEffectPlugin *>(plugin);
		if (!image_effect) {
			continue;
		}

		const QString plugin_id =
			QString::fromStdString(image_effect->getIdentifier());
		if (existing_ids.contains(plugin_id)) {
			continue;
		}

		const auto &contexts = image_effect->getContexts();
		if (contexts.empty()) {
			qWarning() << "Skipping OFX plugin with no contexts:" << plugin_id;
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
		library.append(plugin_node);
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
