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

//! oakcore-rs: Rust value types mirroring the oakcore C++ library.
//!
//! Bit-exact behavioral compatibility with `olive::core` is the design
//! constraint; see README.md.

#![warn(missing_docs)]

mod rational;
mod samplefmt;
mod timerange;

pub use rational::Rational;
pub use samplefmt::{PixelFormat, SampleFormat};
pub use timerange::{TimeRange, TimeRangeList};
