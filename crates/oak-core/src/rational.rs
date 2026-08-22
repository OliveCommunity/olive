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

//! Rational numbers with oakcore-compatible semantics.

/// The cap used by the C++ `reduce_fraction` oracle: it reduces against
/// `INT_MAX` regardless of the wider integer width. We keep the same cap so
/// that every value C++ can represent round-trips bit-for-bit.
const REDUCE_MAX: i128 = i32::MAX as i128;

/// `RATIONAL_MIN` as produced by the C++ `Rational(INT_MIN)` constructor:
/// because the reduce cap is `INT_MAX`, `INT_MIN` reduces to `-2147483647/1`
/// (not `-2147483648/1`). Arithmetic treats this value (and its positive
/// counterpart) as a sentinel that propagates NaN.
const RATIONAL_MIN: Rational = Rational {
	num: -2147483647,
	den: 1,
};

/// `RATIONAL_MAX` (`Rational(INT_MAX)`, i.e. `2147483647/1`).
const RATIONAL_MAX: Rational = Rational {
	num: 2147483647,
	den: 1,
};

/// A rational number, always kept reduced with a non-negative
/// denominator (mirrors `olive::core::Rational`).
///
/// Compatibility notes (these are load-bearing, project files depend
/// on them):
/// - `0/0` is the "null/invalid" sentinel (`Rational()` in C++).
/// - Arithmetic follows the C++ overflow behavior: intermediate
///   products are 128-bit where the C++ uses wider temporaries; where
///   C++ truncates, we truncate identically.
/// - `from_string`/`to_string` round-trip the exact C++ text format
///   (e.g. "30000/1001"), including the sentinel spellings used in
///   project XML ("0/0", RATIONAL_MIN/MAX sentinels).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Default)]
pub struct Rational {
	num: i64,
	den: i64,
}

/// Signed Euclidean GCD on absolute values (mirrors the C++
/// `i64_gcd`). Computed in `i128` so that `i64::MIN`-class inputs
/// cannot overflow when negated.
fn i64_gcd(mut a: i128, mut b: i128) -> i128 {
	if a < 0 {
		a = -a;
	}
	if b < 0 {
		b = -b;
	}

	while b != 0 {
		let t = a % b;
		a = b;
		b = t;
	}

	a
}

/// Reduce `num`/`den` in place so that `|num| <= max` and `den <= max`,
/// using the exact C++ algorithm (`core/src/util/fractionutils.cpp`,
/// ported from FFmpeg's `av_reduce`). Implemented in `i128` so the
/// intermediate products never overflow for any `i64` input.
fn reduce_fraction(num: &mut i128, den: &mut i128, max: i128) {
	if *den == 0 {
		*num = 0;
		return;
	}

	let sign = (*num < 0) != (*den < 0);

	let gcd = i64_gcd(*num, *den);
	if gcd != 0 {
		*num = if *num < 0 { -*num } else { *num } / gcd;
		*den = if *den < 0 { -*den } else { *den } / gcd;
	}

	if *num <= max && *den <= max {
		*num = if sign { -*num } else { *num };
		return;
	}

	// Continued fraction approximation (FFmpeg's av_reduce).
	let mut a0n: i128 = 0;
	let mut a0d: i128 = 1;
	let mut a1n: i128 = 1;
	let mut a1d: i128 = 0;

	let mut n = *num;
	let mut d = *den;

	while d != 0 {
		let x = n / d;
		let next_den = n - d * x;
		let a2n = x * a1n + a0n;
		let a2d = x * a1d + a0d;

		if a2n > max || a2d > max {
			let mut x = x;
			if a1n != 0 {
				x = (max - a0n) / a1n;
			}
			if a1d != 0 && (max - a0d) / a1d < x {
				x = (max - a0d) / a1d;
			}

			if d * (2 * x * a1d + a0d) > n * a1d {
				a1n = x * a1n + a0n;
				a1d = x * a1d + a0d;
			}
			break;
		}

		a0n = a1n;
		a0d = a1d;
		a1n = a2n;
		a1d = a2d;
		n = d;
		d = next_den;
	}

	*num = if sign { -a1n } else { a1n };
	*den = a1d;
}

