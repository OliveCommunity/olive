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

//! Project load/save/import tasks, mirroring `src/task/src/project/*`.
//!
//! These tasks move data across the oaknode C ABI: they borrow node handles
//! while running and take ownership only on the `take_*` accessors
//! (architectural decision #5 in README.md).

pub mod import;
pub mod load;
pub mod loadotio;
pub mod save;
pub mod saveotio;
