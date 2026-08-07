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
#include "common/colortransform.h"
#include "node/colormanager.h"
#include "render/color.h"

#include "color/colormanager/colormanager.h"
#include "render/colorprocessor.h"

namespace olive
{

#define super OCIOBaseNode

const std::string ChromaKeyNode::k_color_input = "color_key";
const std::string ChromaKeyNode::k_mask_only_input = "mask_only_in";
const std::string ChromaKeyNode::k_invert_input = "invert_in";
const std::string ChromaKeyNode::k_upper_tolerance_input = "upper_tolerance_in";
const std::string ChromaKeyNode::k_lower_tolerance_input = "lower_tolerance_in";
const std::string ChromaKeyNode::k_garbage_matte_input = "garbage_in";
const std::string ChromaKeyNode::k_core_matte_input = "core_in";
const std::string ChromaKeyNode::k_shadows_input = "shadows_in";
const std::string ChromaKeyNode::k_highlights_input = "highlights_in";

ChromaKeyNode::ChromaKeyNode()
{
	add_input(k_color_input, NodeValue::k_color,
			 Variant::from_value(Color(0.0f, 1.0f, 0.0f, 1.0f)));

	add_input(k_lower_tolerance_input, NodeValue::k_float, 5.0);
	set_input_property(k_lower_tolerance_input, "min", 0.0);
	set_input_property(k_lower_tolerance_input, "base", 0.1);

	add_input(k_upper_tolerance_input, NodeValue::k_float, 25.0);
	set_input_property(k_upper_tolerance_input, "base", 0.1);

	// FIXME: Temporarily disabled. This will break if "lower tolerance" is keyframed or connected to
	//        something and there's currently no solution to remedy that. If there is in the future,
	//        we can look into re-enabling this.
	//SetInputProperty(kUpperToleranceInput, QStringLiteral("min"), GetStandardValue(kLowerToleranceInput).toDouble());

	add_input(k_garbage_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_core_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_highlights_input, NodeValue::k_float, 100.0f);
	set_input_property(k_highlights_input, "min", 0.0);
	set_input_property(k_highlights_input, "base", 0.1);

	add_input(k_shadows_input, NodeValue::k_float, 100.0f);
	set_input_property(k_shadows_input, "min", 0.0);
	set_input_property(k_shadows_input, "base", 0.1);

	add_input(k_invert_input, NodeValue::k_boolean, false);

	add_input(k_mask_only_input, NodeValue::k_boolean, false);
}

std::string ChromaKeyNode::name() const
{
	return "Chroma Key";
}

std::string ChromaKeyNode::id() const
{
	return "org.olivevideoeditor.Olive.chromakey";
}

std::vector<Node::CategoryID> ChromaKeyNode::category() const
{
	return { k_category_keying };
}

std::string ChromaKeyNode::description() const
{
	return "A simple color key based on the distance from the chroma of a selected color.";
}

void ChromaKeyNode::retranslate()
{
	super::retranslate();
	set_input_name(k_texture_input, "Input");
	set_input_name(k_garbage_matte_input, "Garbage Matte");
	set_input_name(k_core_matte_input, "Core Matte");
	set_input_name(k_color_input, "Key Color");
	set_input_name(k_shadows_input, "Shadows");
	set_input_name(k_highlights_input, "Highlights");
	set_input_name(k_upper_tolerance_input, "Upper Tolerance");
	set_input_name(k_lower_tolerance_input, "Lower Tolerance");
	set_input_name(k_invert_input, "Invert Mask");
	set_input_name(k_mask_only_input, "Show Mask Only");
}

void ChromaKeyNode::InputValueChangedEvent(const std::string &input, int element)
{
	(void) element;
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
	std::string frag =
		FileFunctions::read_file_as_string(":/shaders/chromakey.frag");
	// QString::arg() equivalent: replace every "%1" marker with the stub
	std::string::size_type pos = 0;
	while ((pos = frag.find("%1", pos)) != std::string::npos) {
		frag.replace(pos, 2, request.stub);
		pos += request.stub.size();
	}
	return ShaderCode(frag);
}

void ChromaKeyNode::generate_processor()
{
	if (manager()) {
		OakNodeColorManager mgr =
			oaknode_colormanager_wrap_borrowed(manager());
		OakColorTransform transform =
			oakcommon_colortransform_init_output("cie_xyz_d65_interchange");

		char ref_space[256];
		int needed = oaknode_colormanager_get_reference_color_space(
			mgr, ref_space, sizeof(ref_space));
		OakColorProcessor processor = {};
		if (needed > 0 && needed <= int(sizeof(ref_space))) {
			processor = oakrender_color_processor_create_transform(
				mgr, ref_space, transform, OAKRENDER_COLOR_DIRECTION_NORMAL);
		}

		oakcommon_colortransform_free(&transform);
		mgr.release(mgr.ctx);

		if (processor.ctx) {
			set_processor(processor);
		}
	}
}

void ChromaKeyNode::value(const NodeValueRow &value, const NodeGlobals &globals,
						  NodeValueTable *table) const
{
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		if (processor().ctx) {
			ColorTransformJob job(value);

			job.set_color_processor(
				oakrender_color_processor_get_native(processor()));
			job.set_input_texture(value.at(k_texture_input));
			job.set_needs_custom_shader(this);
			job.set_function_name("SceneLinearToCIEXYZ_d65");

			table->push(NodeValue::k_texture, tex->to_job(job), this);
		}
	}
}

void ChromaKeyNode::config_changed()
{
	generate_processor();
}

std::string ChromaKeyNode::get_input_id_for_legacy_id(const std::string &id) const
{
	// Older project files used the misspelled "tolerence" input IDs
	if (id == "upper_tolerence_in") {
		return k_upper_tolerance_input;
	}
	if (id == "lower_tolerence_in") {
		return k_lower_tolerance_input;
	}

	return super::get_input_id_for_legacy_id(id);
}

} // namespace olive
