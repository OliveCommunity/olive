// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//! Shared base for binary math nodes (C++
//! `src/node/src/math/math/mathbase.{h,cpp}`, `olive::MathNodeBase`).
//! Not an instantiable node — modeled as a helper module: the C++
//! class carries no data members, only the operation/pairing enums,
//! the pairing heuristic, and the static eval/shader helpers shared
//! by `MathNode` (and conceptually other binary math nodes).

use crate::value::{NodeValue, NodeValueTable};

/// Binary operation (C++ `MathNodeBase::Operation`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operation {
	/// Addition (C++ `k_op_add`).
	Add,
	/// Subtraction (C++ `k_op_subtract`).
	Subtract,
	/// Multiplication (C++ `k_op_multiply`).
	Multiply,
	/// Division (C++ `k_op_divide`).
	Divide,
	/// Exponentiation (C++ `k_op_power`).
	Power,
}

/// Operand type pairing (C++ `MathNodeBase::Pairing`; discriminants
/// match the C++ enum because they are serialized into shader ids as
/// `"<op>.<pairing>.<type_a>.<type_b>"`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Pairing {
	/// No valid pairing (C++ `k_pair_none = -1`).
	None = -1,
	/// number op number.
	NumberNumber,
	/// vec op vec.
	VecVec,
	/// matrix op matrix.
	MatrixMatrix,
	/// color op color.
	ColorColor,
	/// texture op texture.
	TextureTexture,
	/// vec op number.
	VecNumber,
	/// matrix op vec.
	MatrixVec,
	/// number op color.
	NumberColor,
	/// texture op number.
	TextureNumber,
	/// texture op color.
	TextureColor,
	/// texture op matrix.
	TextureMatrix,
	/// samples op samples.
	SampleSample,
	/// samples op number.
	SampleNumber,
}

/// Pairing heuristic (C++ `MathNodeBase::PairingCalculator`):
/// inspects the two operand value tables and picks the most likely
/// [`Pairing`] plus the concrete values to feed the operation.
pub struct PairingCalculator {
	/// Chosen pairing (C++ `most_likely_pairing_`).
	pub most_likely_pairing: Pairing,
	/// Chosen operand A (C++ `most_likely_value_a_`).
	pub most_likely_value_a: NodeValue,
	/// Chosen operand B (C++ `most_likely_value_b_`).
	pub most_likely_value_b: NodeValue,
}

impl PairingCalculator {
	/// Build from the two operand tables (C++ `PairingCalculator()`
	/// constructor): per-table `get_pair_likelihood` weight vectors are
	/// combined (longer table gets a weight bonus), and the highest
	/// non-negative combined likelihood wins; the winning operands are
	/// copied out of the tables.
	pub fn new(table_a: &NodeValueTable, table_b: &NodeValueTable) -> Self {
		todo!()
	}

	/// Whether a pairing was found (C++ `found_most_likely_pairing()`).
	pub fn found_most_likely_pairing(&self) -> bool {
		todo!()
	}
}

/// Stateless helper namespace for the C++ `MathNodeBase` static
/// methods (unit-like: the C++ base class has no data members).
pub struct MathNodeBase;

impl MathNodeBase {
	/// Display name for an operation (C++ `get_operation_name()`):
	/// Add/Subtract/Multiply/Divide/Power.
	pub fn operation_name(op: Operation) -> &'static str {
		match op {
			Operation::Add => "Add",
			Operation::Subtract => "Subtract",
			Operation::Multiply => "Multiply",
			Operation::Divide => "Divide",
			Operation::Power => "Power",
		}
	}

	/// Whether applying `op` with scalar `number` is an identity
	/// (C++ `number_is_no_op()`): 0 for add/subtract, fuzzy-1.0 for
	/// multiply/divide/power.
	pub fn number_is_no_op(op: Operation, number: f32) -> bool {
		todo!()
	}

	/// Shared shader generator (C++ `get_shader_code_internal()`):
	/// parses the `"<op>.<pairing>.<type_a>.<type_b>"` shader id,
	/// builds a fragment shader declaring both operands as uniforms and
	/// applying the GLSL operator (`pow()` for power, with a vec4-wrapped
	/// exponent for texture-number); the texture*matrix multiply case
	/// instead emits a no-op fragment plus a vertex shader that
	/// multiplies `gl_Position` by the matrix uniform.
	pub fn shader_code_internal(shader_id: &str, param_a_in: &str, param_b_in: &str) -> (String, String) {
		todo!()
	}

	/// Shared value evaluation (C++ `value_internal()`): big switch on
	/// the pairing — number/number (preserving rationals unless power),
	/// vec/vec (zero-padding divide guard), matrix*vec, vec/number,
	/// matrix/matrix, color+/-color, color*number, sample buffers
	/// (elementwise, longer tail memcpy'd), texture pairings (shader job
	/// with `"op.pairing.ta.tb"` id; no-op push-through when the texture
	/// is null, the number is identity, or the adjusted matrix is
	/// identity), and sample*number (static: in-place SIMD loop;
	/// dynamic: sample job).
	pub fn value_internal(
		operation: Operation,
		pairing: Pairing,
		param_a_in: &str,
		val_a: &NodeValue,
		param_b_in: &str,
		val_b: &NodeValue,
		output: &mut NodeValueTable,
	) {
		todo!()
	}

	/// Shared per-sample processing for the sample*number pairing
	/// (C++ `process_samples_internal()`): reads the scalar from
	/// `param_a_in` (falling back to `param_b_in`) and applies the
	/// operation to the input sample at `index` on every channel.
	pub fn process_samples_internal(
		values: &crate::value::NodeValueRow,
		operation: Operation,
		param_a_in: &str,
		param_b_in: &str,
		input: &crate::value::SampleBuffer,
		output: &mut crate::value::SampleBuffer,
		index: usize,
	) {
		todo!()
	}

	/// Extract a number from any numeric/rational value (C++
	/// `retrieve_number()`).
	pub fn retrieve_number(val: &NodeValue) -> f32 {
		todo!()
	}
}
