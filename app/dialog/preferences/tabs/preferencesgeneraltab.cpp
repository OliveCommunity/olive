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

#include "preferencesgeneraltab.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>

#include "common/autoscroll.h"
#include "core.h"
#include "preferencesbehaviortab.h"

namespace olive
{

PreferencesGeneralTab::PreferencesGeneralTab()
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	{
		QGroupBox *global_groupbox = new QGroupBox(tr("Locale"));
		QGridLayout *global_layout = new QGridLayout(global_groupbox);
		layout->addWidget(global_groupbox);

		int row = 0;

		// General -> Language
		global_layout->addWidget(new QLabel(tr("Language:")), row, 0);

		language_combobox_ = new QComboBox();

		// Add default language (en-US)
		QDir language_dir(QStringLiteral(":/ts"));
		QStringList languages = language_dir.entryList();
		foreach (const QString &l, languages) {
			add_language(l);
		}

		QString current_language = OAK_CONFIG("Language").toString();
		if (current_language.isEmpty()) {
			// No configured language, use system language
			current_language = QLocale::system().name();

			// If we don't have a language for this, default to en_US
			if (!languages.contains(current_language)) {
				current_language = QStringLiteral("en_US");
			}
		}
		language_combobox_->setCurrentIndex(
			languages.indexOf(current_language));

		global_layout->addWidget(language_combobox_, row, 1);
	}

	{
		QGroupBox *timeline_groupbox = new QGroupBox(tr("Timeline"));
		QGridLayout *timeline_layout = new QGridLayout(timeline_groupbox);
		layout->addWidget(timeline_groupbox);

		int row = 0;

		QLabel *autoscroll_lbl = new QLabel(tr("Auto-Scroll Method:"));
		autoscroll_lbl->setSizePolicy(QSizePolicy::Expanding,
									  QSizePolicy::Expanding);
		timeline_layout->addWidget(autoscroll_lbl, row, 0);

		// ComboBox indices match enum indices
		autoscroll_method_ = new QComboBox();
		autoscroll_method_->addItem(tr("None"), AutoScroll::k_none);
		autoscroll_method_->addItem(tr("Page Scrolling"), AutoScroll::k_page);
		autoscroll_method_->addItem(tr("Smooth Scrolling"),
									AutoScroll::k_smooth);
		autoscroll_method_->setCurrentIndex(OAK_CONFIG("Autoscroll").toInt());
		timeline_layout->addWidget(autoscroll_method_, row, 1);

		row++;

		timeline_layout->addWidget(new QLabel(tr("Rectified Waveforms:")), row,
								   0);

		rectified_waveforms_ = new QCheckBox();
		rectified_waveforms_->setChecked(
			OAK_CONFIG("RectifiedWaveforms").toBool());
		timeline_layout->addWidget(rectified_waveforms_, row, 1);

		row++;

		timeline_layout->addWidget(
			new QLabel(tr("Default Still Image Length:")), row, 0);

		default_still_length_ = new RationalSlider();
		default_still_length_->set_minimum(Rational(100, 1000));
		default_still_length_->set_timebase(Rational(100, 1000));
		default_still_length_->set_format(tr("%1 seconds"));
		default_still_length_->set_value(
			OAK_CONFIG("DefaultStillLength").value<Rational>());
		timeline_layout->addWidget(default_still_length_);
	}

	{
		QGroupBox *autorecovery_groupbox = new QGroupBox(tr("Auto-Recovery"));
		QGridLayout *autorecovery_layout =
			new QGridLayout(autorecovery_groupbox);
		layout->addWidget(autorecovery_groupbox);

		int row = 0;

		autorecovery_layout->addWidget(new QLabel(tr("Enable Auto-Recovery:")),
									   row, 0);

		autorecovery_enabled_ = new QCheckBox();
		autorecovery_enabled_->setChecked(
			OAK_CONFIG("AutorecoveryEnabled").toBool());
		autorecovery_layout->addWidget(autorecovery_enabled_, row, 1);

		row++;

		autorecovery_layout->addWidget(
			new QLabel(tr("Auto-Recovery Interval:")), row, 0);

		autorecovery_interval_ = new IntegerSlider();
		autorecovery_interval_->set_minimum(1);
		autorecovery_interval_->set_maximum(60);
		autorecovery_interval_->set_format(
			QT_TRANSLATE_N_NOOP("olive::SliderBase", "%n minute(s)"), true);
		autorecovery_interval_->set_value(
			OAK_CONFIG("AutorecoveryInterval").toLongLong());
		autorecovery_layout->addWidget(autorecovery_interval_, row, 1);

		row++;

		autorecovery_layout->addWidget(
			new QLabel(tr("Maximum Versions Per Project:")), row, 0);

		autorecovery_maximum_ = new IntegerSlider();
		autorecovery_maximum_->set_minimum(1);
		autorecovery_maximum_->set_maximum(1000);
		autorecovery_maximum_->set_value(
			OAK_CONFIG("AutorecoveryMaximum").toLongLong());
		autorecovery_layout->addWidget(autorecovery_maximum_, row, 1);

		row++;

		QPushButton *browse_autorecoveries =
			new QPushButton(tr("Browse Auto-Recoveries"));
		connect(browse_autorecoveries, &QPushButton::clicked, Core::instance(),
				&Core::browse_auto_recoveries);
		autorecovery_layout->addWidget(browse_autorecoveries, row, 1);
	}

