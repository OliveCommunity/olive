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

#ifndef OAK_SEQUENCEDIALOGPRESETTAB_H
#define OAK_SEQUENCEDIALOGPRESETTAB_H

#include <QLabel>
#include <QTreeWidget>
#include <QWidget>

#include "presetmanager.h"
#include "sequencepreset.h"

namespace olive
{

class SequenceDialogPresetTab : public QWidget,
								public PresetManager<SequencePreset> {
	Q_OBJECT
public:
	SequenceDialogPresetTab(QWidget *parent = nullptr);

public slots:
	void save_parameters_as_preset(SequencePreset preset);

signals:
	void preset_changed(const SequencePreset &preset);

	void preset_accepted();

private:
	QTreeWidgetItem *create_folder(const QString &name);

	QTreeWidgetItem *create_hd_preset_folder(const QString &name, int width,
										  int height, int divider);

	QTreeWidgetItem *create_sd_preset_folder(
		const QString &name, int width, int height, const Rational &frame_rate,
		const Rational &standard_par, const Rational &wide_par, int divider);

	QTreeWidgetItem *get_selected_item();
	QTreeWidgetItem *get_selected_custom_preset();

	void add_standard_item(QTreeWidgetItem *folder, PresetPtr preset,
						 const QString &description = QString());

	void add_custom_item(QTreeWidgetItem *folder, PresetPtr preset, int index,
					   const QString &description = QString());

	void add_item_internal(QTreeWidgetItem *folder, PresetPtr preset,
						 bool is_custom, int index,
						 const QString &description = QString());

	QTreeWidget *preset_tree_;

	QTreeWidgetItem *my_presets_folder_;

	QVector<PresetPtr> default_preset_data_;

private slots:
	void selected_item_changed(QTreeWidgetItem *current,
							 QTreeWidgetItem *previous);

	void item_double_clicked(QTreeWidgetItem *item, int column);

	void show_context_menu();

	void delete_selected_preset();
};

}

#endif // OAK_SEQUENCEDIALOGPRESETTAB_H
