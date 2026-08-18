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

//! The right-click menu layer: the Rust counterpart of the C++ context-menu
//! system (`MenuShared` segments + the per-widget `show_context_menu`
//! methods, `app/widget/menu/menushared.cpp` and friends).
//!
//! * [`context`] — the shared plumbing every panel needs to own a
//!   [`ContextMenu`](gpui_widgets::menu::ContextMenu): entity creation,
//!   event subscription and show/hide, plus the
//!   [`ContextMenuTriggered`](context::ContextMenuTriggered) event the
//!   panels emit so the shell routes registry items through the same
//!   dispatch path the menu bar uses.
//! * [`shared`] — the shared menu segments (edit / clip-edit / in-out /
//!   color label / new), built from the action registry
//!   ([`crate::actions`]) so ids, labels and shortcut annotations can never
//!   diverge from the menu bar.

pub mod context;
pub mod shared;
