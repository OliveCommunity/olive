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

#ifndef OAK_PANELFOCUSMANAGER_H
#define OAK_PANELFOCUSMANAGER_H

#include <QObject>
#include <QList>

#include "panel/panel.h"

namespace olive
{

/**
 * @brief The PanelFocusManager class
 *
 * This object keeps track of which panel is focused at any given time.
 *
 * Sometimes a function (specifically a keyboard-triggered one, e.g. Delete) may have different purposes depending on
 * which panel is "focused" at any given time. Pressing Delete on the Timeline is not the same as pressing Delete in
 * the Project panel, for example. This kind of "focus" is slightly different from standard QWidget focus, since it
 * aims to be less specific than a single QPushButton or QLineEdit, and rather specific to the panel widgets like
 * that belong to.
 *
 * PanelFocusManager's SLOT(focus_changed()) connects to the QApplication instance_'s SIGNAL(focusChanged()) so that
 * it always knows when focus has changed within the application.
 */
class PanelManager : public QObject {
	Q_OBJECT
public:
	PanelManager(QObject *parent = nullptr);

	/**
   * @brief Destroy all panels
   *
   * Should only be used on application exit to cleanly free all panels.
   */
	void delete_all_panels();

	/**
   * @brief Get a list of all existing panels
   *
   * Panels are ordered from most recently focused to least recently focused.
   */
	const QList<PanelWidget *> &panels();

	/**
   * @brief Return the currently focused widget, or nullptr if nothing is focused
   *
   * This result == CurrentlyFocused() if HoverFocus is true and panel is hovered
   */
	PanelWidget *currently_focused(bool enable_hover = true) const;

	/**
   * @brief Return the widget that the mouse is currently hovering over, or nullptr if nothing is hovered over
   */
	PanelWidget *currently_hovered() const;

	PanelWidget *get_panel_with_name(const QString &name) const;

	template <class T>
	/**
   * @brief Get most recently focused panel of a certain type
   *
   * @return
   *
   * The most recently focused panel of the specified type, or nullptr if none exists
   */
	T *most_recently_focused();

	/**
   * @brief Create PanelManager singleton instance_
   */
	static void create_instance();

	/**
   * @brief Destroy PanelManager singleton instance_
   *
   * If no PanelManager was created, this is a no-op.
   */
	static void destroy_instance();

	/**
   * @brief Access to PanelManager singleton instance_
   */
	static PanelManager *instance();

	template <class T>
	/**
   * @brief Get a list of panels of a certain type
   */
	QList<T *> get_panels_of_type();

	/**
   * @brief Panel should call this upon construction so it can be kept track of
   */
	void register_panel(PanelWidget *panel);

	/**
   * @brief Panel should call this upon destruction so no invalid pointers will be kept for it
   */
	void unregister_panel(PanelWidget *panel);

	void set_suppress_changed_signal(bool e)
	{
		suppress_changed_signal_ = e;
	}

public slots:
	/**
   * @brief Connect this to a QApplication's SIGNAL(focusChanged())
   *
   * Interprets focus information to determine the currently focused panel
   */
	void focus_changed(QWidget *old, QWidget *now);

signals:
	/**
   * @brief Signal emitted when the currently focused panel changes
   */
	void focused_panel_changed(PanelWidget *panel);

private:
	/**
   * @brief History array for traversing through (see MostRecentlyFocused())
   */
	QList<PanelWidget *> focus_history_;

	/**
   * @brief PanelManager singleton instance_
   */
	static PanelManager *instance_;

	bool suppress_changed_signal_;
};

template <class T> T *PanelManager::most_recently_focused()
{
	T *cast_test;

	for (int i = 0; i < focus_history_.size(); i++) {
		cast_test = dynamic_cast<T *>(focus_history_.at(i));

		if (cast_test != nullptr) {
			return cast_test;
		}
	}

	return nullptr;
}

template <class T> QList<T *> PanelManager::get_panels_of_type()
{
	QList<T *> panels;

	T *cast_test;

	foreach (PanelWidget *panel, focus_history_) {
		cast_test = dynamic_cast<T *>(panel);

		if (cast_test) {
			panels.append(cast_test);
		}
	}

	return panels;
}

}

#endif // OAK_PANELFOCUSMANAGER_H
