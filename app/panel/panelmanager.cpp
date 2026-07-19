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

#include "panelmanager.h"

#include "config/config.h"

namespace olive
{

PanelManager *PanelManager::instance_ = nullptr;

PanelManager::PanelManager(QObject *parent)
	: QObject(parent)
	, suppress_changed_signal_(false)
{
}

void PanelManager::delete_all_panels()
{
	// Prevent any confusion regarding focus history by clearing it first
	QList<PanelWidget *> copy = focus_history_;
	focus_history_.clear();
	qDeleteAll(copy);
}

const QList<PanelWidget *> &PanelManager::panels()
{
	return focus_history_;
}

PanelWidget *PanelManager::currently_focused(bool enable_hover) const
{
	// If hover focus is enabled, find the currently hovered panel and return it (if no panel is hovered, resort to
	// default behavior)
	if (enable_hover && OAK_CONFIG("HoverFocus").toBool()) {
		PanelWidget *hovered = currently_hovered();

		if (hovered != nullptr) {
			return hovered;
		}
	}

	if (focus_history_.isEmpty()) {
		return nullptr;
	}

	return focus_history_.first();
}

PanelWidget *PanelManager::currently_hovered() const
{
	QPoint global_mouse = QCursor::pos();

	foreach (PanelWidget *panel, focus_history_) {
		if (panel->rect().contains(panel->mapFromGlobal(global_mouse))) {
			return panel;
		}
	}

	return nullptr;
}

PanelWidget *PanelManager::get_panel_with_name(const QString &name) const
{
	foreach (PanelWidget *panel, focus_history_) {
		if (panel->objectName() == name) {
			return panel;
		}
	}

	return nullptr;
}

void PanelManager::create_instance()
{
	instance_ = new PanelManager();
}

void PanelManager::destroy_instance()
{
	delete instance_;
	instance_ = nullptr;
}

PanelManager *PanelManager::instance()
{
	return instance_;
}

void PanelManager::register_panel(PanelWidget *panel)
{
	// Add panel to the bottom of the focus history
	focus_history_.append(panel);

	// We're about to center the panel relative to the parent (usually the main window), but for some
	// reason this requires the panel to be shown first.
	panel->show();

	if (focus_history_.size() == 1) {
		// This is the first panel, focus it
		panel->set_border_visible(true);
		emit focused_panel_changed(panel);
	}
}

void PanelManager::unregister_panel(PanelWidget *panel)
{
	focus_history_.removeOne(panel);
}

void PanelManager::focus_changed(QWidget *old, QWidget *now)
{
	Q_UNUSED(old)

	QObject *parent = now;
	PanelWidget *panel_cast_test;

	// Loop through widget's parent hierarchy
	if (!focus_history_.empty()) {
		while (parent != nullptr) {
			// Use dynamic_cast to test if this object is a PanelWidget
			panel_cast_test = dynamic_cast<PanelWidget *>(parent);

			if (panel_cast_test) {
				if (focus_history_.first() != panel_cast_test) {
					// If so, bump this to the top of the focus history
					int panel_index = focus_history_.indexOf(panel_cast_test);

					// Disable highlight border on old panel
					if (!focus_history_.isEmpty()) {
						focus_history_.first()->set_border_visible(false);
					}

					// Enable new border's highlight
					panel_cast_test->set_border_visible(true);

					// If it's not in the focus history, prepend it, otherwise move it
					if (panel_index == -1) {
						focus_history_.prepend(panel_cast_test);
					} else {
						focus_history_.move(panel_index, 0);
					}

					if (!suppress_changed_signal_) {
						emit focused_panel_changed(panel_cast_test);
					}
				}

				break;
			}

			parent = parent->parent();
		}
	}
}

}