	{
		QGroupBox *behavior_groupbox =
			new QGroupBox(PreferencesBehaviorTab::behavior_pref_tr("Behavior"));
		QVBoxLayout *behavior_layout = new QVBoxLayout(behavior_groupbox);
		layout->addWidget(behavior_groupbox);

		hover_focus_ = new QCheckBox(
			PreferencesBehaviorTab::behavior_pref_tr("Enable hover focus"));
		hover_focus_->setToolTip(PreferencesBehaviorTab::behavior_pref_tr(
			"Panels will be considered focused when the mouse cursor is over them without having to click them."));
		hover_focus_->setChecked(OAK_CONFIG("HoverFocus").toBool());
		behavior_layout->addWidget(hover_focus_);

		slider_ladder_ = new QCheckBox(
			PreferencesBehaviorTab::behavior_pref_tr("Enable slider ladder"));
		slider_ladder_->setChecked(OAK_CONFIG("UseSliderLadders").toBool());
		behavior_layout->addWidget(slider_ladder_);

		scroll_zooms_ = new QCheckBox(PreferencesBehaviorTab::behavior_pref_tr(
			"Scrolling zooms by default"));
		scroll_zooms_->setToolTip(PreferencesBehaviorTab::behavior_pref_tr(
			"By default, scrolling will move the view around, and holding Ctrl/Cmd will make it zoom instead. "
			"Enabling this will switch those, scrolling will zoom by default, and holding Ctrl/Cmd will move the view instead."));
		scroll_zooms_->setChecked(OAK_CONFIG("ScrollZooms").toBool());
		behavior_layout->addWidget(scroll_zooms_);
	}

	layout->addStretch();
}

void PreferencesGeneralTab::accept(void *command)
{
	Q_UNUSED(command)

	OAK_CONFIG("RectifiedWaveforms") = rectified_waveforms_->isChecked();

	OAK_CONFIG("Autoscroll") = autoscroll_method_->currentData();

	OAK_CONFIG("DefaultStillLength") =
		QVariant::fromValue(default_still_length_->get_value());

	QString set_language = language_combobox_->currentData().toString();
	if (QLocale::system().name() == set_language) {
		// Language is set to the system, assume this is effectively "auto"
		set_language = QString();
	}

	// If the language has changed, set it now
	if (OAK_CONFIG("Language").toString() != set_language) {
		OAK_CONFIG("Language") = set_language;
		Core::instance()->set_language(
			set_language.isEmpty() ? QLocale::system().name() : set_language);
	}

	OAK_CONFIG("AutorecoveryEnabled") = autorecovery_enabled_->isChecked();
	OAK_CONFIG("AutorecoveryInterval") =
		QVariant::fromValue(autorecovery_interval_->get_value());
	OAK_CONFIG("AutorecoveryMaximum") =
		QVariant::fromValue(autorecovery_maximum_->get_value());
	Core::instance()->set_autorecovery_interval(
		autorecovery_interval_->get_value());

	OAK_CONFIG("HoverFocus") = hover_focus_->isChecked();
	OAK_CONFIG("UseSliderLadders") = slider_ladder_->isChecked();
	OAK_CONFIG("ScrollZooms") = scroll_zooms_->isChecked();
}

void PreferencesGeneralTab::add_language(const QString &locale_name)
{
	language_combobox_->addItem(tr("%1 (%2)").arg(
		QLocale(locale_name).nativeLanguageName(), locale_name));
	;
	language_combobox_->setItemData(language_combobox_->count() - 1,
									locale_name);
}

}
