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

//! oaktask C ABI export surface. These `#[no_mangle] extern "C"` symbols are
//! the Rust counterpart of the C++ task facade declared in
//! `include/task/*.h`; each module mirrors one header. The public headers are
//! authoritative for signatures; the inventory comments below each module
//! enumerate the full symbol set. Every export passes through
//! [`crate::handle::guard*`] / explicit null checks so panics never cross the
//! FFI boundary.

pub mod manager;
pub mod project;
pub mod task;
pub(crate) mod taskhandle;