/// C `frexp`: split into mantissa in [0.5, 1) and base-2 exponent.
/// Only used by `from_double`; NaN/inf/zero pass through with exp 0.
fn frexp(x: f64, exp: &mut i32) -> f64 {
	if x == 0.0 || x.is_nan() || x.is_infinite() {
		*exp = 0;
		return x;
	}
	let bits = x.to_bits();
	let raw = ((bits >> 52) & 0x7ff) as i32;
	if raw == 0 {
		// Subnormal: scale up into the normal range first.
		let scaled = x * 9007199254740992.0; // 2^53
		let mut e = 0;
		let m = frexp(scaled, &mut e);
		*exp = e - 53;
		return m;
	}
	*exp = raw - 1022;
	f64::from_bits((bits & !(0x7ffu64 << 52)) | (1022u64 << 52))
}

/// Apply C++ `fix_signs`: negative denominators are normalized by
/// flipping both signs; `0/0` stays as the NaN sentinel; a zero
/// numerator becomes `0/1`.
fn fix_signs(num: &mut i64, den: &mut i64) {
	if *den < 0 {
		*den = -*den;
		*num = -*num;
	} else if *den == 0 {
		*num = 0;
	} else if *num == 0 {
		*den = 1;
	}
}

/// Build a rational from already-reduced `i128` values, applying
/// `fix_signs` and narrowing to `i64` (safe: `reduce_fraction` caps at
/// `i32::MAX`).
fn from_reduced(num: i128, den: i128) -> Rational {
	let mut num = num as i64;
	let mut den = den as i64;
	fix_signs(&mut num, &mut den);
	Rational { num, den }
}

/// Compare two fractions exactly (C++ `compare_fractions`). Non-NaN
/// inputs yield `-1`/`0`/`1`; the `0/0` cases return `i32::MIN`
/// (meaningless, never used for total ordering).
fn compare_fractions(an: i64, ad: i64, bn: i64, bd: i64) -> i32 {
	let tmp = an as i128 * bd as i128 - bn as i128 * ad as i128;

	if tmp != 0 {
		// C++: `((tmp ^ ad ^ bd) >> 63) | 1` == sign of tmp (dens are >= 0).
		if tmp > 0 {
			1
		} else {
			-1
		}
	} else if bd != 0 && ad != 0 {
		0
	} else if an != 0 && bn != 0 {
		((an >> 31) - (bn >> 31)) as i32
	} else {
		i32::MIN
	}
}

/// Parse a single C++ `strtol`-style integer (base 10); garbage or
/// empty input yields 0.
fn to_int(s: &str) -> i64 {
	s.trim().parse::<i64>().unwrap_or(0)
}

/// Rounding modes for the C++ `Timecode` conversion helpers.
#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum Rounding {
	Round,
	Floor,
}

/// C++ `Rational::flipped` as a free function: swap numerator and
/// denominator, then `fix_signs`. A null rational (0/0 or 0/n) is left
/// unchanged.
fn flipped(r: Rational) -> Rational {
	if r.num == 0 {
		return r;
	}
	let mut num = r.den;
	let mut den = r.num;
	fix_signs(&mut num, &mut den);
	Rational { num, den }
}

/// C++ `Timecode::timestamp_to_time`: `timebase.num * ts / timebase.den`,
/// reduced against `INT_MAX`.
fn timestamp_to_time(ts: i64, timebase: Rational) -> Rational {
	let mut num = timebase.num as i128 * ts as i128;
	let mut den = timebase.den as i128;
	reduce_fraction(&mut num, &mut den, REDUCE_MAX);
	from_reduced(num, den)
}

