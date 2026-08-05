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

#ifndef OAK_MATHNODEBASE_H
#define OAK_MATHNODEBASE_H

#include "node.h"
#include "olive/core/util/cpuoptimize.h"

namespace olive
{

class MathNodeBase : public Node {
public:
	MathNodeBase() = default;

	enum Operation { k_op_add, k_op_subtract, k_op_multiply, k_op_divide, k_op_power };

	static std::string get_operation_name(Operation o);

protected:
	enum Pairing {
		k_pair_none = -1,

		k_pair_number_number,
		k_pair_vec_vec,
		k_pair_matrix_matrix,
		k_pair_color_color,
		k_pair_texture_texture,

		k_pair_vec_number,
		k_pair_matrix_vec,
		k_pair_number_color,
		k_pair_texture_number,
		k_pair_texture_color,
		k_pair_texture_matrix,
		k_pair_sample_sample,
		k_pair_sample_number,

		k_pair_count
	};

	class PairingCalculator {
	public:
		PairingCalculator(const NodeValueTable &table_a,
						  const NodeValueTable &table_b);

		bool found_most_likely_pairing() const;
		Pairing get_most_likely_pairing() const;

		const NodeValue &get_most_likely_value_a() const;
		const NodeValue &get_most_likely_value_b() const;

	private:
		static std::vector<int> get_pair_likelihood(const NodeValueTable &table);

		Pairing most_likely_pairing_;

		NodeValue most_likely_value_a_;

		NodeValue most_likely_value_b_;
	};

	template <typename T, typename U>
	static T perform_all(Operation operation, T a, U b);

	template <typename T, typename U>
	static T perform_mult_div(Operation operation, T a, U b);

	template <typename T, typename U>
	static T perform_add_sub(Operation operation, T a, U b);

	template <typename T, typename U>
	static T perform_mult(Operation operation, T a, U b);

	template <typename T, typename U>
	static T perform_add_sub_mult(Operation operation, T a, U b);

	template <typename T, typename U>
	static T perform_add_sub_mult_div(Operation operation, T a, U b);

	static void perform_all_on_float_buffer(Operation operation, float *a, float b,
										int start, int end);

#if defined(OLIVE_PROCESSOR_X86) || defined(OLIVE_PROCESSOR_ARM)
	static void perform_all_on_float_buffer_sse(Operation operation, float *a,
										   float b, int start, int end);
#endif

	static std::string get_shader_uniform_type(const NodeValue::Type &type);

	static std::string get_shader_variable_call(const std::string &input_id,
										 const NodeValue::Type &type,
										 const std::string &coord_op = std::string());

	static Vector4D retrieve_vector(const NodeValue &val);

	static float retrieve_number(const NodeValue &val);

	static bool number_is_no_op(const Operation &op, const float &number);

	ShaderCode get_shader_code_internal(const std::string &shader_id,
									 const std::string &param_a_in,
									 const std::string &param_b_in) const;

	void push_vector(NodeValueTable *output, NodeValue::Type type,
					const Vector4D &vec) const;

	void value_internal(Operation operation, Pairing pairing,
					   const std::string &param_a_in, const NodeValue &val_a,
					   const std::string &param_b_in, const NodeValue &val_b,
					   const NodeGlobals &globals,
					   NodeValueTable *output) const;

	void process_samples_internal(const NodeValueRow &values, Operation operation,
								const std::string &param_a_in,
								const std::string &param_b_in,
								const SampleBuffer &input, SampleBuffer &output,
								int index) const;
};

}

#endif // OAK_MATHNODEBASE_H
