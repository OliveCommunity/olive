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

#include "sequencedialogpresettab.h"

#include <QDir>
#include <QGroupBox>
#include <QInputDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QSplitter>
#include <QTreeWidgetItem>
#include <QXmlStreamWriter>

#include "config/config.h"
#include "render/videoparams.h"
#include "ui/icons/icons.h"
#include "widget/menu/menu.h"

namespace olive
{

const int k_data_is_preset = Qt::UserRole;
const int k_data_preset_is_custom_role = Qt::UserRole + 1;
const int k_data_preset_data_role = Qt::UserRole + 2;

SequenceDialogPresetTab::SequenceDialogPresetTab(QWidget *parent)
	: QWidget(parent)
	, PresetManager<SequencePreset>(this, QStringLiteral("sequencepresets"))
{
	QVBoxLayout *outer_layout = new QVBoxLayout(this);
	outer_layout->setContentsMargins(0, 0, 0, 0);

	preset_tree_ = new QTreeWidget();
	preset_tree_->setColumnCount(1);
	preset_tree_->setHeaderLabel(tr("Preset"));
	preset_tree_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(preset_tree_, &QTreeWidget::customContextMenuRequested, this,
			&SequenceDialogPresetTab::show_context_menu);
	outer_layout->addWidget(preset_tree_);
	connect(preset_tree_, &QTreeWidget::currentItemChanged, this,
			&SequenceDialogPresetTab::selected_item_changed);
	connect(preset_tree_, &QTreeWidget::itemDoubleClicked, this,
			&SequenceDialogPresetTab::item_double_clicked);

	// Add "my presets" folder
	my_presets_folder_ = create_folder(tr("My Presets"));
	preset_tree_->addTopLevelItem(my_presets_folder_);

	// Add presets
	preset_tree_->addTopLevelItem(
		create_hd_preset_folder(tr("4K UHD"), 3840, 2160, 2));
	preset_tree_->addTopLevelItem(
		create_hd_preset_folder(tr("1080p"), 1920, 1080, 1));
	preset_tree_->addTopLevelItem(
		create_hd_preset_folder(tr("720p"), 1280, 720, 1));

	preset_tree_->addTopLevelItem(
		create_sd_preset_folder(tr("NTSC"), 720, 480, Rational(30000, 1001),
							 VideoParams::k_pixel_aspect_ntsc_standard,
							 VideoParams::k_pixel_aspect_ntsc_widescreen, 1));
	preset_tree_->addTopLevelItem(
		create_sd_preset_folder(tr("PAL"), 720, 576, Rational(25, 1),
							 VideoParams::k_pixel_aspect_pal_standard,
							 VideoParams::k_pixel_aspect_pal_widescreen, 1));

	// Load custom presets
	for (int i = 0; i < get_number_of_presets(); i++) {
		add_custom_item(my_presets_folder_, get_preset(i), i);
	}
}

void SequenceDialogPresetTab::save_parameters_as_preset(SequencePreset preset)
{
	PresetPtr preset_ptr = std::make_shared<SequencePreset>(preset);

	// If replaced, no need to make another item. If not saved, shared ptr will delete itself
	if (save_preset(preset_ptr) == k_appended) {
		add_custom_item(my_presets_folder_, preset_ptr, get_number_of_presets() - 1);
	}
}

QTreeWidgetItem *SequenceDialogPresetTab::create_folder(const QString &name)
{
	QTreeWidgetItem *folder = new QTreeWidgetItem();
	folder->setText(0, name);
	folder->setIcon(0, icon::folder);
	return folder;
}

QTreeWidgetItem *
SequenceDialogPresetTab::create_hd_preset_folder(const QString &name, int width,
											  int height, int divider)
{
	const PixelFormat default_format = static_cast<PixelFormat::Format>(
		OAK_CONFIG("OfflinePixelFormat").toInt());
	const bool default_autocache = false;
	QTreeWidgetItem *parent = create_folder(name);
	const uint64_t layout = k_channel_layout_stereo;
	add_standard_item(parent,
					std::make_shared<SequencePreset>(
						tr("%1 23.976 FPS").arg(name), width, height,
						Rational(24000, 1001), VideoParams::k_pixel_aspect_square,
						VideoParams::k_interlace_none, 48000, layout, divider,
						default_format, default_autocache));
	add_standard_item(parent,
					std::make_shared<SequencePreset>(
						tr("%1 25 FPS").arg(name), width, height,
						Rational(25, 1), VideoParams::k_pixel_aspect_square,
						VideoParams::k_interlace_none, 48000, layout, divider,
						default_format, default_autocache));
	add_standard_item(parent,
					std::make_shared<SequencePreset>(
						tr("%1 29.97 FPS").arg(name), width, height,
						Rational(30000, 1001), VideoParams::k_pixel_aspect_square,
						VideoParams::k_interlace_none, 48000, layout, divider,
						default_format, default_autocache));
	add_standard_item(parent,
					std::make_shared<SequencePreset>(
						tr("%1 50 FPS").arg(name), width, height,
						Rational(50, 1), VideoParams::k_pixel_aspect_square,
						VideoParams::k_interlace_none, 48000, layout, divider,
						default_format, default_autocache));
	add_standard_item(parent,
					std::make_shared<SequencePreset>(
						tr("%1 59.94 FPS").arg(name), width, height,
						Rational(60000, 1001), VideoParams::k_pixel_aspect_square,
						VideoParams::k_interlace_none, 48000, layout, divider,
						default_format, default_autocache));
	return parent;
}

QTreeWidgetItem *SequenceDialogPresetTab::create_sd_preset_folder(
	const QString &name, int width, int height, const Rational &frame_rate,
	const Rational &standard_par, const Rational &wide_par, int divider)
{
	const PixelFormat default_format = static_cast<PixelFormat::Format>(
		OAK_CONFIG("OfflinePixelFormat").toInt());
	const bool default_autocache = false;
	QTreeWidgetItem *parent = create_folder(name);
	preset_tree_->addTopLevelItem(parent);
	const uint64_t layout = k_channel_layout_stereo;
	add_standard_item(
		parent, std::make_shared<SequencePreset>(
					tr("%1 Standard").arg(name), width, height, frame_rate,
					standard_par, VideoParams::k_interlaced_bottom_first, 48000,
					layout, divider, default_format, default_autocache));
	add_standard_item(
		parent, std::make_shared<SequencePreset>(
					tr("%1 Widescreen").arg(name), width, height, frame_rate,
					wide_par, VideoParams::k_interlaced_bottom_first, 48000,
					layout, divider, default_format, default_autocache));
	return parent;
}

QTreeWidgetItem *SequenceDialogPresetTab::get_selected_item()
{
	QList<QTreeWidgetItem *> selected_items = preset_tree_->selectedItems();

	if (selected_items.isEmpty()) {
		return nullptr;
	} else {
		return selected_items.first();
	}
}

QTreeWidgetItem *SequenceDialogPresetTab::get_selected_custom_preset()
{
	QTreeWidgetItem *sel = get_selected_item();

	if (sel && sel->data(0, k_data_is_preset).toBool() &&
		sel->data(0, k_data_preset_is_custom_role).toBool()) {
		return sel;
	}

	return nullptr;
}

void SequenceDialogPresetTab::add_standard_item(QTreeWidgetItem *folder,
											  PresetPtr preset,
											  const QString &description)
{
	int index = default_preset_data_.size();
	default_preset_data_.append(preset);
	add_item_internal(folder, preset, false, index, description);
}

void SequenceDialogPresetTab::add_custom_item(QTreeWidgetItem *folder,
											PresetPtr preset, int index,
											const QString &description)
{
	add_item_internal(folder, preset, true, index, description);
}

void SequenceDialogPresetTab::add_item_internal(QTreeWidgetItem *folder,
											  PresetPtr preset, bool is_custom,
											  int index,
											  const QString &description)
{
	QTreeWidgetItem *item = new QTreeWidgetItem();

	item->setText(0, preset->get_name());
	item->setIcon(0, icon::video);
	item->setToolTip(0, description);
	item->setData(0, k_data_is_preset, true);
	item->setData(0, k_data_preset_is_custom_role, is_custom);
	item->setData(0, k_data_preset_data_role, index);

	folder->addChild(item);
}

void SequenceDialogPresetTab::selected_item_changed(QTreeWidgetItem *current,
												  QTreeWidgetItem *previous)
{
	Q_UNUSED(previous)

	if (current->data(0, k_data_is_preset).toBool()) {
		int preset_index = current->data(0, k_data_preset_data_role).toInt();

		PresetPtr preset_data =
			(current->data(0, k_data_preset_is_custom_role).toBool()) ?
				get_preset(preset_index) :
				default_preset_data_.at(preset_index);

		emit preset_changed(*static_cast<SequencePreset *>(preset_data.get()));
	}
}

void SequenceDialogPresetTab::item_double_clicked(QTreeWidgetItem *item,
												int column)
{
	Q_UNUSED(column)

	if (item->data(0, k_data_is_preset).toBool()) {
		emit preset_accepted();
	}
}

void SequenceDialogPresetTab::show_context_menu()
{
	QTreeWidgetItem *sel = get_selected_custom_preset();

	if (sel) {
		Menu m(this);

		QAction *delete_action = m.addAction(tr("Delete Preset"));
		connect(delete_action, &QAction::triggered, this,
				&SequenceDialogPresetTab::delete_selected_preset);

		m.exec(QCursor::pos());
	}
}

void SequenceDialogPresetTab::delete_selected_preset()
{
	QTreeWidgetItem *sel = get_selected_custom_preset();

	if (sel) {
		int preset_index = sel->data(0, k_data_preset_data_role).toInt();

		// Shift all items whose index was after this preset forward
		for (int i = 0; i < my_presets_folder_->childCount(); i++) {
			QTreeWidgetItem *custom_item = my_presets_folder_->child(i);
			int this_item_index =
				custom_item->data(0, k_data_preset_data_role).toInt();

			if (this_item_index > preset_index) {
				custom_item->setData(0, k_data_preset_data_role,
									 this_item_index - 1);
			}
		}

		// Remove the preset
		delete_preset(preset_index);

		// Delete the item
		delete sel;
	}
}

}
