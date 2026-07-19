/***
  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team
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

#include "chromakey.h"

#include "node/color/colormanager/colormanager.h"
#include "render/colorprocessor.h"

namespace olive
{

#define super OCIOBaseNode

const QString ChromaKeyNode::k_color_input = QStringLiteral("color_key");
const QString ChromaKeyNode::k_mask_only_input = QStringLiteral("mask_only_in");
const QString ChromaKeyNode::k_invert_input = QStringLiteral("invert_in");
const QString ChromaKeyNode::k_upper_tolerance_input =
	QStringLiteral("upper_tolerance_in");
const QString ChromaKeyNode::k_lower_tolerance_input =
	QStringLiteral("lower_tolerance_in");
const QString ChromaKeyNode::k_garbage_matte_input = QStringLiteral("garbage_in");
const QString ChromaKeyNode::k_core_matte_input = QStringLiteral("core_in");
const QString ChromaKeyNode::k_shadows_input = QStringLiteral("shadows_in");
const QString ChromaKeyNode::k_highlights_input = QStringLiteral("highlights_in");

ChromaKeyNode::ChromaKeyNode()
{
	add_input(k_color_input, NodeValue::k_color,
			 QVariant::fromValue(Color(0.0f, 1.0f, 0.0f, 1.0f)));

	add_input(k_lower_tolerance_input, NodeValue::k_float, 5.0);
	set_input_property(k_lower_tolerance_input, QStringLiteral("min"), 0.0);
	set_input_property(k_lower_tolerance_input, QStringLiteral("base"), 0.1);

	add_input(k_upper_tolerance_input, NodeValue::k_float, 25.0);
	set_input_property(k_upper_tolerance_input, QStringLiteral("base"), 0.1);

	// FIXME: Temporarily disabled. This will break if "lower tolerance" is keyframed or connected to
	//        something and there's currently no solution to remedy that. If there is in the future,
	//        we can look into re-enabling this.
	//SetInputProperty(kUpperToleranceInput, QStringLiteral("min"), GetStandardValue(kLowerToleranceInput).toDouble());

	add_input(k_garbage_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_core_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_highlights_input, NodeValue::k_float, 100.0f);
	set_input_property(k_highlights_input, QStringLiteral("min"), 0.0);
	set_input_property(k_highlights_input, QStringLiteral("base"), 0.1);

	add_input(k_shadows_input, NodeValue::k_float, 100.0f);
	set_input_property(k_shadows_input, QStringLiteral("min"), 0.0);
	set_input_property(k_shadows_input, QStringLiteral("base"), 0.1);

	add_input(k_invert_input, NodeValue::k_boolean, false);

	add_input(k_mask_only_input, NodeValue::k_boolean, false);
}

QString ChromaKeyNode::name() const
{
	return tr("Chroma Key");
}

QString ChromaKeyNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.chromakey");
}

QVector<Node::CategoryID> ChromaKeyNode::category() const
{
	return { k_category_keying };
}

QString ChromaKeyNode::description() const
{
	return tr(
		"A simple color key based on the distance from the chroma of a selected color.");
}

void ChromaKeyNode::retranslate()
{
	super::retranslate();
	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_garbage_matte_input, tr("Garbage Matte"));
	set_input_name(k_core_matte_input, tr("Core Matte"));
	set_input_name(k_color_input, tr("Key Color"));
	set_input_name(k_shadows_input, tr("Shadows"));
	set_input_name(k_highlights_input, tr("Highlights"));
	set_input_name(k_upper_tolerance_input, tr("Upper Tolerance"));
	set_input_name(k_lower_tolerance_input, tr("Lower Tolerance"));
	set_input_name(k_invert_input, tr("Invert Mask"));
	set_input_name(k_mask_only_input, tr("Show Mask Only"));
}

void ChromaKeyNode::InputValueChangedEvent(const QString &input, int element)
{
	Q_UNUSED(element);
	if (input == k_lower_tolerance_input) {
		// FIXME: Temporarily disabled. This will break if "lower tolerance" is keyframed or connected to
		//        something and there's currently no solution to remedy that. If there is in the future,
		//        we can look into re-enabling this.
		//SetInputProperty(kUpperToleranceInput, QStringLiteral("min"), GetStandardValue(kLowerToleranceInput).toDouble());
	}

	generate_processor();
}

ShaderCode ChromaKeyNode::get_shader_code(const ShaderRequest &request) const
{
	return ShaderCode(FileFunctions::read_file_as_string(
						  QStringLiteral(":/shaders/chromakey.frag"))
						  .arg(request.stub));
}

void ChromaKeyNode::generate_processor()
{
	if (manager()) {
		try {
			ColorTransform transform("cie_xyz_d65_interchange");
			set_processor(ColorProcessor::create(
				manager(), manager()->get_reference_color_space(), transform));
		} catch (const ocio::Exception &e) {
			std::cerr << std::endl << e.what() << std::endl;
		}
	}
}

void ChromaKeyNode::value(const NodeValueRow &value, const NodeGlobals &globals,
						  NodeValueTable *table) const
{
	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		if (processor()) {
			ColorTransformJob job(value);

			job.set_color_processor(processor());
			job.set_input_texture(value[k_texture_input]);
			job.set_needs_custom_shader(this);
			job.set_function_name(QStringLiteral("SceneLinearToCIEXYZ_d65"));

			table->push(NodeValue::k_texture, tex->to_job(job), this);
		}
	}
}

void ChromaKeyNode::config_changed()
{
	generate_processor();
}

QString ChromaKeyNode::get_input_id_for_legacy_id(const QString &id) const
{
	// Older project files used the misspelled "tolerence" input IDs
	if (id == QStringLiteral("upper_tolerence_in")) {
		return k_upper_tolerance_input;
	}
	if (id == QStringLiteral("lower_tolerence_in")) {
		return k_lower_tolerance_input;
	}

	return super::get_input_id_for_legacy_id(id);
}

} // namespace olive
