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

#include "export.h"

#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStandardPaths>

#include "common/digit.h"
#include "common/qtutils.h"
#include "codec/exportcodec.h"
#include "codec/exportformat.h"
#include "dialog/msgbox.h"
#include "dialog/task/task.h"
#include "exportsavepresetdialog.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/events.h"
#include "widget/manageddisplay/colorprocessorhandle.h"
#include "widget/viewer/vieweroutpututils.h"
#include "oakengine/exporter.h"
#include "oakengine/project.h"
#include "oakengine/task.h"
#include "oakengine/encoding.h"
#include "oakengine/viewer.h"
#include "ui/icons/icons.h"
#include "widget/timeruler/timeruler.h"
#include "common/configwrapper.h"

namespace olive
{

#define super QDialog

namespace
{

// pix_fmt string (e.g. "yuv420p") to its index in the codec's supported
// list; 0 (the codec's preferred format) when absent.
int pix_fmt_index(int codec, const QString &pix_fmt)
{
	if (pix_fmt.isEmpty()) {
		return 0;
	}
	return oakengine_encoding_pix_fmt_index(codec, pix_fmt.toUtf8().constData());
}

// OakEngineEncodingParams (assembled by the dialog) -> facade POD. One-to-one with
// oak_export_options_ex; see oakengine/exporter.h for the field docs.
oak_export_options_ex params_to_ex(const OakEngineEncodingParams *p)
{
	oak_export_options_ex o = {};

	int64_t vbrate = 0, abrate = 0;
	int asample_rate = 0;
	uint64_t ach_layout = 0;
	int asample_fmt = 0;
	int vthreads = 0;
	int scaling = 0;
	int is_img_seq = 0;

	oak_video_params vp = {};
	oakengine_encoding_params_get_video_params(p, &vp);

	vbrate = oakengine_encoding_params_video_bit_rate(p);
	abrate = oakengine_encoding_params_audio_bit_rate(p);
	vthreads = oakengine_encoding_params_video_threads(p);
	scaling = oakengine_encoding_params_video_scaling_method(p);
	is_img_seq = oakengine_encoding_params_video_is_image_sequence(p);

	if (oakengine_encoding_params_has_custom_range(p)) {
		o.range_mode = OAKENGINE_EXPORT_RANGE_CUSTOM;
		int64_t r_in_num = 0, r_in_den = 1, r_out_num = 0, r_out_den = 1;
		oakengine_encoding_params_get_custom_range(
			p, &r_in_num, &r_in_den, &r_out_num, &r_out_den);
		o.range_in_ts =
			Timecode::time_to_timestamp(
				Rational(r_in_num, r_in_den),
				Rational(vp.time_base_num, vp.time_base_den));
		o.range_out_ts =
			Timecode::time_to_timestamp(
				Rational(r_out_num, r_out_den),
				Rational(vp.time_base_num, vp.time_base_den));
	} else {
		o.range_mode = OAKENGINE_EXPORT_RANGE_ENTIRE;
	}

	o.format = oakengine_encoding_params_format(p);
	o.video_enabled = oakengine_encoding_params_video_enabled(p) ? 1 : 0;
	o.video_codec = oakengine_encoding_params_video_codec(p);
	o.audio_enabled = oakengine_encoding_params_audio_enabled(p) ? 1 : 0;
	o.audio_codec = oakengine_encoding_params_audio_codec(p);
	o.subtitles_enabled = oakengine_encoding_params_subtitles_enabled(p) ? 1 : 0;
	o.subtitles_sidecar = oakengine_encoding_params_subtitles_are_sidecar(p) ? 1 : 0;
	o.subtitles_format =
		oakengine_encoding_params_subtitles_are_sidecar(p)
			? oakengine_encoding_params_subtitles_sidecar_format(p)
			: 0;
	o.subtitles_codec = oakengine_encoding_params_subtitles_enabled(p)
							? oakengine_encoding_params_subtitles_codec(p)
							: 0;

	o.video_bit_rate = vbrate;
	o.audio_bit_rate = abrate;

	char pix_fmt_buf[64];
	if (oakengine_encoding_params_video_pix_fmt(
			p, pix_fmt_buf, static_cast<int>(sizeof(pix_fmt_buf))) > 0) {
		o.video_pix_fmt = oakengine_encoding_pix_fmt_index(
			o.video_codec, pix_fmt_buf);
	} else {
		o.video_pix_fmt = 0;
	}

	if (oakengine_encoding_params_get_audio_params(
			p, &asample_rate, &ach_layout, &asample_fmt) == OAKENGINE_OK) {
		o.audio_sample_rate = asample_rate;
		o.audio_channel_layout = ach_layout;
		o.audio_sample_format = asample_fmt;
	}

	char ct_buf[128];
	const int ct_ret = oakengine_encoding_params_color_transform_output(
		p, ct_buf, static_cast<int>(sizeof(ct_buf)));
	if (ct_ret <= 0 || ct_buf[0] == '\0') {
		o.color_transform = OAKENGINE_EXPORT_COLOR_REFERENCE;
	} else {
		const QString ct = QString::fromUtf8(ct_buf);
		if (ct == QStringLiteral("sRGB OETF")) {
			o.color_transform = OAKENGINE_EXPORT_COLOR_SRGB_OETF;
		} else if (ct == QStringLiteral("Rec.709 OETF")) {
			o.color_transform = OAKENGINE_EXPORT_COLOR_REC709_OETF;
		} else if (ct == QStringLiteral("BT.1886 EOTF")) {
			o.color_transform = OAKENGINE_EXPORT_COLOR_BT1886_EOTF;
		} else {
			o.color_transform = OAKENGINE_EXPORT_COLOR_CUSTOM;
			snprintf(o.color_transform_name, sizeof(o.color_transform_name),
					 "%s", ct_buf);
		}
	}

	o.video_width = vp.width;
	o.video_height = vp.height;
	o.frame_rate_num = vp.time_base_den; // time_base is frame duration, so rate = den/num
	o.frame_rate_den = vp.time_base_num;
	o.pixel_aspect_num = vp.pixel_aspect_num;
	o.pixel_aspect_den = vp.pixel_aspect_den;
	o.interlacing = vp.interlacing;
	o.pixel_format = vp.format;
	o.scaling_method = scaling;
	o.color_range = vp.color_range;
	o.video_threads = vthreads;
	o.is_image_sequence = is_img_seq;

	return o;
}

} // namespace

ExportDialog::ExportDialog(ViewerOutput *viewer_node, bool stills_only_mode,
						   QWidget *parent)
	: super(parent)
	, viewer_node_(viewer_node)
	, stills_only_mode_(stills_only_mode)
	, loading_presets_(false)
{
	QHBoxLayout *layout = new QHBoxLayout(this);

	QSplitter *splitter = new QSplitter(Qt::Horizontal);
	splitter->setChildrenCollapsible(false);
	layout->addWidget(splitter);

	preferences_area_ = new QWidget();
	QGridLayout *preferences_layout = new QGridLayout(preferences_area_);
	preferences_layout->setContentsMargins(0, 0, 0, 0);

	int row = 0;

	QLabel *fn_lbl = new QLabel(tr("Filename:"));
	fn_lbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	preferences_layout->addWidget(fn_lbl, row, 0);

	filename_edit_ = new QLineEdit();
	preferences_layout->addWidget(filename_edit_, row, 1, 1, 2);

	QPushButton *file_browse_btn = new QPushButton();
	file_browse_btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	file_browse_btn->setIcon(icon::folder);
	file_browse_btn->setToolTip(tr("Browse for exported file filename"));
	connect(file_browse_btn, &QPushButton::clicked, this,
			&ExportDialog::browse_filename);
	preferences_layout->addWidget(file_browse_btn, row, 3);

	row++;

	QLabel *preset_lbl = new QLabel(tr("Preset:"));
	preset_lbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	preferences_layout->addWidget(preset_lbl, row, 0);
	preset_combobox_ = new QComboBox();
	load_presets();
	connect(
		preset_combobox_,
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &ExportDialog::preset_combo_box_changed);
	preferences_layout->addWidget(preset_combobox_, row, 1, 1, 2);

	/*QPushButton* preset_load_btn = new QPushButton();
  preset_load_btn->setIcon(icon::Open);
  preset_load_btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
  preferences_layout->addWidget(preset_load_btn, row, 2);*/

	QPushButton *preset_save_btn = new QPushButton();
	preset_save_btn->setIcon(icon::save);
	preset_save_btn->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	preferences_layout->addWidget(preset_save_btn, row, 3);
	connect(preset_save_btn, &QPushButton::clicked, this,
			&ExportDialog::save_preset);

	row++;

	preferences_layout->addWidget(QtUtils::create_horizontal_line(), row, 0, 1,
								  4);

	row++;

	preferences_layout->addWidget(new QLabel(tr("Range:")), row, 0);

	range_combobox_ = new QComboBox();
	range_combobox_->addItem(tr("Entire Sequence"));
	range_combobox_->addItem(tr("In to Out"));
	range_combobox_->setEnabled(viewer_node_->get_work_area()->enabled());

	preferences_layout->addWidget(range_combobox_, row, 1, 1, 3);

	row++;

	preferences_layout->addWidget(QtUtils::create_horizontal_line(), row, 0, 1,
								  4);

	row++;

	preferences_layout->addWidget(new QLabel(tr("Format:")), row, 0);
	format_combobox_ = new ExportFormatComboBox();
	preferences_layout->addWidget(format_combobox_, row, 1, 1, 3);

	row++;

	QHBoxLayout *av_enabled_layout = new QHBoxLayout();

	video_enabled_ = new QCheckBox(tr("Export Video"));
	av_enabled_layout->addWidget(video_enabled_);

	audio_enabled_ = new QCheckBox(tr("Export Audio"));
	av_enabled_layout->addWidget(audio_enabled_);

	subtitles_enabled_ = new QCheckBox(tr("Export Subtitles"));
	av_enabled_layout->addWidget(subtitles_enabled_);

	preferences_layout->addLayout(av_enabled_layout, row, 0, 1, 4);

	row++;

	preferences_tabs_ = new QTabWidget();

	color_manager_ = oak_color_manager(viewer_node_->project()->color_manager());
	video_tab_ = new ExportVideoTab(color_manager_);
	add_preferences_tab(video_tab_, tr("Video"));

	// Set video tab time and make connections
	viewer_sub_ = oakengine_event_subscribe(
		reinterpret_cast<OakEngineNode *>(viewer_node),
		OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED,
		[](const oakengine_event *event, void *userdata) {
			auto *dlg = static_cast<ExportDialog *>(userdata);
			auto *tab = dlg->video_tab_;
			tab->set_time(Rational(event->a, event->b));
		},
		this);
	connect(video_tab_, &ExportVideoTab::time_changed, this,
			[viewer_node](const Rational &time) {
				oakengine_viewer_set_playhead(
					reinterpret_cast<OakEngineNode *>(viewer_node),
					time.numerator(), time.denominator());
			});
	{
		int64_t pn, pd;
		oakengine_viewer_get_playhead(
			reinterpret_cast<OakEngineNode *>(viewer_node), &pn, &pd);
		video_tab_->set_time(Rational(pn, pd));
	}

	audio_tab_ = new ExportAudioTab();
	add_preferences_tab(audio_tab_, tr("Audio"));

	subtitle_tab_ = new ExportSubtitlesTab();
	add_preferences_tab(subtitle_tab_, tr("Subtitles"));

	preferences_layout->addWidget(preferences_tabs_, row, 0, 1, 4);

	row++;

	{
		QGroupBox *options_group = new QGroupBox();
		preferences_layout->addWidget(options_group, row, 0, 1, 4);

		QGridLayout *options_layout = new QGridLayout(options_group);

		int opt_row = 0;

		export_bkg_box_ = new QCheckBox(tr("Run In Background"));
		export_bkg_box_->setToolTip(tr(
			"Exporting in the background allows you to continue using Oak Video Editor while "
			"exporting, but may result in slower export speeds, and may"
			"severely impact editing and playback performance."));
		options_layout->addWidget(export_bkg_box_, opt_row, 0);

		import_file_after_export_ =
			new QCheckBox(tr("Import Result After Export"));
		options_layout->addWidget(import_file_after_export_, opt_row, 1);

		connect(export_bkg_box_, &QCheckBox::toggled, import_file_after_export_,
				[this](bool e) { import_file_after_export_->setEnabled(!e); });
	}

	row++;

	QHBoxLayout *btn_layout = new QHBoxLayout();
	btn_layout->setContentsMargins(0, 0, 0, 0);
	preferences_layout->addLayout(btn_layout, row, 0, 1, 4);

	btn_layout->addStretch();

	QPushButton *export_btn = new QPushButton(tr("Export"));
	btn_layout->addWidget(export_btn);
	connect(export_btn, &QPushButton::clicked, this,
			&ExportDialog::start_export);

	QPushButton *cancel_btn = new QPushButton(tr("Cancel"));
	btn_layout->addWidget(cancel_btn);
	connect(cancel_btn, &QPushButton::clicked, this, &ExportDialog::reject);

	btn_layout->addStretch();

	splitter->addWidget(preferences_area_);

	QWidget *preview_area = new QWidget();
	QVBoxLayout *preview_layout = new QVBoxLayout(preview_area);
	preview_layout->addWidget(new QLabel(tr("Preview")));
	preview_viewer_ = new ViewerWidget();
	preview_viewer_->ruler()->set_marker_editing_enabled(false);
	preview_viewer_->setSizePolicy(QSizePolicy::Expanding,
								   QSizePolicy::Expanding);
	preview_layout->addWidget(preview_viewer_);
	splitter->addWidget(preview_area);

	// Prioritize preview area
	splitter->setSizes({ 1, 99999 });

	// Set default filename
	set_default_filename();

	// Set defaults
	previously_selected_format_ = OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO;
	connect(format_combobox_, &ExportFormatComboBox::format_changed, this,
			&ExportDialog::format_changed);

	VideoParams vp = viewer_output_video_params(viewer_node_);
	video_aspect_ratio_ =
		static_cast<double>(vp.width()) / static_cast<double>(vp.height());

	connect(video_tab_->width_slider(), &IntegerSlider::value_changed, this,
			&ExportDialog::resolution_changed);

	connect(video_tab_->height_slider(), &IntegerSlider::value_changed, this,
			&ExportDialog::resolution_changed);

	connect(
		video_tab_->scaling_method_combobox(),
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		this, &ExportDialog::update_viewer_dimensions);

	connect(video_tab_->maintain_aspect_checkbox(), &QCheckBox::toggled, this,
			&ExportDialog::resolution_changed);

	connect(video_tab_, &ExportVideoTab::color_space_changed, preview_viewer_,
			static_cast<void (ViewerWidget::*)(const ColorTransform &)>(
				&ViewerWidget::set_color_transform));
	connect(video_tab_, &ExportVideoTab::image_sequence_check_box_changed, this,
			&ExportDialog::image_sequence_check_box_changed);

	// We don't check if the codec supports subtitles because we can always export to a sidecar file
	bool has_subtitle_tracks = sequence_has_subtitles();
	connect(subtitles_enabled_, &QCheckBox::toggled, subtitle_tab_,
			&QWidget::setEnabled);
	subtitles_enabled_->setEnabled(has_subtitle_tracks);

	// If the viewer already has cached params, use them
	if (!stills_only_mode_ &&
		oakengine_encoding_params_get_last_used(
			reinterpret_cast<OakEngineSequence *>(viewer_node_)) != nullptr) {
		// This will automatically set the param data
		QtUtils::set_combo_box_data(preset_combobox_, k_preset_last_used);
	} else {
		set_defaults();
	}

	// Set viewer to view the node and set its colorspace
	preview_viewer_->connect_viewer_node(viewer_node_);
	preview_viewer_->set_color_menu_enabled(false);
	preview_viewer_->set_color_transform(video_tab_->current_ocio_color_space());

	qApp->installEventFilter(this);

	connect(video_enabled_, &QCheckBox::toggled, video_tab_,
			&QWidget::setEnabled);
	video_tab_->setEnabled(video_enabled_->isChecked());
	connect(audio_enabled_, &QCheckBox::toggled, audio_tab_,
			&QWidget::setEnabled);
	audio_tab_->setEnabled(audio_enabled_->isChecked());
	connect(subtitles_enabled_, &QCheckBox::toggled, subtitle_tab_,
			&QWidget::setEnabled);
	subtitle_tab_->setEnabled(subtitles_enabled_->isChecked());
}

Rational ExportDialog::get_selected_timebase() const
{
	return video_tab_->get_selected_frame_rate().flipped();
}

void ExportDialog::set_selected_timebase(const Rational &r)
{
	video_tab_->set_selected_frame_rate(r.flipped());
}

void ExportDialog::start_export()
{
	if (!video_enabled_->isChecked() && !audio_enabled_->isChecked() &&
		!subtitles_enabled_->isChecked()) {
		msg_box(
			this, QMessageBox::Critical, tr("Invalid parameters"),
			tr("Video, audio, and subtitles are disabled. There's nothing to export."));
		return;
	}

	// Validate if the entered filename contains the correct extension (the extension is necessary
	// for both FFmpeg and OIIO to determine the output format)
char ext_buf[64];
	int ext_len = oakengine_encoding_format_extension(format_combobox_->get_format(), ext_buf, sizeof(ext_buf));
	QString necessary_ext = QStringLiteral(".%1").arg(QString::fromUtf8(ext_buf, ext_len));
	QString proposed_filename = filename_edit_->text().trimmed();

	// If it doesn't, see if the user wants to append it automatically. If not, we don't abort the export.
	if (!proposed_filename.endsWith(necessary_ext, Qt::CaseInsensitive)) {
		if (msg_box(
				this, QMessageBox::Warning, tr("Invalid filename"),
				tr("The filename must contain the extension \"%1\". Would you like to append it "
				   "automatically?")
					.arg(necessary_ext),
				QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
			filename_edit_->setText(proposed_filename.append(necessary_ext));
		} else {
			return;
		}
	}

	// Validate the intended path
	QFileInfo file_info(proposed_filename);
	QFileInfo dir_info(file_info.path());

	// If the directory does not exist, try to create it
	QDir dest_dir(file_info.path());
	if (!FileFunctions::directory_is_valid(dest_dir)) {
		msg_box(
			this, QMessageBox::Critical,
			tr("Failed to create output directory"),
			tr("The intended output directory doesn't exist and Oak Video Editor couldn't create it. "
			   "Please choose a different filename."));
		return;
	}

	// Validate if this is an image sequence and if the filename contains enough digits
	if (video_tab_->is_image_sequence_set()) {
		// Ensure filename contains digits
		if (!oakengine_encoding_filename_contains_digit_placeholder(proposed_filename.toUtf8().constData())) {
			msg_box(
				this, QMessageBox::Critical, tr("Invalid filename"),
				tr("Export is set to an image sequence, but the filename does not have a section for digits "
				   "(formatted as [#####] where the amount of # is the amount of digits)."));
			return;
		}

		int64_t frame_count = get_export_length_in_timebase_units();
		int64_t needed_digit_count = get_digit_count(frame_count);
		int current_digit_count =
			oakengine_encoding_image_sequence_digit_count(proposed_filename.toUtf8().constData());
		if (current_digit_count < needed_digit_count) {
			msg_box(
				this, QMessageBox::Critical, tr("Invalid filename"),
				tr("Filename doesn't contain enough digits for the amount of frames "
				   "this export will need (need %1 for %n frame(s)).",
				   nullptr, frame_count)
					.arg(QString::number(needed_digit_count)));
			return;
		}
	}

	// Validate if the file exists and whether the user wishes to overwrite it
	if (file_info.exists()) {
		if (msg_box(
				this, QMessageBox::Warning, tr("Confirm Overwrite"),
				tr("The file \"%1\" already exists. Do you want to overwrite it?")
					.arg(proposed_filename),
				QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
			return;
		}
	}

	// Validate video resolution
	if (video_enabled_->isChecked() &&
		(video_tab_->get_selected_codec() == OAKENGINE_ENCODING_CODEC_H264 ||
		 video_tab_->get_selected_codec() == OAKENGINE_ENCODING_CODEC_H265) &&
		(video_tab_->width_slider()->get_value() % 2 != 0 ||
		 video_tab_->height_slider()->get_value() % 2 != 0)) {
		msg_box(this, QMessageBox::Critical, tr("Invalid Parameters"),
						tr("Width and height must be multiples of 2."));
		return;
	}

	OakEngineTask *task = oakengine_task_create_export(
		reinterpret_cast<OakEngineSequence *>(viewer_node_),
		generate_params());

	if (export_bkg_box_->isChecked()) {
		// Send to TaskManager to export in background
		oakengine_task_manager_add(task);
		this->accept();
	} else {
		// Use modal dialog box
		TaskDialog *td = new TaskDialog(task, tr("Export"), this);
		connect(td, &TaskDialog::task_succeeded, this,
				&ExportDialog::export_finished);
		td->open();
	}
}

void ExportDialog::export_finished()
{
	TaskDialog *td = static_cast<TaskDialog *>(sender());

	if (oakengine_task_is_cancelled(td->get_task())) {
		// If this task was cancelled, we stay open so the user can potentially queue another export
	} else {
		// Accept this dialog and close
		if (import_file_after_export_->isEnabled() &&
			import_file_after_export_->isChecked()) {
			QString filename = filename_edit_->text().trimmed();
			emit request_import_file(filename);
		}

		this->accept();
	}
}

void ExportDialog::image_sequence_check_box_changed(bool e)
{
	QFileInfo current_fileinfo(filename_edit_->text());

	QString basename = current_fileinfo.completeBaseName();
	QString suffix = current_fileinfo.suffix();

	if (e) {
		if (!oakengine_encoding_filename_contains_digit_placeholder(basename.toUtf8().constData())) {
			basename.append(QStringLiteral("_[#####]"));
		}
	} else {
		char buf[1024];
		oakengine_encoding_filename_remove_digit_placeholder(
			basename.toUtf8().constData(), buf, sizeof(buf));
		basename = QString::fromUtf8(buf);
	}

	// Set filename
	if (!suffix.isEmpty()) {
		basename.append('.');
		basename.append(suffix);
	}
	filename_edit_->setText(current_fileinfo.dir().filePath(basename));
}

void ExportDialog::save_preset()
{
	ExportSavePresetDialog d(generate_params(), this);
	if (d.exec() == QDialog::Accepted) {
		load_presets();
		preset_combobox_->setCurrentText(d.get_selected_preset_name());
	}
}

void ExportDialog::preset_combo_box_changed()
{
	if (loading_presets_) {
		return;
	}

	QComboBox *c = static_cast<QComboBox *>(sender());

	int preset_number = c->currentData().toInt();
	if (preset_number == k_preset_default) {
		set_defaults();
	} else if (preset_number == k_preset_last_used) {
		OakEngineEncodingParams *last =
			oakengine_encoding_params_get_last_used(
				reinterpret_cast<OakEngineSequence *>(viewer_node_));
		if (last) {
			set_params(last);
		} else {
			set_defaults();
		}
	} else {
		set_params(presets_.at(preset_number));
	}
}

void ExportDialog::add_preferences_tab(QWidget *inner_widget,
									 const QString &title)
{
	QScrollArea *scroll_area = new QScrollArea();
	scroll_area->setWidgetResizable(true);
	scroll_area->setWidget(inner_widget);
	preferences_tabs_->addTab(scroll_area, title);
}

void ExportDialog::browse_filename()
{
	int f = format_combobox_->get_format();

	char name_buf[256];
	char ext_buf[64];
	oakengine_encoding_format_name(f, name_buf, sizeof(name_buf));
	oakengine_encoding_format_extension(f, ext_buf, sizeof(ext_buf));

	QString browsed_fn = QFileDialog::getSaveFileName(
		this, "", filename_edit_->text().trimmed(),
		QStringLiteral("%1 (*.%2)")
			.arg(QString::fromUtf8(name_buf), QString::fromUtf8(ext_buf)),
		nullptr,

		// We don't confirm overwrite here because we do it later
		QFileDialog::DontConfirmOverwrite);

	if (!browsed_fn.isEmpty()) {
		filename_edit_->setText(browsed_fn);
	}
}

void ExportDialog::format_changed(int current_format)
{
	QString current_filename = filename_edit_->text().trimmed();
	char ext_buf[64];
	oakengine_encoding_format_extension(previously_selected_format_, ext_buf, sizeof(ext_buf));
	QString previously_selected_ext = QString::fromUtf8(ext_buf);
	oakengine_encoding_format_extension(current_format, ext_buf, sizeof(ext_buf));
	QString currently_selected_ext = QString::fromUtf8(ext_buf);

	// If the previous extension was added, remove it
	if (current_filename.endsWith(previously_selected_ext,
								  Qt::CaseInsensitive)) {
		current_filename.resize(current_filename.size() -
								previously_selected_ext.size() - 1);
	}

	// Add the extension and set it
	current_filename.append('.');
	current_filename.append(currently_selected_ext);
	filename_edit_->setText(current_filename);

	previously_selected_format_ = current_format;

	// Update video and audio comboboxes
	bool has_video_codecs = video_tab_->set_format(current_format);
	video_enabled_->setChecked(has_video_codecs);
	video_enabled_->setEnabled(has_video_codecs);

	bool has_audio_codecs = audio_tab_->set_format(current_format);
	audio_enabled_->setChecked(has_audio_codecs);
	audio_enabled_->setEnabled(has_audio_codecs);

	if (subtitles_enabled_->isEnabled()) {
		subtitle_tab_->set_format(current_format);
	}
}

void ExportDialog::resolution_changed()
{
	if (video_tab_->maintain_aspect_checkbox()->isChecked()) {
		// Keep aspect ratio maintained
		if (sender() == video_tab_->height_slider()) {
			// Convert height to float
			double new_width = video_tab_->height_slider()->get_value();

			// Generate width from aspect ratio
			new_width *= video_aspect_ratio_;

			// Align to even number and set
			video_tab_->width_slider()->set_value(new_width);

		} else {
			// Convert width to float
			double new_height = video_tab_->width_slider()->get_value();

			// Generate height from aspect ratio
			new_height /= video_aspect_ratio_;

			// Align to even number and set
			video_tab_->height_slider()->set_value(new_height);
		}
	}

	update_viewer_dimensions();
}

void ExportDialog::load_presets()
{
	loading_presets_ = true;

	preset_combobox_->clear();
	presets_.clear();

	preset_combobox_->addItem(tr("Default"), k_preset_default);

	if (oakengine_encoding_params_get_last_used(
			reinterpret_cast<OakEngineSequence *>(viewer_node_)) != nullptr) {
		preset_combobox_->addItem(tr("Last Used"), k_preset_last_used);
	}

	preset_combobox_->insertSeparator(preset_combobox_->count());

	QStringList l;
	{
		const int n = oakengine_encoding_preset_count();
		for (int i = 0; i < n; i++) {
			char name_buf[256];
			if (oakengine_encoding_preset_name(
					i, name_buf, static_cast<int>(sizeof(name_buf))) > 0) {
				l.append(QString::fromUtf8(name_buf));
			}
		}
	}
	presets_.reserve(l.size());

	for (const QString &preset : l) {
		OakEngineEncodingParams *p = oakengine_encoding_params_create();

		char preset_path_buf[1024];
		preset_path_buf[0] = '\0';
		oakengine_encoding_preset_path(
			preset_path_buf, static_cast<int>(sizeof(preset_path_buf)));

		const QByteArray preset_path_utf =
			QDir(QString::fromUtf8(preset_path_buf))
				.filePath(preset)
				.toUtf8();
		const int rc = oakengine_encoding_params_load_file(
			p, preset_path_utf.constData());
		if (rc == OAKENGINE_OK) {
			preset_combobox_->addItem(preset, int(presets_.size()));
			presets_.push_back(p);
		} else {
			oakengine_encoding_params_destroy(p);
		}
	}

	loading_presets_ = false;
}

void ExportDialog::set_default_filename()
{
	Project *p = viewer_node_->project();

	char fn_buf[512];
	oakengine_project_filename(
		reinterpret_cast<OakEngineProject *>(p),
		fn_buf, sizeof(fn_buf));
	QDir doc_location;

	if (fn_buf[0] == '\0') {
		doc_location.setPath(QStandardPaths::writableLocation(
			QStandardPaths::DocumentsLocation));
	} else {
		doc_location = QFileInfo(fn_buf).dir();
	}

	QString file_location = doc_location.filePath(viewer_node_->get_label());
	filename_edit_->setText(file_location);
}

bool ExportDialog::sequence_has_subtitles() const
{
	if (Sequence *s = dynamic_cast<Sequence *>(viewer_node_)) {
		TrackList *tl = s->track_list(Track::k_subtitle);
		for (Track *t : tl->get_tracks()) {
			if (!t->is_muted() && !t->blocks().empty()) {
				return true;
			}
		}
	}

	return false;
}

void ExportDialog::set_defaults()
{
	if (!stills_only_mode_) {
		format_combobox_->set_format(OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO);
	} else {
		format_combobox_->set_format(OAKENGINE_ENCODING_FORMAT_PNG);
	}
	format_changed(format_combobox_->get_format());

	VideoParams vp = viewer_output_video_params(viewer_node_);
	AudioParams ap = viewer_output_audio_params(viewer_node_);

	video_tab_->width_slider()->set_value(vp.width());
	video_tab_->width_slider()->SetDefaultValue(vp.width());
	video_tab_->height_slider()->set_value(vp.height());
	video_tab_->height_slider()->SetDefaultValue(vp.height());
	video_tab_->set_selected_frame_rate(vp.frame_rate());
	video_tab_->pixel_aspect_combobox()->set_pixel_aspect_ratio(
		vp.pixel_aspect_ratio());
	video_tab_->pixel_format_field()->set_pixel_format(
		static_cast<PixelFormat::Format>(
			OAK_CONFIG("OnlinePixelFormat").toInt()));
	video_tab_->interlaced_combobox()->set_interlace_mode(vp.interlacing());
	audio_tab_->sample_rate_combobox()->set_sample_rate(ap.sample_rate());
	audio_tab_->sample_format_combobox()->set_attempt_to_restore_format(false);
	audio_tab_->channel_layout_combobox()->set_channel_layout(
		ap.channel_layout());
	subtitles_enabled_->setChecked(sequence_has_subtitles());
	subtitle_tab_->set_sidecar_format(OAKENGINE_ENCODING_FORMAT_SRT);
}

OakEngineEncodingParams *ExportDialog::generate_params() const
{
	OakEngineEncodingParams *params = oakengine_encoding_params_create();

	oakengine_encoding_params_set_format(
		params, format_combobox_->get_format());
	oakengine_encoding_params_set_filename(
		params, filename_edit_->text().trimmed().toUtf8().constData());

	const Rational export_len = viewer_node_->get_length();
	oakengine_encoding_params_set_export_length(
		params, export_len.numerator(), export_len.denominator());

	if (oakengine_encoding_codec_is_still_image(video_tab_->get_selected_codec()) &&
		!video_tab_->is_image_sequence_set()) {
		// Exporting as image without exporting image sequence, only export one frame
		Rational export_time = video_tab_->get_still_image_time();
		const Rational tb = get_selected_timebase();
		oakengine_encoding_params_set_custom_range(
			params, export_time.numerator(), export_time.denominator(),
			(export_time + tb).numerator(),
			(export_time + tb).denominator());
	} else if (range_combobox_->currentIndex() == k_range_in_to_out) {
		const TimeRange &r = viewer_node_->get_work_area()->range();
		oakengine_encoding_params_set_custom_range(
			params, r.in().numerator(), r.in().denominator(),
			r.out().numerator(), r.out().denominator());
	}

	if (video_tab_->scaling_method_combobox()->isEnabled()) {
		oakengine_encoding_params_set_video_scaling_method(
			params,
			video_tab_->scaling_method_combobox()->currentData().toInt());
	}

	if (video_enabled_->isChecked()) {
		const int video_codec = video_tab_->get_selected_codec();

		// Build video params from the tab
		const int vw = static_cast<int>(video_tab_->width_slider()->get_value());
		const int vh = static_cast<int>(video_tab_->height_slider()->get_value());
		const Rational tb = get_selected_timebase();
		const int pix_fmt = video_tab_->pixel_format_field()->get_pixel_format();
		const int ch_count = oakengine_video_params_internal_channel_count();
		const Rational par = video_tab_->pixel_aspect_combobox()->get_pixel_aspect_ratio();
		const int interlace = video_tab_->interlaced_combobox()->get_interlace_mode();

		oak_video_params vp = {};
		vp.width = vw;
		vp.height = vh;
		vp.time_base_num = tb.numerator();
		vp.time_base_den = tb.denominator();
		vp.format = pix_fmt;
		vp.pixel_aspect_num = par.numerator();
		vp.pixel_aspect_den = par.denominator();
		vp.interlacing = interlace;
		vp.color_range = video_tab_->color_range();

		oakengine_encoding_params_enable_video(params, &vp, video_codec);

		oakengine_encoding_params_set_video_threads(
			params, video_tab_->threads());

		if (video_tab_->isVisible()) {
			video_tab_->get_codec_section()->add_opts(params);
		}

		{
			const QString ct = video_tab_->current_ocio_color_space();
			oakengine_encoding_params_set_color_transform(
				params, ct.isEmpty() ? nullptr : ct.toUtf8().constData());
		}

		{
			const QString pix_fmt_name = video_tab_->pix_fmt();
			oakengine_encoding_params_set_video_pix_fmt(
				params,
				pix_fmt_name.isEmpty() ? nullptr
									   : pix_fmt_name.toUtf8().constData());
		}

		oakengine_encoding_params_set_video_is_image_sequence(
			params, video_tab_->is_image_sequence_set() ? 1 : 0);
	}

	if (audio_enabled_->isChecked()) {
		const int audio_codec = audio_tab_->get_codec();
		const int sample_rate = audio_tab_->sample_rate_combobox()->get_sample_rate();
		const uint64_t ch_layout = audio_tab_->channel_layout_combobox()->get_channel_layout();
		const int sample_fmt = audio_tab_->sample_format_combobox()->get_sample_format();

		oakengine_encoding_params_enable_audio(
			params, sample_rate, ch_layout, sample_fmt, audio_codec);

		oakengine_encoding_params_set_audio_bit_rate(
			params,
			audio_tab_->bit_rate_slider()->get_value() * 1000);
	}

	if (subtitles_enabled_->isEnabled() && subtitles_enabled_->isChecked()) {
		if (!subtitle_tab_->get_sidecar_enabled()) {
			// Export subtitles embedded in container
			oakengine_encoding_params_enable_subtitles(
				params, subtitle_tab_->get_subtitle_codec());
		} else {
			// Export subtitles to a sidecar file
			oakengine_encoding_params_enable_sidecar_subtitles(
				params, subtitle_tab_->get_sidecar_format(),
				subtitle_tab_->get_subtitle_codec());
		}
	}

	return params;
}

void ExportDialog::set_params(const OakEngineEncodingParams *e)
{
	format_combobox_->set_format(oakengine_encoding_params_format(e));
	format_changed(format_combobox_->get_format());

	if (oakengine_encoding_params_has_custom_range(e) &&
		viewer_node_->get_work_area()->enabled()) {
		range_combobox_->setCurrentIndex(k_range_in_to_out);
	}

	QtUtils::set_combo_box_data(video_tab_->scaling_method_combobox(),
								oakengine_encoding_params_video_scaling_method(e));

	const int video_enabled = oakengine_encoding_params_video_enabled(e);
	video_enabled_->setChecked(video_enabled);
	if (video_enabled) {
		oak_video_params vp = {};
		oakengine_encoding_params_get_video_params(e, &vp);

		video_tab_->width_slider()->set_value(vp.width);
		video_tab_->height_slider()->set_value(vp.height);
		set_selected_timebase(Rational(vp.time_base_num, vp.time_base_den));
		video_tab_->pixel_format_field()->set_pixel_format(
			static_cast<olive::core::PixelFormat::Format>(vp.format));
		video_tab_->pixel_aspect_combobox()->set_pixel_aspect_ratio(
			Rational(vp.pixel_aspect_num, vp.pixel_aspect_den));
		video_tab_->interlaced_combobox()->set_interlace_mode(vp.interlacing);

		video_tab_->set_selected_codec(oakengine_encoding_params_video_codec(e));

		video_tab_->set_color_range(vp.color_range);

		video_tab_->set_threads(oakengine_encoding_params_video_threads(e));

		if (video_tab_->isVisible()) {
			video_tab_->get_codec_section()->set_opts(e);
		}

		{
			char ct_buf[128];
			if (oakengine_encoding_params_color_transform_output(
					e, ct_buf, static_cast<int>(sizeof(ct_buf))) > 0) {
				video_tab_->set_ocio_color_space(QString::fromUtf8(ct_buf));
			} else {
				video_tab_->set_ocio_color_space(QString());
			}
		}

		{
			char pix_fmt_buf[64];
			if (oakengine_encoding_params_video_pix_fmt(
					e, pix_fmt_buf, static_cast<int>(sizeof(pix_fmt_buf))) > 0) {
				video_tab_->set_pix_fmt(QString::fromUtf8(pix_fmt_buf));
			} else {
				video_tab_->set_pix_fmt(QString());
			}
		}

		video_tab_->set_image_sequence(
			oakengine_encoding_params_video_is_image_sequence(e));
	}

	const int audio_enabled = oakengine_encoding_params_audio_enabled(e);
	audio_enabled_->setChecked(audio_enabled);
	if (audio_enabled) {
		int asample_rate = 0;
		uint64_t ach_layout = 0;
		int asample_fmt = 0;
		oakengine_encoding_params_get_audio_params(
			e, &asample_rate, &ach_layout, &asample_fmt);

		audio_tab_->sample_rate_combobox()->set_sample_rate(asample_rate);
		audio_tab_->channel_layout_combobox()->set_channel_layout(ach_layout);
		audio_tab_->sample_format_combobox()->set_sample_format(
			static_cast<olive::core::SampleFormat::Format>(asample_fmt));

		audio_tab_->set_codec(oakengine_encoding_params_audio_codec(e));

		audio_tab_->bit_rate_slider()->set_value(
			oakengine_encoding_params_audio_bit_rate(e) / 1000);
	}

	if (subtitles_enabled_->isEnabled()) {
		const int subs_enabled = oakengine_encoding_params_subtitles_enabled(e);
		subtitles_enabled_->setChecked(subs_enabled);
		subtitle_tab_->set_sidecar_enabled(
			oakengine_encoding_params_subtitles_are_sidecar(e));
		if (subs_enabled) {
			subtitle_tab_->set_subtitle_codec(
				oakengine_encoding_params_subtitles_codec(e));
			if (oakengine_encoding_params_subtitles_are_sidecar(e)) {
				subtitle_tab_->set_sidecar_format(
					oakengine_encoding_params_subtitles_sidecar_format(e));
			}
		}
	}
}

bool ExportDialog::eventFilter(QObject *o, QEvent *e)
{
	// Any parameters in scrollable areas, ignore wheel events so the user doesn't unwittingly change
	// them while trying to scroll through the pages
	if (e->type() == QEvent::Wheel) {
		while ((o = o->parent())) {
			if (o == video_tab_ || o == audio_tab_ || o == subtitle_tab_) {
				e->ignore();
				return true;
			}
		}
	}

	return super::eventFilter(o, e);
}

void ExportDialog::done(int r)
{
	preview_viewer_->connect_viewer_node(nullptr);

	if (!stills_only_mode_) {
		OakEngineEncodingParams *p = generate_params();
		oakengine_encoding_params_set_last_used(
			reinterpret_cast<OakEngineSequence *>(viewer_node_), p);
		oakengine_encoding_params_destroy(p);
	}

	super::done(r);
}

Rational ExportDialog::get_export_length() const
{
	if (range_combobox_->currentIndex() == k_range_in_to_out) {
		return viewer_node_->get_work_area()->range().length();
	} else {
		return viewer_node_->get_length();
	}
}

int64_t ExportDialog::get_export_length_in_timebase_units() const
{
	return Timecode::time_to_timestamp(get_export_length(),
									   get_selected_timebase());
}

void ExportDialog::update_viewer_dimensions()
{
	preview_viewer_->set_viewer_resolution(
		static_cast<int>(video_tab_->width_slider()->get_value()),
		static_cast<int>(video_tab_->height_slider()->get_value()));

	VideoParams vp = viewer_output_video_params(viewer_node_);

	float mat16[16];
	oakengine_encoding_generate_matrix(
		video_tab_->scaling_method_combobox()->currentData().toInt(),
		vp.width(), vp.height(),
		static_cast<int>(video_tab_->width_slider()->get_value()),
		static_cast<int>(video_tab_->height_slider()->get_value()),
		mat16);
	QMatrix4x4 transform(mat16);

	preview_viewer_->set_matrix(transform);
}

}
