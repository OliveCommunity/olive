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

#include <QMatrix4x4>
#include <QVector2D>

#include "common/tohex.h"
#include "node/distort/transform/transformdistortnode.h"

namespace olive
{

ShaderCode MathNodeBase::get_shader_code_internal(const QString &shader_id,
											   const QString &param_a_in,
											   const QString &param_b_in) const
{
	QStringList code_id = shader_id.split('.');

	Operation op = static_cast<Operation>(code_id.at(0).toInt());
	Pairing pairing = static_cast<Pairing>(code_id.at(1).toInt());
	NodeValue::Type type_a =
		static_cast<NodeValue::Type>(code_id.at(2).toInt());
	NodeValue::Type type_b =
		static_cast<NodeValue::Type>(code_id.at(3).toInt());

	QString operation, frag, vert;

	if (pairing == k_pair_texture_matrix && op == k_op_multiply) {
		// Override the operation for this operation since we multiply texture COORDS by the matrix rather than
		const QString &tex_in = (type_a == NodeValue::k_texture) ? param_a_in :
																  param_b_in;
		const QString &mat_in = (type_a == NodeValue::k_texture) ? param_b_in :
																  param_a_in;

		// No-op frag shader (can we return QString() instead?)
		operation = QStringLiteral("texture(%1, ove_texcoord)").arg(tex_in);

		vert = QStringLiteral("uniform mat4 %1;\n"
							  "\n"
							  "in vec4 a_position;\n"
							  "in vec2 a_texcoord;\n"
							  "\n"
							  "out vec2 ove_texcoord;\n"
							  "\n"
							  "void main() {\n"
							  "    gl_Position = %1 * a_position;\n"
							  "    ove_texcoord = a_texcoord;\n"
							  "}\n")
				   .arg(mat_in);

	} else {
		switch (op) {
		case k_op_add:
			operation = QStringLiteral("%1 + %2");
			break;
		case k_op_subtract:
			operation = QStringLiteral("%1 - %2");
			break;
		case k_op_multiply:
			operation = QStringLiteral("%1 * %2");
			break;
		case k_op_divide:
			operation = QStringLiteral("%1 / %2");
			break;
		case k_op_power:
			if (pairing == k_pair_texture_number) {
				// The "number" in this operation has to be declared a vec4
				if (NodeValue::type_is_numeric(type_a)) {
					operation = QStringLiteral("pow(%2, vec4(%1))");
				} else {
					operation = QStringLiteral("pow(%1, vec4(%2))");
				}
			} else {
				operation = QStringLiteral("pow(%1, %2)");
			}
			break;
		}

		operation = operation.arg(get_shader_variable_call(param_a_in, type_a),
								  get_shader_variable_call(param_b_in, type_b));
	}

	frag =
		QStringLiteral(
			"uniform %1 %3;\n"
			"uniform %2 %4;\n"
			"\n"
			"in vec2 ove_texcoord;\n"
			"out vec4 frag_color;\n"
			"\n"
			"void main(void) {\n"
			"    vec4 c = %5;\n"
			"    c.a = clamp(c.a, 0.0, 1.0);\n" // Ensure alpha is between 0.0 and 1.0
			"    frag_color = c;\n"
			"}\n")
			.arg(get_shader_uniform_type(type_a), get_shader_uniform_type(type_b),
				 param_a_in, param_b_in, operation);

	return ShaderCode(frag, vert);
}

QString MathNodeBase::get_shader_uniform_type(const olive::NodeValue::Type &type)
{
	switch (type) {
	case NodeValue::k_texture:
		return QStringLiteral("sampler2D");
	case NodeValue::k_color:
		return QStringLiteral("vec4");
	case NodeValue::k_matrix:
		return QStringLiteral("mat4");
	default:
		return QStringLiteral("float");
	}
}

QString MathNodeBase::get_shader_variable_call(const QString &input_id,
											const NodeValue::Type &type,
											const QString &coord_op)
{
	if (type == NodeValue::k_texture) {
		return QStringLiteral("texture(%1, ove_texcoord%2)")
			.arg(input_id, coord_op);
	}

	return input_id;
}

QVector4D MathNodeBase::retrieve_vector(const NodeValue &val)
{
	// QVariant doesn't know that QVector*D can convert themselves so we do it here
	switch (val.type()) {
	case NodeValue::k_vec2:
		return QVector4D(val.to_vec2());
	case NodeValue::k_vec3:
		return QVector4D(val.to_vec3());
	case NodeValue::k_vec4:
	default:
		return val.to_vec4();
	}
}

void MathNodeBase::push_vector(NodeValueTable *output,
							  olive::NodeValue::Type type,
							  const QVector4D &vec) const
{
	switch (type) {
	case NodeValue::k_vec2:
		output->push(type, QVector2D(vec), this);
		break;
	case NodeValue::k_vec3:
		output->push(type, QVector3D(vec), this);
		break;
	case NodeValue::k_vec4:
		output->push(type, vec, this);
		break;
	default:
		break;
	}
}

QString MathNodeBase::get_operation_name(Operation o)
{
	switch (o) {
	case k_op_add:
		return tr("Add");
	case k_op_subtract:
		return tr("Subtract");
	case k_op_multiply:
		return tr("Multiply");
	case k_op_divide:
		return tr("Divide");
	case k_op_power:
		return tr("Power");
	}

	return QString();
}

void MathNodeBase::perform_all_on_float_buffer(Operation operation, float *a,
										   float b, int start, int end)
{
	for (int j = start; j < end; j++) {
		a[j] = perform_all(operation, a[j], b);
	}
}

#if defined(Q_PROCESSOR_X86) || defined(Q_PROCESSOR_ARM)
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
	Operation operation, Pairing pairing, const QString &param_a_in,
	const NodeValue &val_a, const QString &param_b_in, const NodeValue &val_b,
	const NodeGlobals &globals, NodeValueTable *output) const
{
	switch (pairing) {
	case k_pair_number_number: {
		if (val_a.type() == NodeValue::k_rational &&
			val_b.type() == NodeValue::k_rational && operation != k_op_power) {
			// Preserve rationals
			output->push(
				NodeValue::k_rational,
				QVariant::fromValue(perform_add_sub_mult_div<Rational, Rational>(
					operation, val_a.to_rational(), val_b.to_rational())),
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
		// We convert all vectors to QVector4D just for simplicity and exploit the fact that kVec4 is higher than kVec2 in
		// the enum to find the largest data type
		QVector4D vec_a = retrieve_vector(val_a);
		QVector4D vec_b = retrieve_vector(val_b);

		if (operation == k_op_divide) {
			// Lower-dimensional vectors are padded with zeros; dividing the
			// padding components would be 0/0 (assert in Qt debug builds, NaN
			// otherwise). Force those components to 0/1 so the result is a
			// well-defined zero, which is discarded by PushVector anyway.
			const NodeValue::Type max_type = qMax(val_a.type(), val_b.type());
			if (max_type == NodeValue::k_vec2) {
				vec_a.setZ(0.0f);
				vec_a.setW(0.0f);
				vec_b.setZ(1.0f);
				vec_b.setW(1.0f);
			} else if (max_type == NodeValue::k_vec3) {
				vec_a.setW(0.0f);
				vec_b.setW(1.0f);
			}
		}

		push_vector(output, qMax(val_a.type(), val_b.type()),
				   perform_add_sub_mult_div<QVector4D, QVector4D>(operation, vec_a,
															  vec_b));
		break;
	}

	case k_pair_matrix_vec: {
		QMatrix4x4 matrix = (val_a.type() == NodeValue::k_matrix) ?
								val_a.to_matrix() :
								val_b.to_matrix();
		QVector4D vec = (val_a.type() == NodeValue::k_matrix) ?
							retrieve_vector(val_b) :
							retrieve_vector(val_a);

		// Only valid operation is multiply
		push_vector(output, qMax(val_a.type(), val_b.type()),
				   perform_mult<QVector4D, QMatrix4x4>(operation, vec, matrix));
		break;
	}

	case k_pair_vec_number: {
		QVector4D vec = (NodeValue::type_is_vector(val_a.type()) ?
							 retrieve_vector(val_a) :
							 retrieve_vector(val_b));
		float number = retrieve_number(NodeValue::type_is_vector(val_a.type()) ?
										  val_b :
										  val_a);

		// Only multiply and divide are valid operations
		push_vector(output,
				   NodeValue::type_is_vector(val_a.type()) ? val_a.type() :
															 val_b.type(),
				   perform_mult_div<QVector4D, float>(operation, vec, number));
		break;
	}

	case k_pair_matrix_matrix: {
		QMatrix4x4 mat_a = val_a.to_matrix();
		QMatrix4x4 mat_b = val_b.to_matrix();
		output->push(NodeValue::k_matrix,
					 perform_add_sub_mult<QMatrix4x4, QMatrix4x4>(operation, mat_a,
															   mat_b),
					 this);
		break;
	}

	case k_pair_color_color: {
		Color col_a = val_a.to_color();
		Color col_b = val_b.to_color();

		// Only add and subtract are valid operations
		output->push(NodeValue::k_color,
					 QVariant::fromValue(
						 perform_add_sub<Color, Color>(operation, col_a, col_b)),
					 this);
		break;
	}

	case k_pair_number_color: {
		Color col = (val_a.type() == NodeValue::k_color) ? val_a.to_color() :
														  val_b.to_color();
		float num = (val_a.type() == NodeValue::k_color) ? val_b.to_double() :
														  val_a.to_double();

		// Only multiply and divide are valid operations
		output->push(
			NodeValue::k_color,
			QVariant::fromValue(perform_mult<Color, float>(operation, col, num)),
			this);
		break;
	}

	case k_pair_sample_sample: {
		SampleBuffer samples_a = val_a.to_samples();
		SampleBuffer samples_b = val_b.to_samples();

		size_t max_samples =
			qMax(samples_a.sample_count(), samples_b.sample_count());
		size_t min_samples =
			qMin(samples_a.sample_count(), samples_b.sample_count());

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

		output->push(NodeValue::k_samples, QVariant::fromValue(mixed_samples),
					 this);
		break;
	}

	case k_pair_texture_color:
	case k_pair_texture_number:
	case k_pair_texture_texture:
	case k_pair_texture_matrix: {
		ShaderJob job;
		job.set_shader_id(QStringLiteral("%1.%2.%3.%4")
							.arg(QString::number(operation),
								 QString::number(pairing),
								 QString::number(val_a.type()),
								 QString::number(val_b.type())));

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
			const QVector2D &sequence_res = globals.nonsquare_resolution();
			QVector2D texture_res(texture->params().width() *
									  texture->pixel_aspect_ratio().to_double(),
								  texture->params().height());

			QMatrix4x4 adjusted_matrix =
				TransformDistortNode::adjust_matrix_by_resolutions(
					number_val.to_matrix(), sequence_res,
					texture->params().offset(), texture_res);

			if (operation != k_op_multiply || adjusted_matrix.isIdentity()) {
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
		const QString &number_param =
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
#if defined(Q_PROCESSOR_X86) || defined(Q_PROCESSOR_ARM)
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

				output->push(NodeValue::k_samples, QVariant::fromValue(buffer),
							 this);
			} else {
				SampleJob job(globals.time(),
							  val_a.type() == NodeValue::k_samples ? val_a :
																	val_b);
				job.insert(number_param,
						   NodeValue(NodeValue::k_float, number, this));
				output->push(NodeValue::k_samples, QVariant::fromValue(job),
							 this);
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
										  const QString &param_a_in,
										  const QString &param_b_in,
										  const olive::SampleBuffer &input,
										  olive::SampleBuffer &output,
										  int index) const
{
	// This function is only used for sample+number pairing
	NodeValue number_val = values[param_a_in];

	if (number_val.type() == NodeValue::k_none) {
		number_val = values[param_b_in];

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
		if (qIsNull(number)) {
			return true;
		}
		break;
	case k_op_multiply:
	case k_op_divide:
	case k_op_power:
		if (qFuzzyCompare(number, 1.0f)) {
			return true;
		}
		break;
	}

	return false;
}

MathNodeBase::PairingCalculator::PairingCalculator(
	const NodeValueTable &table_a, const NodeValueTable &table_b)
{
	QVector<int> pair_likelihood_a = get_pair_likelihood(table_a);
	QVector<int> pair_likelihood_b = get_pair_likelihood(table_b);

	int weight_a = qMax(0, table_b.count() - table_a.count());
	int weight_b = qMax(0, table_a.count() - table_b.count());

	QVector<int> likelihoods(k_pair_count);

	for (int i = 0; i < k_pair_count; i++) {
		if (pair_likelihood_a.at(i) == -1 || pair_likelihood_b.at(i) == -1) {
			likelihoods.replace(i, -1);
		} else {
			likelihoods.replace(i, pair_likelihood_a.at(i) + weight_a +
									   pair_likelihood_b.at(i) + weight_b);
		}
	}

	most_likely_pairing_ = k_pair_none;

	for (int i = 0; i < likelihoods.size(); i++) {
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

QVector<int>
MathNodeBase::PairingCalculator::get_pair_likelihood(const NodeValueTable &table)
{
	QVector<int> likelihood(k_pair_count, -1);

	for (int i = 0; i < table.count(); i++) {
		NodeValue::Type type = table.at(i).type();

		int weight = i;

		if (NodeValue::type_is_vector(type)) {
			likelihood.replace(k_pair_vec_vec, weight);
			likelihood.replace(k_pair_vec_number, weight);
			likelihood.replace(k_pair_matrix_vec, weight);
		} else if (type == NodeValue::k_matrix) {
			likelihood.replace(k_pair_matrix_matrix, weight);
			likelihood.replace(k_pair_matrix_vec, weight);
			likelihood.replace(k_pair_texture_matrix, weight);
		} else if (type == NodeValue::k_color) {
			likelihood.replace(k_pair_color_color, weight);
			likelihood.replace(k_pair_number_color, weight);
			likelihood.replace(k_pair_texture_color, weight);
		} else if (NodeValue::type_is_numeric(type)) {
			likelihood.replace(k_pair_number_number, weight);
			likelihood.replace(k_pair_vec_number, weight);
			likelihood.replace(k_pair_number_color, weight);
			likelihood.replace(k_pair_texture_number, weight);
			likelihood.replace(k_pair_sample_number, weight);
		} else if (type == NodeValue::k_samples) {
			likelihood.replace(k_pair_sample_sample, weight);
			likelihood.replace(k_pair_sample_number, weight);
		} else if (type == NodeValue::k_texture) {
			likelihood.replace(k_pair_texture_texture, weight);
			likelihood.replace(k_pair_texture_number, weight);
			likelihood.replace(k_pair_texture_color, weight);
			likelihood.replace(k_pair_texture_matrix, weight);
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
