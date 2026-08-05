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

#include "math.h"

namespace olive
{

const std::string MathNode::k_method_in = "method_in";
const std::string MathNode::k_param_a_in = "param_a_in";
const std::string MathNode::k_param_b_in = "param_b_in";
const std::string MathNode::k_param_c_in = "param_c_in";

#define super MathNodeBase

MathNode::MathNode()
{
	add_input(k_method_in, NodeValue::k_combo,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_param_a_in, NodeValue::k_float, 0.0);
	set_input_property(k_param_a_in, "decimalplaces", 8);
	set_input_property(k_param_a_in, "autotrim", true);

	add_input(k_param_b_in, NodeValue::k_float, 0.0);
	set_input_property(k_param_b_in, "decimalplaces", 8);
	set_input_property(k_param_b_in, "autotrim", true);
}

std::string MathNode::name() const
{
	// Default to naming after the operation
	if (parent()) {
		std::string op_name = get_operation_name(get_operation());
		if (!op_name.empty()) {
			return op_name;
		}
	}

	return "Math";
}

std::string MathNode::id() const
{
	return "org.olivevideoeditor.Olive.math";
}

std::vector<Node::CategoryID> MathNode::category() const
{
	return { k_category_math };
}

std::string MathNode::description() const
{
	return "Perform a mathematical operation between two values.";
}

void MathNode::retranslate()
{
	super::retranslate();

	set_input_name(k_method_in, "Method");
	set_input_name(k_param_a_in, "Value");
	set_input_name(k_param_b_in, "Value");

	StringList operations = { get_operation_name(k_op_add),
							  get_operation_name(k_op_subtract),
							  get_operation_name(k_op_multiply),
							  get_operation_name(k_op_divide),
							  get_operation_name(k_op_power) };

	set_combo_box_strings(k_method_in, operations);
}

ShaderCode MathNode::get_shader_code(const ShaderRequest &request) const
{
	return get_shader_code_internal(request.id, k_param_a_in, k_param_b_in);
}

void MathNode::value(const NodeValueRow &value, const NodeGlobals &globals,
					 NodeValueTable *table) const
{
	// Auto-detect what values to operate with
	// FIXME: Very inefficient
	NodeValueTable at, bt;
	at.push(value.at(k_param_a_in));
	bt.push(value.at(k_param_b_in));
	PairingCalculator calc(at, bt);

	// Do nothing if no pairing was found
	if (!calc.found_most_likely_pairing()) {
		return;
	}

	return value_internal(get_operation(), calc.get_most_likely_pairing(), k_param_a_in,
						 calc.get_most_likely_value_a(), k_param_b_in,
						 calc.get_most_likely_value_b(), globals, table);
}

void MathNode::process_samples(const NodeValueRow &values,
							  const SampleBuffer &input, SampleBuffer &output,
							  int index) const
{
	return process_samples_internal(values, get_operation(), k_param_a_in, k_param_b_in,
								  input, output, index);
}

}
