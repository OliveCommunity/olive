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

#include "preferences.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QSplitter>
#include <QVBoxLayout>

#include "oakengine/config.h"
#include "tabs/preferencesgeneraltab.h"
#include "tabs/preferencesbehaviortab.h"
#include "tabs/preferencesappearancetab.h"
#include "tabs/preferencesdisktab.h"
#include "tabs/preferencesaudiotab.h"
#include "tabs/preferenceskeyboardtab.h"
#include "tabs/preferencesluttab.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

PreferencesDialog::PreferencesDialog(MainWindow *main_window, int start_tab)
	: ConfigDialogBase(main_window)
{
	setWindowTitle(tr("Preferences"));

	add_tab(new PreferencesGeneralTab(), tr("General"));
	add_tab(new PreferencesAppearanceTab(), tr("Appearance"));
	add_tab(new PreferencesAudioTab(), tr("Audio"));
	add_tab(
		new PreferencesBehaviorTab(PreferencesBehaviorTab::k_category_timeline),
		tr("Timeline"));
	add_tab(
		new PreferencesBehaviorTab(PreferencesBehaviorTab::k_category_playback),
		tr("Playback"));
	add_tab(new PreferencesBehaviorTab(PreferencesBehaviorTab::k_category_project),
		   tr("Project"));
	add_tab(new PreferencesBehaviorTab(PreferencesBehaviorTab::k_category_nodes),
		   tr("Nodes"));
	add_tab(
		new PreferencesBehaviorTab(PreferencesBehaviorTab::k_category_rendering),
		tr("Rendering"));
	add_tab(new PreferencesDiskTab(), tr("Disk"));
	add_tab(new PreferencesLutTab(), tr("LUT"));
	add_tab(new PreferencesKeyboardTab(main_window), tr("Keyboard"));

	set_current_tab(start_tab);
}

void PreferencesDialog::AcceptEvent()
{
	oakengine_config_save();
}

}