/// C++ `Timecode::time_to_timestamp` (any rounding mode), given an
/// explicit timebase.
pub(crate) fn time_to_timestamp_rnd(time: Rational, timebase: Rational, rnd: Rounding) -> i64 {
	let d = time.to_f64() * flipped(timebase).to_f64();

	if d.is_nan() {
		return 0;
	}

	let eps = 0.000000000001;

	match rnd {
		Rounding::Round => d.round() as i64,
		Rounding::Floor => {
			if d > d.ceil() - eps {
				d.ceil() as i64
			} else {
				d.floor() as i64
			}
		}
	}
}

/// C++ `Timecode::snap_time_to_timebase` with the `k_floor` rounding
/// used by `TimeRangeListFrameIterator`.
pub(crate) fn snap_time_to_timebase(time: Rational, timebase: Rational) -> Rational {
	let ts = time_to_timestamp_rnd(time, timebase, Rounding::Floor);
	timestamp_to_time(ts, timebase)
}

impl Rational {
	/// The invalid sentinel (C++ `Rational()`, i.e. 0/0).
	pub const NULL: Rational = Rational { num: 0, den: 0 };

	/// Construct reduced; `new(0, 0)` yields [`Rational::NULL`].
	pub fn new(num: i64, den: i64) -> Rational {
		let mut num = num;
		let mut den = den;
		fix_signs(&mut num, &mut den);
		let mut num = num as i128;
		let mut den = den as i128;
		reduce_fraction(&mut num, &mut den, REDUCE_MAX);
		Rational {
			num: num as i64,
			den: den as i64,
		}
	}

	/// Numerator of the reduced form.
	pub fn numerator(self) -> i64 {
		self.num
	}

	/// Denominator of the reduced form (0 for the null sentinel).
	pub fn denominator(self) -> i64 {
		self.den
	}

	/// True for the null sentinel (`num == 0`, so 0/0 and 0/1).
	pub fn is_null(self) -> bool {
		self.num == 0
	}

	/// True for the NaN sentinel (`den == 0`, only 0/0 after
	/// normalization). C++ `isNaN()`.
	pub fn is_nan(self) -> bool {
		self.den == 0
	}

	/// True when this value equals `RATIONAL_MIN` or `RATIONAL_MAX`
	/// (the sentinels that propagate NaN through arithmetic in C++).
	fn is_minmax(self) -> bool {
		self == RATIONAL_MIN || self == RATIONAL_MAX
	}

	/// Parse the C++ text format; invalid input yields the null
	/// sentinel (C++ `fromString` behavior).
	pub fn from_string(s: &str) -> Rational {
		let elements: Vec<&str> = s.split('/').collect();
		match elements.len() {
			1 => Rational::new(to_int(elements[0]), 1),
			2 => Rational::new(to_int(elements[0]), to_int(elements[1])),
			_ => Rational::NULL,
		}
	}

	/// Format identical to C++ `toString()`.
	pub fn to_display_string(self) -> String {
		format!("{}/{}", self.num, self.den)
	}

	/// Truncating conversion to f64 (C++ `toDouble`).
	pub fn to_f64(self) -> f64 {
		if self.den != 0 {
			self.num as f64 / self.den as f64
		} else {
			f64::NAN
		}
	}

	/// f64 → Rational (C++ `Rational::from_double`, continued-fraction
	/// port of FFmpeg's `av_d2q`; NaN and |v| > INT_MAX+3 yield the
	/// 0/0 NaN sentinel).
	/// `// CPP-PARITY: core/src/util/rational.cpp:39`
	pub fn from_double(value: f64) -> Rational {
		if value.is_nan() || value.abs() > i32::MAX as f64 + 3.0 {
			return Rational::NULL;
		}

		let mut exponent = 0;
		let _ = frexp(value, &mut exponent);
		exponent = (exponent - 1).max(0);
		let den: i64 = 1i64 << (62 - exponent);
		let num: i64 = (value * den as f64 + 0.5).floor() as i64;

		let mut rnum = num as i128;
		let mut rden = den as i128;
		reduce_fraction(&mut rnum, &mut rden, i32::MAX as i128);

		if (rnum == 0 || rden == 0) && value != 0.0 {
			// Too small to represent above; retry at maximum precision.
			rnum = (value * i64::MAX as f64) as i64 as i128;
			rden = i64::MAX as i128;
			reduce_fraction(&mut rnum, &mut rden, i32::MAX as i128);
		}

		from_reduced(rnum, rden)
	}

