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

#ifndef OAK_EXPORTDIALOG_H
#define OAK_EXPORTDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <cstdint>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QProgressBar>

#include "dialog/export/exportformatcombobox.h"
#include "exportaudiotab.h"
#include "exportsubtitlestab.h"
#include "exportvideotab.h"
#include "oakengine/encoding.h"
#include "widget/nodeparamview/nodeparamviewwidgetbridge.h"
#include "widget/viewer/viewer.h"

namespace olive
{

class ExportDialog : public QDialog {
	Q_OBJECT
public:
	ExportDialog(OakEngineNode *viewer_node, bool stills_only_mode,
				 QWidget *parent = nullptr);
	ExportDialog(OakEngineNode *viewer_node, QWidget *parent = nullptr)
		: ExportDialog(viewer_node, false, parent)
	{
	}

	Rational get_selected_timebase() const;
	void set_selected_timebase(const Rational &r);

	OakEngineEncodingParams *generate_params() const;
	void set_params(const OakEngineEncodingParams *e);

	virtual bool eventFilter(QObject *o, QEvent *e) override;

public slots:
	virtual void done(int r) override;

signals:
	void request_import_file(const QString &s);

private:
	void add_preferences_tab(QWidget *inner_widget, const QString &title);

	void load_presets();
	void set_default_filename();

	bool sequence_has_subtitles() const;

	void set_defaults();

	OakEngineNode *viewer_node_;

	int64_t viewer_sub_ = 0;

	int previously_selected_format_;

	Rational get_export_length() const;
	int64_t get_export_length_in_timebase_units() const;

	enum RangeSelection { k_range_entire_sequence, k_range_in_to_out };

	enum AutoPreset {
		k_preset_default = -1,
		k_preset_last_used = -2,
	};

	QTabWidget *preferences_tabs_;

	QComboBox *preset_combobox_;
	QComboBox *range_combobox_;
	std::vector<OakEngineEncodingParams *> presets_;

	QCheckBox *video_enabled_;
	QCheckBox *audio_enabled_;
	QCheckBox *subtitles_enabled_;

	ViewerWidget *preview_viewer_;
	QLineEdit *filename_edit_;
	ExportFormatComboBox *format_combobox_;

	ExportVideoTab *video_tab_;
	ExportAudioTab *audio_tab_;
	ExportSubtitlesTab *subtitle_tab_;

	double video_aspect_ratio_;

	OakEngineColorManager *color_manager_;

	QWidget *preferences_area_;
	QCheckBox *export_bkg_box_;
	QCheckBox *import_file_after_export_;

	bool stills_only_mode_;

	bool loading_presets_;

private slots:
	void browse_filename();

	void format_changed(int current_format);

	void resolution_changed();

	void update_viewer_dimensions();

	void start_export();

	void export_finished();

	void image_sequence_check_box_changed(bool e);

	void save_preset();

	void preset_combo_box_changed();
};

}

#endif // OAK_EXPORTDIALOG_H
