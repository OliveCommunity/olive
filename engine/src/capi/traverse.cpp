/***

  Oak - Non-Linear Video Editor
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

#include "oakengine/traverse.h"

#include <vector>

#include <QByteArray>
#include <QString>
#include <QTransform>
#include <QVector>

#include "node/traverser.h"
#include "node/value.h"
#include "render/videoparams.h"

// oak_node_value_type of a row value: same mapping as node.cpp's
// to_c_type(). Types without any facade representation report
// OAK_NODE_VALUE_NONE; duplicated here because node.cpp's copy is
// translation-unit local.
static int to_c_type(olive::NodeValue::Type t)
{
	switch (t) {
	case olive::NodeValue::k_int:
		return OAK_NODE_VALUE_INT;
	case olive::NodeValue::k_float:
		return OAK_NODE_VALUE_FLOAT;
	case olive::NodeValue::k_boolean:
		return OAK_NODE_VALUE_BOOL;
	case olive::NodeValue::k_rational:
		return OAK_NODE_VALUE_RATIONAL;
	case olive::NodeValue::k_color:
		return OAK_NODE_VALUE_COLOR;
	case olive::NodeValue::k_vec2:
		return OAK_NODE_VALUE_VEC2;
	case olive::NodeValue::k_vec3:
		return OAK_NODE_VALUE_VEC3;
	case olive::NodeValue::k_vec4:
		return OAK_NODE_VALUE_VEC4;
	case olive::NodeValue::k_combo:
		return OAK_NODE_VALUE_COMBO;
	case olive::NodeValue::k_file:
		return OAK_NODE_VALUE_STRING;
	case olive::NodeValue::k_text:
		return OAK_NODE_VALUE_TEXT;
	case olive::NodeValue::k_font:
		return OAK_NODE_VALUE_FONT;
	case olive::NodeValue::k_str_combo:
		return OAK_NODE_VALUE_STR_COMBO;
	case olive::NodeValue::k_binary:
		return OAK_NODE_VALUE_BINARY;
	case olive::NodeValue::k_bezier:
		return OAK_NODE_VALUE_BEZIER;
	case olive::NodeValue::k_texture:
		return OAK_NODE_VALUE_TEXTURE;
	case olive::NodeValue::k_samples:
		return OAK_NODE_VALUE_SAMPLES;
	case olive::NodeValue::k_video_params:
		return OAK_NODE_VALUE_VIDEO_PARAMS;
	case olive::NodeValue::k_audio_params:
		return OAK_NODE_VALUE_AUDIO_PARAMS;
	default:
		return OAK_NODE_VALUE_NONE;
	}
}

namespace
{

olive::Node *impl(OakEngineNode *h)
{
	return reinterpret_cast<olive::Node *>(h);
}

// oak_video_params POD -> olive::VideoParams (same mapping as
// encoding.cpp's to_cpp()).
olive::VideoParams to_cpp(const oak_video_params &v)
{
	olive::VideoParams vp(
		v.width, v.height, olive::Rational(v.time_base_num, v.time_base_den),
		static_cast<olive::PixelFormat::Format>(v.format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(v.pixel_aspect_num, v.pixel_aspect_den),
		static_cast<olive::VideoParams::Interlacing>(v.interlacing),
		v.divider > 0 ? v.divider : 1);
	vp.set_color_range(static_cast<olive::VideoParams::ColorRange>(v.color_range));
	return vp;
}

olive::TimeRange to_range(int64_t in_num, int64_t in_den, int64_t out_num,
						  int64_t out_den)
{
	return olive::TimeRange(olive::Rational(in_num, in_den),
							olive::Rational(out_num, out_den));
}

} // namespace

// Owned result object: per-input tables plus every string the accessors can
// return, pre-converted to UTF-8 so the returned pointers stay valid until
// oakengine_traverse_db_free().
struct OakEngineTraverseDb {
	struct Row {
		int type = OAK_NODE_VALUE_NONE;
		const olive::Node *source = nullptr;
		QByteArray tag;
		QByteArray value_string;
		std::vector<QByteArray> splits;
	};

	struct Input {
		QByteArray id;
		olive::NodeValueTable table; // kept for table_element_index_for_hint
		QVector<Row> rows;
	};

	QVector<Input> inputs;
};

namespace
{

OakEngineTraverseDb::Row convert_row(const olive::NodeValue &v)
{
	OakEngineTraverseDb::Row row;
	row.type = to_c_type(v.type());
	row.source = v.source();
	row.tag = v.tag().toUtf8();
	row.value_string = olive::NodeValue::value_to_string(v, false).toUtf8();
	const olive::SplitValue split = v.to_split_value();
	for (const QVariant &component : split) {
		row.splits.push_back(
			olive::NodeValue::value_to_string(v.type(), component, true)
				.toUtf8());
	}
	return row;
}

OakEngineTraverseDb::Input convert_input(const QString &id,
										 const olive::NodeValueTable &table)
{
	OakEngineTraverseDb::Input input;
	input.id = id.toUtf8();
	input.table = table;
	input.rows.reserve(table.count());
	for (int i = 0; i < table.count(); i++) {
		input.rows.append(convert_row(table.at(i)));
	}
	return input;
}

const OakEngineTraverseDb::Row *row_at(const OakEngineTraverseDb *db,
									   int input_index, int row)
{
	if (!db || input_index < 0 || input_index >= db->inputs.size() ||
		row < 0 || row >= db->inputs.at(input_index).rows.size()) {
		return nullptr;
	}
	return &db->inputs.at(input_index).rows.at(row);
}

} // namespace

extern "C"
{

OakEngineTraverseDb *oakengine_traverse_generate_database(
	OakEngineNode *node, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den)
{
	if (!node || in_den == 0 || out_den == 0) {
		return nullptr;
	}
	olive::Node *n = impl(node);
	olive::NodeTraverser traverser;
	const olive::NodeValueDatabase database =
		traverser.generate_database(n, to_range(in_num, in_den, out_num, out_den));

	auto *db = new OakEngineTraverseDb;
	// NodeValueDatabase is a QHash; emit entries in the node's input order so
	// the C-side index mapping is deterministic.
	for (const QString &id : n->inputs()) {
		auto it = database.cbegin();
		for (; it != database.cend(); ++it) {
			if (it.key() == id) {
				break;
			}
		}
		if (it != database.cend()) {
			db->inputs.append(convert_input(id, it.value()));
		}
	}
	return db;
}

OakEngineTraverseDb *oakengine_traverse_generate_table(
	OakEngineNode *node, int64_t in_num, int64_t in_den, int64_t out_num,
	int64_t out_den)
{
	if (!node || in_den == 0 || out_den == 0) {
		return nullptr;
	}
	olive::NodeTraverser traverser;
	const olive::NodeValueTable table = traverser.generate_table(
		impl(node), to_range(in_num, in_den, out_num, out_den));

	auto *db = new OakEngineTraverseDb;
	// A bare output table has no input id; represented as a single entry
	// with an empty id (see traverse.h).
	db->inputs.append(convert_input(QString(), table));
	return db;
}

void oakengine_traverse_db_free(OakEngineTraverseDb *db)
{
	delete db;
}

int oakengine_traverse_db_input_count(const OakEngineTraverseDb *db)
{
	return db ? int(db->inputs.size()) : 0;
}

const char *oakengine_traverse_db_input_id(const OakEngineTraverseDb *db,
										   int input_index)
{
	if (!db || input_index < 0 || input_index >= db->inputs.size()) {
		return nullptr;
	}
	return db->inputs.at(input_index).id.constData();
}

int oakengine_traverse_db_row_count(const OakEngineTraverseDb *db,
									int input_index)
{
	if (!db || input_index < 0 || input_index >= db->inputs.size()) {
		return 0;
	}
	return int(db->inputs.at(input_index).rows.size());
}

int oakengine_traverse_row_type(const OakEngineTraverseDb *db, int input_index,
								int row)
{
	const OakEngineTraverseDb::Row *r = row_at(db, input_index, row);
	return r ? r->type : OAK_NODE_VALUE_NONE;
}

OakEngineNode *oakengine_traverse_row_source(const OakEngineTraverseDb *db,
											 int input_index, int row)
{
	const OakEngineTraverseDb::Row *r = row_at(db, input_index, row);
	return (r && r->source) ?
			   reinterpret_cast<OakEngineNode *>(
				   const_cast<olive::Node *>(r->source)) :
			   nullptr;
}

const char *oakengine_traverse_row_tag(const OakEngineTraverseDb *db,
									   int input_index, int row)
{
	const OakEngineTraverseDb::Row *r = row_at(db, input_index, row);
	static const char empty[] = "";
	return r ? r->tag.constData() : empty;
}

const char *oakengine_traverse_row_value_string(const OakEngineTraverseDb *db,
												int input_index, int row)
{
	const OakEngineTraverseDb::Row *r = row_at(db, input_index, row);
	return r ? r->value_string.constData() : nullptr;
}

int oakengine_traverse_row_split_count(const OakEngineTraverseDb *db,
									   int input_index, int row)
{
	const OakEngineTraverseDb::Row *r = row_at(db, input_index, row);
	return r ? int(r->splits.size()) : 0;
}

const char *oakengine_traverse_row_split_string(const OakEngineTraverseDb *db,
												int input_index, int row,
												int split)
{
	const OakEngineTraverseDb::Row *r = row_at(db, input_index, row);
	if (!r || split < 0 || split >= int(r->splits.size())) {
		return nullptr;
	}
	return r->splits[size_t(split)].constData();
}

int oakengine_traverse_table_element_index_for_hint(
	OakEngineNode *hint_node, const char *input_id, int element,
	const OakEngineTraverseDb *table_db)
{
	if (!hint_node || !input_id || !table_db || table_db->inputs.size() != 1) {
		return -1;
	}
	olive::NodeTraverser traverser;
	return traverser.generate_row_value_element_index(
		impl(hint_node), QString::fromUtf8(input_id), element,
		&table_db->inputs.first().table);
}

int oakengine_traverse_generate_row(OakEngineNode *node, int64_t in_num,
									int64_t in_den, int64_t out_num,
									int64_t out_den,
									const oak_video_params *cache_video_params,
									int sample_rate, uint64_t channel_layout,
									void *row_out)
{
	if (!node || !row_out || in_den == 0 || out_den == 0) {
		return OAKENGINE_E_INVALID;
	}
	olive::NodeTraverser traverser;
	if (cache_video_params) {
		traverser.set_cache_video_params(to_cpp(*cache_video_params));
	}
	if (sample_rate > 0) {
		traverser.set_cache_audio_params(
			olive::AudioParams(sample_rate, channel_layout,
							   olive::core::SampleFormat::f32_p));
	}
	// Transition bridge: row_out is the application's own olive::NodeValueRow
	// (a QHash typedef), filled in place.
	auto *row = static_cast<olive::NodeValueRow *>(row_out);
	*row = traverser.generate_row(impl(node),
								  to_range(in_num, in_den, out_num, out_den));
	return OAKENGINE_OK;
}

int oakengine_traverse_transform(OakEngineNode *start, OakEngineNode *end,
								 int64_t in_num, int64_t in_den,
								 int64_t out_num, int64_t out_den,
								 const oak_video_params *cache_video_params,
								 double out_m[6])
{
	if (!start || !end || !out_m || in_den == 0 || out_den == 0) {
		return OAKENGINE_E_INVALID;
	}
	olive::NodeTraverser traverser;
	if (cache_video_params) {
		traverser.set_cache_video_params(to_cpp(*cache_video_params));
	}
	QTransform t;
	traverser.transform(&t, impl(start), impl(end),
						to_range(in_num, in_den, out_num, out_den));
	out_m[0] = t.m11();
	out_m[1] = t.m12();
	out_m[2] = t.m21();
	out_m[3] = t.m22();
	out_m[4] = t.dx();
	out_m[5] = t.dy();
	return OAKENGINE_OK;
}

} // extern "C"
