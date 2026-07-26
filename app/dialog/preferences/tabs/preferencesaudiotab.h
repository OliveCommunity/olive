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

#ifndef OAK_PREFERENCESAUDIOTAB_H
#define OAK_PREFERENCESAUDIOTAB_H

#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>

#include "dialog/configbase/configdialogbase.h"
#include "dialog/export/exportaudiotab.h"
#include "dialog/export/exportformatcombobox.h"
#include "preferencesbehaviortab.h"

namespace olive
{

class PreferencesAudioTab : public ConfigDialogBaseTab {
	Q_OBJECT
public:
	PreferencesAudioTab();

	virtual void accept(void *command) override;

private:
	QComboBox *audio_backend_combobox_;

	/**
   * @brief UI widget for selecting the output audio device
   */
	QComboBox *audio_output_devices_;

	/**
   * @brief UI widget for selecting the input audio device
   */
	QComboBox *audio_input_devices_;

	/**
   * @brief UI widget for editing the recording channels
   */
	QComboBox *recording_combobox_;

	/**
   * @brief Button that triggers a refresh of the available audio devices
   */
	QPushButton *refresh_devices_btn_;

	SampleRateComboBox *output_rate_combo_;
	ChannelLayoutComboBox *output_ch_layout_combo_;
	SampleFormatComboBox *output_fmt_combo_;

	/**
   * @brief UI widget for the output buffer size in frames (0 = auto)
   */
	QSpinBox *output_buffer_size_;

	ExportFormatComboBox *record_format_combo_;

	ExportAudioTab *record_options_;

	QCheckBox *audio_scrubbing_;

private slots:
	void refresh_backends();

	void refresh_devices();

	void hard_refresh_backends();

	void attempt_to_set_devices_from_config();
};

}

#endif // OAK_PREFERENCESAUDIOTAB_H
