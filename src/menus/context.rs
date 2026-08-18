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

//! The context-menu plumbing every panel shares: each panel owns one
//! [`ContextMenuHandle`], which wraps the
//! [`ContextMenu`](gpui_widgets::menu::ContextMenu) popup entity and splits
//! its item activations in two — items whose id belongs to the action
//! registry ([`crate::actions::entry_for_menu_id`]) are re-emitted as
//! [`ContextMenuTriggered`] so the app shell routes them through the same
//! dispatch path the menu bar uses, and everything else (the local ids from
//! [`super::shared::LOCAL_ID_BASE`] up) goes to the panel's own handler.
//! This is the Rust counterpart of the C++ panels wiring shared
//! `MenuShared` actions and widget-local slots into one `QMenu`.

use gpui::{App, AppContext, Context, Entity, EventEmitter, Pixels, Point, Window};
use gpui_widgets::menu::{ContextMenu, ContextMenuEvent, Menu};

/// A registry-backed context-menu item was triggered: the panel re-emits it
/// so the app shell dispatches it like a menu-bar click (after pointing
/// `focused_panel` at the panel, since a right-click does not emit
/// `PanelEvent::Focused`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ContextMenuTriggered {
	/// The triggered item's id (an action registry menu id).
	pub item: usize,
}

/// A panel's context menu: owns the popup entity, re-emits registry items as
/// [`ContextMenuTriggered`] and hands local items to the panel.
pub struct ContextMenuHandle {
	menu: Entity<ContextMenu>,
}

impl ContextMenuHandle {
	/// Create the popup entity and subscribe to it. `on_local_item` handles
	/// every triggered item that is not in the action registry (color
	/// labels, panel-specific placeholders, …).
	pub fn new<P, F>(on_local_item: F, window: &mut Window, cx: &mut Context<P>) -> Self
	where
		P: EventEmitter<ContextMenuTriggered>,
		F: Fn(&mut P, usize, &mut Context<P>) + 'static,
	{
		let menu = cx.new(|cx| ContextMenu::new(0, window, cx));
		cx.subscribe(
			&menu,
			move |panel: &mut P, _menu, event: &ContextMenuEvent, cx| {
				if crate::actions::entry_for_menu_id(event.item).is_some() {
					cx.emit(ContextMenuTriggered { item: event.item });
				} else {
					on_local_item(panel, event.item, cx);
				}
			},
		)
		.detach();
		Self { menu }
	}

	/// Open the menu at `position` (window coordinates).
	pub fn show(&self, position: Point<Pixels>, menu: Menu, cx: &mut App) {
		self.menu.update(cx, |menu_view, cx| menu_view.show(position, menu, cx));
	}

	/// The popup entity, to be rendered as a child of the panel so the
	/// anchored popup can paint above it.
	pub fn widget(&self) -> Entity<ContextMenu> {
		self.menu.clone()
	}
}
