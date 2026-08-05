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

#include "mathbase.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "distort/transform/transformdistortnode.h"
#include "olive/core/util/stringutils.h"

namespace olive
{

ShaderCode MathNodeBase::get_shader_code_internal(const std::string &shader_id,
											   const std::string &param_a_in,
											   const std::string &param_b_in) const
{
	StringList code_id = core::StringUtils::split(shader_id, '.');

	Operation op =
		static_cast<Operation>(strtol(code_id.at(0).c_str(), nullptr, 10));
	Pairing pairing =
		static_cast<Pairing>(strtol(code_id.at(1).c_str(), nullptr, 10));
	NodeValue::Type type_a =
		static_cast<NodeValue::Type>(strtol(code_id.at(2).c_str(), nullptr, 10));
	NodeValue::Type type_b =
		static_cast<NodeValue::Type>(strtol(code_id.at(3).c_str(), nullptr, 10));

	std::string operation, frag, vert;

	if (pairing == k_pair_texture_matrix && op == k_op_multiply) {
		// Override the operation for this operation since we multiply texture COORDS by the matrix rather than
		const std::string &tex_in = (type_a == NodeValue::k_texture) ? param_a_in :
																  param_b_in;
		const std::string &mat_in = (type_a == NodeValue::k_texture) ? param_b_in :
																  param_a_in;

		// No-op frag shader (can we return std::string() instead?)
		operation = "texture(" + tex_in + ", ove_texcoord)";

		vert = "uniform mat4 " + mat_in +
			   ";\n"
			   "\n"
			   "in vec4 a_position;\n"
			   "in vec2 a_texcoord;\n"
			   "\n"
			   "out vec2 ove_texcoord;\n"
			   "\n"
			   "void main() {\n"
			   "    gl_Position = " +
			   mat_in +
			   " * a_position;\n"
			   "    ove_texcoord = a_texcoord;\n"
			   "}\n";

	} else {
		const std::string var_a = get_shader_variable_call(param_a_in, type_a);
		const std::string var_b = get_shader_variable_call(param_b_in, type_b);

		switch (op) {
		case k_op_add:
			operation = var_a + " + " + var_b;
			break;
		case k_op_subtract:
			operation = var_a + " - " + var_b;
			break;
		case k_op_multiply:
			operation = var_a + " * " + var_b;
			break;
		case k_op_divide:
			operation = var_a + " / " + var_b;
			break;
		case k_op_power:
			if (pairing == k_pair_texture_number) {
				// The "number" in this operation has to be declared a vec4
				if (NodeValue::type_is_numeric(type_a)) {
					operation = "pow(" + var_b + ", vec4(" + var_a + "))";
				} else {
					operation = "pow(" + var_a + ", vec4(" + var_b + "))";
				}
			} else {
				operation = "pow(" + var_a + ", " + var_b + ")";
			}
			break;
		}
	}

	frag = "uniform " + get_shader_uniform_type(type_a) + " " + param_a_in +
		   ";\n"
		   "uniform " +
		   get_shader_uniform_type(type_b) + " " + param_b_in +
		   ";\n"
		   "\n"
		   "in vec2 ove_texcoord;\n"
		   "out vec4 frag_color;\n"
		   "\n"
		   "void main(void) {\n"
		   "    vec4 c = " +
		   operation +
		   ";\n"
		   "    c.a = clamp(c.a, 0.0, 1.0);\n" // Ensure alpha is between 0.0 and 1.0
		   "    frag_color = c;\n"
		   "}\n";

	return ShaderCode(frag, vert);
}

std::string
MathNodeBase::get_shader_uniform_type(const olive::NodeValue::Type &type)
{
	switch (type) {
	case NodeValue::k_texture:
		return "sampler2D";
	case NodeValue::k_color:
		return "vec4";
	case NodeValue::k_matrix:
		return "mat4";
	default:
		return "float";
	}
}

std::string MathNodeBase::get_shader_variable_call(const std::string &input_id,
											const NodeValue::Type &type,
											const std::string &coord_op)
{
	if (type == NodeValue::k_texture) {
		return "texture(" + input_id + ", ove_texcoord" + coord_op + ")";
	}

	return input_id;
}

Vector4D MathNodeBase::retrieve_vector(const NodeValue &val)
{
	// Variant doesn't know that Vector*D can convert themselves so we do it here
	switch (val.type()) {
	case NodeValue::k_vec2: {
		Vector2D v = val.to_vec2();
		return Vector4D(v.x(), v.y(), 0.0f, 0.0f);
	}
	case NodeValue::k_vec3: {
		Vector3D v = val.to_vec3();
		return Vector4D(v.x(), v.y(), v.z(), 0.0f);
	}
	case NodeValue::k_vec4:
	default:
		return val.to_vec4();
	}
}

void MathNodeBase::push_vector(NodeValueTable *output,
							  olive::NodeValue::Type type, const Vector4D &vec) const
{
	switch (type) {
	case NodeValue::k_vec2:
		output->push(type, Vector2D(vec.x(), vec.y()), this);
		break;
	case NodeValue::k_vec3:
		output->push(type, Vector3D(vec.x(), vec.y(), vec.z()), this);
		break;
	case NodeValue::k_vec4:
		output->push(type, vec, this);
		break;
	default:
		break;
	}
}

std::string MathNodeBase::get_operation_name(Operation o)
{
	switch (o) {
	case k_op_add:
		return "Add";
	case k_op_subtract:
		return "Subtract";
	case k_op_multiply:
		return "Multiply";
	case k_op_divide:
		return "Divide";
	case k_op_power:
		return "Power";
	}

	return std::string();
}

void MathNodeBase::perform_all_on_float_buffer(Operation operation, float *a,
										   float b, int start, int end)
{
	for (int j = start; j < end; j++) {
		a[j] = perform_all(operation, a[j], b);
	}
}

#if defined(OLIVE_PROCESSOR_X86) || defined(OLIVE_PROCESSOR_ARM)
void MathNodeBase::perform_all_on_float_buffer_sse(Operation operation, float *a,
											  float b, int start, int end)
{
	int end_divisible_4 = (end / 4) * 4;

	// Load number to multiply by into buffer
	__m128 mult = _mm_load1_ps(&b);

	switch (operation) {
	case k_op_add:
		// Loop all values
		for (int j = 0; j < end_divisible_4; j += 4) {
			_mm_storeu_ps(a + start + j,
						  _mm_add_ps(_mm_loadu_ps(a + start + j), mult));
		}
		break;
	case k_op_subtract:
		for (int j = 0; j < end_divisible_4; j += 4) {
			_mm_storeu_ps(a + start + j,
						  _mm_sub_ps(_mm_loadu_ps(a + start + j), mult));
		}
		break;
	case k_op_multiply:
		for (int j = 0; j < end_divisible_4; j += 4) {
			_mm_storeu_ps(a + start + j,
						  _mm_mul_ps(_mm_loadu_ps(a + start + j), mult));
		}
		break;
	case k_op_divide:
		for (int j = 0; j < end_divisible_4; j += 4) {
			_mm_storeu_ps(a + start + j,
						  _mm_div_ps(_mm_loadu_ps(a + start + j), mult));
		}
		break;
	case k_op_power:
		// Fallback for operations we can't support here
		end_divisible_4 = 0;
		break;
	}

	// Handle last 1-3 bytes if necessary, or all bytes if we couldn't
	// support this op on SSE
	perform_all_on_float_buffer(operation, a, b, end_divisible_4, end);
}
#endif

void MathNodeBase::value_internal(
	Operation operation, Pairing pairing, const std::string &param_a_in,
	const NodeValue &val_a, const std::string &param_b_in, const NodeValue &val_b,
	const NodeGlobals &globals, NodeValueTable *output) const
{
	switch (pairing) {
	case k_pair_number_number: {
		if (val_a.type() == NodeValue::k_rational &&
			val_b.type() == NodeValue::k_rational && operation != k_op_power) {
			// Preserve rationals
			output->push(
				NodeValue::k_rational,
				perform_add_sub_mult_div<Rational, Rational>(
					operation, val_a.to_rational(), val_b.to_rational()),
				this);
		} else {
			output->push(NodeValue::k_float,
						 perform_all<float, float>(operation,
												  retrieve_number(val_a),
												  retrieve_number(val_b)),
						 this);
		}
		break;
	}

	case k_pair_vec_vec: {
		// We convert all vectors to Vector4D just for simplicity and exploit the fact that kVec4 is higher than kVec2 in
		// the enum to find the largest data type
		Vector4D vec_a = retrieve_vector(val_a);
		Vector4D vec_b = retrieve_vector(val_b);

		if (operation == k_op_divide) {
			// Lower-dimensional vectors are padded with zeros; dividing the
			// padding components would be 0/0 (assert in Qt debug builds, NaN
			// otherwise). Force those components to 0/1 so the result is a
			// well-defined zero, which is discarded by push_vector anyway.
			const NodeValue::Type max_type = std::max(val_a.type(), val_b.type());
			if (max_type == NodeValue::k_vec2) {
				vec_a.set_z(0.0f);
				vec_a.set_w(0.0f);
				vec_b.set_z(1.0f);
				vec_b.set_w(1.0f);
			} else if (max_type == NodeValue::k_vec3) {
				vec_a.set_w(0.0f);
				vec_b.set_w(1.0f);
			}
		}

		push_vector(output, std::max(val_a.type(), val_b.type()),
				   perform_add_sub_mult_div<Vector4D, Vector4D>(operation, vec_a,
															  vec_b));
		break;
	}

	case k_pair_matrix_vec: {
		Matrix4x4 matrix = (val_a.type() == NodeValue::k_matrix) ?
								val_a.to_matrix() :
								val_b.to_matrix();
		Vector4D vec = (val_a.type() == NodeValue::k_matrix) ?
							retrieve_vector(val_b) :
							retrieve_vector(val_a);

		// Only valid operation is multiply
		push_vector(output, std::max(val_a.type(), val_b.type()),
				   perform_mult<Vector4D, Matrix4x4>(operation, vec, matrix));
		break;
	}

	case k_pair_vec_number: {
		Vector4D vec = (NodeValue::type_is_vector(val_a.type()) ?
							retrieve_vector(val_a) :
							retrieve_vector(val_b));
		float number = retrieve_number(NodeValue::type_is_vector(val_a.type()) ?
										  val_b :
										  val_a);

		// Only multiply and divide are valid operations
		push_vector(output,
				   NodeValue::type_is_vector(val_a.type()) ? val_a.type() :
															 val_b.type(),
				   perform_mult_div<Vector4D, float>(operation, vec, number));
		break;
	}

	case k_pair_matrix_matrix: {
		Matrix4x4 mat_a = val_a.to_matrix();
		Matrix4x4 mat_b = val_b.to_matrix();
		output->push(NodeValue::k_matrix,
					 perform_add_sub_mult<Matrix4x4, Matrix4x4>(operation, mat_a,
															   mat_b),
					 this);
		break;
	}

	case k_pair_color_color: {
		Color col_a = val_a.to_color();
		Color col_b = val_b.to_color();

		// Only add and subtract are valid operations
		output->push(NodeValue::k_color,
					 perform_add_sub<Color, Color>(operation, col_a, col_b),
					 this);
		break;
	}

	case k_pair_number_color: {
		Color col = (val_a.type() == NodeValue::k_color) ? val_a.to_color() :
														  val_b.to_color();
		float num = (val_a.type() == NodeValue::k_color) ? val_b.to_double() :
														  val_a.to_double();

		// Only multiply and divide are valid operations
		output->push(NodeValue::k_color,
					 perform_mult<Color, float>(operation, col, num), this);
		break;
	}

	case k_pair_sample_sample: {
		SampleBuffer samples_a = val_a.to_samples();
		SampleBuffer samples_b = val_b.to_samples();

		size_t max_samples =
			std::max(samples_a.sample_count(), samples_b.sample_count());
		size_t min_samples =
			std::min(samples_a.sample_count(), samples_b.sample_count());

		SampleBuffer mixed_samples =
			SampleBuffer(samples_a.audio_params(), max_samples);

		for (int i = 0; i < mixed_samples.audio_params().channel_count(); i++) {
			// Mix samples that are in both buffers
			for (size_t j = 0; j < min_samples; j++) {
				mixed_samples.data(i)[j] = perform_all<float, float>(
					operation, samples_a.data(i)[j], samples_b.data(i)[j]);
			}
		}

		if (max_samples > min_samples) {
			// Fill in remainder space with 0s
			size_t remainder = max_samples - min_samples;

			const SampleBuffer &larger_buffer =
				(max_samples == samples_a.sample_count()) ? samples_a :
															samples_b;

			for (int i = 0; i < mixed_samples.audio_params().channel_count();
				 i++) {
				memcpy(&mixed_samples.data(i)[min_samples],
					   &larger_buffer.data(i)[min_samples],
					   remainder * sizeof(float));
			}
		}

		output->push(NodeValue::k_samples, mixed_samples, this);
		break;
	}

	case k_pair_texture_color:
	case k_pair_texture_number:
	case k_pair_texture_texture:
	case k_pair_texture_matrix: {
		ShaderJob job;
		job.set_shader_id(std::to_string(operation) + "." +
						  std::to_string(pairing) + "." +
						  std::to_string(val_a.type()) + "." +
						  std::to_string(val_b.type()));

		job.insert(param_a_in, val_a);
		job.insert(param_b_in, val_b);

		bool operation_is_noop = false;

		const NodeValue &number_val =
			val_a.type() == NodeValue::k_texture ? val_b : val_a;
		const NodeValue &texture_val =
			val_a.type() == NodeValue::k_texture ? val_a : val_b;
		TexturePtr texture = texture_val.to_texture();

		if (!texture) {
			operation_is_noop = true;
		} else if (pairing == k_pair_texture_number) {
			if (number_is_no_op(operation, retrieve_number(number_val))) {
				operation_is_noop = true;
			}
		} else if (pairing == k_pair_texture_matrix) {
			// Only allow matrix multiplication
			const Vector2D &sequence_res = globals.nonsquare_resolution();
			Vector2D texture_res(texture->params().width() *
									  texture->pixel_aspect_ratio().to_double(),
								  texture->params().height());

			Matrix4x4 adjusted_matrix =
				TransformDistortNode::adjust_matrix_by_resolutions(
					number_val.to_matrix(), sequence_res,
					Vector2D(texture->params().x(), texture->params().y()), texture_res);

			if (operation != k_op_multiply || adjusted_matrix.is_identity()) {
				operation_is_noop = true;
			} else {
				// Replace with adjusted matrix
				job.insert(val_a.type() == NodeValue::k_texture ? param_b_in :
																 param_a_in,
						   NodeValue(NodeValue::k_matrix, adjusted_matrix,
									 this));
			}
		}

		if (operation_is_noop) {
			// Just push texture as-is
			output->push(texture_val);
		} else {
			// Push shader job
			output->push(NodeValue::k_texture,
						 Texture::job(globals.vparams(), job), this);
		}
		break;
	}

	case k_pair_sample_number: {
		// Queue a sample job
		const NodeValue &number_val =
			val_a.type() == NodeValue::k_samples ? val_b : val_a;
		const std::string &number_param =
			val_a.type() == NodeValue::k_samples ? param_b_in : param_a_in;

		float number = retrieve_number(number_val);

		SampleBuffer buffer = val_a.type() == NodeValue::k_samples ?
								  val_a.to_samples() :
								  val_b.to_samples();

		if (buffer.is_allocated()) {
			if (is_input_static(number_param)) {
				if (!number_is_no_op(operation, number)) {
					for (int i = 0; i < buffer.audio_params().channel_count();
						 i++) {
#if defined(OLIVE_PROCESSOR_X86) || defined(OLIVE_PROCESSOR_ARM)
						// Use SSE instructions for optimization
						perform_all_on_float_buffer_sse(operation, buffer.data(i),
												   number, 0,
												   buffer.sample_count());
#else
						PerformAllOnFloatBuffer(operation, buffer.data(i),
												number, 0,
												buffer.sample_count());
#endif
					}
				}

				output->push(NodeValue::k_samples, buffer, this);
			} else {
				SampleJob job(globals.time(),
							  val_a.type() == NodeValue::k_samples ? val_a :
																	val_b);
				job.insert(number_param,
						   NodeValue(NodeValue::k_float, number, this));
				output->push(NodeValue::k_samples, job, this);
			}
		}
		break;
	}

	case k_pair_none:
	case k_pair_count:
		break;
	}
}

void MathNodeBase::process_samples_internal(const NodeValueRow &values,
										  MathNodeBase::Operation operation,
										  const std::string &param_a_in,
										  const std::string &param_b_in,
										  const olive::SampleBuffer &input,
										  olive::SampleBuffer &output,
										  int index) const
{
	// This function is only used for sample+number pairing
	NodeValue number_val = values.at(param_a_in);

	if (number_val.type() == NodeValue::k_none) {
		number_val = values.at(param_b_in);

		if (number_val.type() == NodeValue::k_none) {
			return;
		}
	}

	float number_flt = retrieve_number(number_val);

	for (int i = 0; i < output.audio_params().channel_count(); i++) {
		output.data(i)[index] = perform_all<float, float>(
			operation, input.data(i)[index], number_flt);
	}
}

float MathNodeBase::retrieve_number(const NodeValue &val)
{
	if (val.type() == NodeValue::k_rational) {
		return val.to_rational().to_double();
	} else {
		return val.to_double();
	}
}

bool MathNodeBase::number_is_no_op(const MathNodeBase::Operation &op,
								const float &number)
{
	switch (op) {
	case k_op_add:
	case k_op_subtract:
		if (number == 0.0f) {
			return true;
		}
		break;
	case k_op_multiply:
	case k_op_divide:
	case k_op_power:
		// Same semantics as qFuzzyCompare(number, 1.0f)
		if (std::abs(number - 1.0f) * 100000.0f <=
			std::min(std::abs(number), 1.0f)) {
			return true;
		}
		break;
	}

	return false;
}

MathNodeBase::PairingCalculator::PairingCalculator(
	const NodeValueTable &table_a, const NodeValueTable &table_b)
{
	std::vector<int> pair_likelihood_a = get_pair_likelihood(table_a);
	std::vector<int> pair_likelihood_b = get_pair_likelihood(table_b);

	int weight_a = std::max(0, table_b.count() - table_a.count());
	int weight_b = std::max(0, table_a.count() - table_b.count());

	std::vector<int> likelihoods(k_pair_count);

	for (int i = 0; i < k_pair_count; i++) {
		if (pair_likelihood_a.at(i) == -1 || pair_likelihood_b.at(i) == -1) {
			likelihoods[i] = -1;
		} else {
			likelihoods[i] = pair_likelihood_a.at(i) + weight_a +
							 pair_likelihood_b.at(i) + weight_b;
		}
	}

	most_likely_pairing_ = k_pair_none;

	for (int i = 0; i < int(likelihoods.size()); i++) {
		if (likelihoods.at(i) > -1) {
			if (most_likely_pairing_ == k_pair_none ||
				likelihoods.at(i) > likelihoods.at(most_likely_pairing_)) {
				most_likely_pairing_ = static_cast<Pairing>(i);
			}
		}
	}

	if (most_likely_pairing_ != k_pair_none) {
		most_likely_value_a_ =
			table_a.at(pair_likelihood_a.at(most_likely_pairing_));
		most_likely_value_b_ =
			table_b.at(pair_likelihood_b.at(most_likely_pairing_));
	}
}

std::vector<int>
MathNodeBase::PairingCalculator::get_pair_likelihood(const NodeValueTable &table)
{
	std::vector<int> likelihood(k_pair_count, -1);

	for (int i = 0; i < table.count(); i++) {
		NodeValue::Type type = table.at(i).type();

		int weight = i;

		if (NodeValue::type_is_vector(type)) {
			likelihood[k_pair_vec_vec] = weight;
			likelihood[k_pair_vec_number] = weight;
			likelihood[k_pair_matrix_vec] = weight;
		} else if (type == NodeValue::k_matrix) {
			likelihood[k_pair_matrix_matrix] = weight;
			likelihood[k_pair_matrix_vec] = weight;
			likelihood[k_pair_texture_matrix] = weight;
		} else if (type == NodeValue::k_color) {
			likelihood[k_pair_color_color] = weight;
			likelihood[k_pair_number_color] = weight;
			likelihood[k_pair_texture_color] = weight;
		} else if (NodeValue::type_is_numeric(type)) {
			likelihood[k_pair_number_number] = weight;
			likelihood[k_pair_vec_number] = weight;
			likelihood[k_pair_number_color] = weight;
			likelihood[k_pair_texture_number] = weight;
			likelihood[k_pair_sample_number] = weight;
		} else if (type == NodeValue::k_samples) {
			likelihood[k_pair_sample_sample] = weight;
			likelihood[k_pair_sample_number] = weight;
		} else if (type == NodeValue::k_texture) {
			likelihood[k_pair_texture_texture] = weight;
			likelihood[k_pair_texture_number] = weight;
			likelihood[k_pair_texture_color] = weight;
			likelihood[k_pair_texture_matrix] = weight;
		}
	}

	return likelihood;
}

bool MathNodeBase::PairingCalculator::found_most_likely_pairing() const
{
	return (most_likely_pairing_ > k_pair_none &&
			most_likely_pairing_ < k_pair_count);
}

MathNodeBase::Pairing
MathNodeBase::PairingCalculator::get_most_likely_pairing() const
{
	return most_likely_pairing_;
}

const NodeValue &MathNodeBase::PairingCalculator::get_most_likely_value_a() const
{
	return most_likely_value_a_;
}

const NodeValue &MathNodeBase::PairingCalculator::get_most_likely_value_b() const
{
	return most_likely_value_b_;
}

template <typename T, typename U>
T MathNodeBase::perform_all(Operation operation, T a, U b)
{
	switch (operation) {
	case k_op_add:
		return a + b;
	case k_op_subtract:
		return a - b;
	case k_op_multiply:
		return a * b;
	case k_op_divide:
		return a / b;
	case k_op_power:
		return std::pow(a, b);
	}

	return a;
}

template <typename T, typename U>
T MathNodeBase::perform_mult_div(Operation operation, T a, U b)
{
	switch (operation) {
	case k_op_multiply:
		return a * b;
	case k_op_divide:
		return a / b;
	case k_op_add:
	case k_op_subtract:
	case k_op_power:
		break;
	}

	return a;
}

template <typename T, typename U>
T MathNodeBase::perform_add_sub(Operation operation, T a, U b)
{
	switch (operation) {
	case k_op_add:
		return a + b;
	case k_op_subtract:
		return a - b;
	case k_op_multiply:
	case k_op_divide:
	case k_op_power:
		break;
	}

	return a;
}

template <typename T, typename U>
T MathNodeBase::perform_mult(Operation operation, T a, U b)
{
	switch (operation) {
	case k_op_multiply:
		return a * b;
	case k_op_add:
	case k_op_subtract:
	case k_op_divide:
	case k_op_power:
		break;
	}

	return a;
}

template <typename T, typename U>
T MathNodeBase::perform_add_sub_mult(Operation operation, T a, U b)
{
	switch (operation) {
	case k_op_add:
		return a + b;
	case k_op_subtract:
		return a - b;
	case k_op_multiply:
		return a * b;
	case k_op_divide:
	case k_op_power:
		break;
	}

	return a;
}

template <typename T, typename U>
T MathNodeBase::perform_add_sub_mult_div(Operation operation, T a, U b)
{
	switch (operation) {
	case k_op_add:
		return a + b;
	case k_op_subtract:
		return a - b;
	case k_op_multiply:
		return a * b;
	case k_op_divide:
		return a / b;
	case k_op_power:
		break;
	}

	return a;
}

}