	/// Frame-number conversion using this value as a timebase
	/// (C++ `Timecode::time_to_timestamp` semantics, rounding mode
	/// included).
	pub fn time_to_timestamp(self, time: Rational) -> i64 {
		time_to_timestamp_rnd(time, self, Rounding::Round)
	}

	/// Inverse of [`Rational::time_to_timestamp`]
	/// (C++ `Timecode::timestamp_to_time`).
	pub fn timestamp_to_time(self, ts: i64) -> Rational {
		timestamp_to_time(ts, self)
	}
}

impl std::ops::Add for Rational {
	type Output = Rational;
	fn add(self, rhs: Rational) -> Rational {
		if self.is_minmax() || rhs.is_minmax() {
			return Rational::NULL;
		}
		if self.is_nan() {
			return self;
		}
		if rhs.is_nan() {
			return Rational::NULL;
		}
		let mut n = self.num as i128 * rhs.den as i128 + rhs.num as i128 * self.den as i128;
		let mut d = self.den as i128 * rhs.den as i128;
		reduce_fraction(&mut n, &mut d, REDUCE_MAX);
		from_reduced(n, d)
	}
}

impl std::ops::Sub for Rational {
	type Output = Rational;
	fn sub(self, rhs: Rational) -> Rational {
		if self.is_minmax() || rhs.is_minmax() {
			return Rational::NULL;
		}
		if self.is_nan() {
			return self;
		}
		if rhs.is_nan() {
			return Rational::NULL;
		}
		let mut n = self.num as i128 * rhs.den as i128 - rhs.num as i128 * self.den as i128;
		let mut d = self.den as i128 * rhs.den as i128;
		reduce_fraction(&mut n, &mut d, REDUCE_MAX);
		from_reduced(n, d)
	}
}

impl std::ops::Mul for Rational {
	type Output = Rational;
	fn mul(self, rhs: Rational) -> Rational {
		if self.is_minmax() || rhs.is_minmax() {
			return Rational::NULL;
		}
		if self.is_nan() {
			return self;
		}
		if rhs.is_nan() {
			return Rational::NULL;
		}
		let mut n = self.num as i128 * rhs.num as i128;
		let mut d = self.den as i128 * rhs.den as i128;
		reduce_fraction(&mut n, &mut d, REDUCE_MAX);
		from_reduced(n, d)
	}
}

impl std::ops::Div for Rational {
	type Output = Rational;
	fn div(self, rhs: Rational) -> Rational {
		if self.is_minmax() || rhs.is_minmax() {
			return Rational::NULL;
		}
		if self.is_nan() {
			return self;
		}
		if rhs.is_nan() {
			return Rational::NULL;
		}
		let mut n = self.num as i128 * rhs.den as i128;
		let mut d = self.den as i128 * rhs.num as i128;
		reduce_fraction(&mut n, &mut d, REDUCE_MAX);
		from_reduced(n, d)
	}
}

impl PartialOrd for Rational {
	fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
		Some(self.cmp(other))
	}
}

impl Ord for Rational {
	fn cmp(&self, other: &Self) -> std::cmp::Ordering {
		use std::cmp::Ordering;
		// NaN (0/0) orders before everything and equals itself, keeping
		// `Ord` consistent with the derived structural `Eq` (C++ makes
		// 0/0 == 0/0 false, but this crate deliberately keeps Eq).
		match (self.den == 0, other.den == 0) {
			(true, true) => Ordering::Equal,
			(true, false) => Ordering::Less,
			(false, true) => Ordering::Greater,
			(false, false) => match compare_fractions(self.num, self.den, other.num, other.den) {
				0 => Ordering::Equal,
				1 => Ordering::Greater,
				_ => Ordering::Less,
			},
		}
	}
}
