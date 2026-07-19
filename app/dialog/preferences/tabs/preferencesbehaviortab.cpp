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

#include "preferencesbehaviortab.h"

#include <QLabel>

#include "config/config.h"

namespace olive
{

PreferencesBehaviorTab::PreferencesBehaviorTab(Category category)
	: category_(category)
	, graphics_backend_combobox_(nullptr)
{
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setAlignment(Qt::AlignTop);

	switch (category_) {
	case k_category_timeline:
		add_items({
			{ tr("Auto-Seek to Imported Clips"),
			  QStringLiteral("EnableSeekToImport") },
			{ tr("Edit Tool Also Seeks"), QStringLiteral("EditToolAlsoSeeks") },
			{ tr("Edit Tool Selects Links"),
			  QStringLiteral("EditToolSelectsLinks") },
			{ tr("Enable Drag Files to Timeline"),
			  QStringLiteral("EnableDragFilesToTimeline") },
			{ tr("Invert Timeline Scroll Axes"),
			  QStringLiteral("InvertTimelineScrollAxes"),
			  tr("Hold ALT on any UI element to switch scrolling axes") },
			{ tr("Seek Also Selects"), QStringLiteral("SeekAlsoSelects") },
			{ tr("Seek to the End of Pastes"), QStringLiteral("PasteSeeks") },
			{ tr("Selecting Also Seeks"), QStringLiteral("SelectAlsoSeeks") },
		});
		break;

	case k_category_playback:
		add_items({
			{ tr("Ask For Name When Setting Marker"),
			  QStringLiteral("SetNameWithMarker") },
			{ tr("Automatically rewind at the end of a sequence"),
			  QStringLiteral("AutoSeekToBeginning") },
		});
		break;

	case k_category_project:
		add_item(tr("Drop Files on Media to Replace"),
				QStringLiteral("DropFileOnMediaToReplace"));
		break;

	case k_category_nodes:
		add_items({
			{ tr("Add Default Effects to New Clips"),
			  QStringLiteral("AddDefaultEffectsToClips") },
			{ tr("Auto-Scale By Default"),
			  QStringLiteral("AutoscaleByDefault") },
			{ tr("Splitting Clips Copies Dependencies"),
			  QStringLiteral("SplitClipsCopyNodes"),
			  tr("Multiple clips can share the same nodes. Disable this to "
				 "automatically share node dependencies among clips when copying "
				 "or splitting them.") },
		});
		break;

	case k_category_rendering: {
		QLabel *backend_label = new QLabel(tr("Graphics Backend"));
		backend_label->setToolTip(
			tr("Selects the graphics API Oak should request on next launch. "
			   "Vulkan is experimental: on most systems it will fall back to "
			   "OpenGL or use a prototype Vulkan path that is not yet fully "
			   "validated."));

		graphics_backend_combobox_ = new QComboBox();
		graphics_backend_combobox_->addItem(tr("OpenGL"),
											QStringLiteral("opengl"));
		graphics_backend_combobox_->addItem(tr("Vulkan (experimental)"),
											QStringLiteral("vulkan"));
		const QString current_backend =
			OAK_CONFIG("GraphicsBackend").toString().toLower();
		const int backend_index = graphics_backend_combobox_->findData(
			current_backend.isEmpty() ? QStringLiteral("opengl") :
										current_backend);
		graphics_backend_combobox_->setCurrentIndex(
			backend_index >= 0 ? backend_index : 0);

		QHBoxLayout *backend_layout = new QHBoxLayout();
		backend_layout->addWidget(backend_label);
		backend_layout->addWidget(graphics_backend_combobox_, 1);
		layout->addLayout(backend_layout);

		add_item(tr("Use glFinish"), QStringLiteral("UseGLFinish"));
		break;
	}
	}
}

void PreferencesBehaviorTab::accept(MultiUndoCommand *command)
{
	Q_UNUSED(command)

	for (auto it = config_map_.cbegin(); it != config_map_.cend(); ++it) {
		OAK_CONFIG_STR(it.value()) = it.key()->isChecked();
	}

	if (graphics_backend_combobox_) {
		OAK_CONFIG("GraphicsBackend") =
			graphics_backend_combobox_->currentData().toString();
	}
}

void PreferencesBehaviorTab::add_items(const QVector<Item> &items)
{
	for (const Item &i : items) {
		add_item(i.text, i.config_key, i.tooltip);
	}
}

QCheckBox *PreferencesBehaviorTab::add_item(const QString &text,
										   const QString &config_key,
										   const QString &tooltip)
{
	QCheckBox *checkbox = new QCheckBox(text);
	checkbox->setToolTip(tooltip);
	checkbox->setChecked(OAK_CONFIG_STR(config_key).toBool());

	config_map_.insert(checkbox, config_key);

	layout()->addWidget(checkbox);

	return checkbox;
}

}
