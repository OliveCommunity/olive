/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2026 Oak Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "proxydialog.h"

#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "config/config.h"
#include "node/project.h"

namespace olive
{

ProxyDialog::ProxyDialog(QWidget *parent, const QVector<Footage *> &footage)
	: QDialog(parent)
	, footage_(footage)
	, footage_tree_(nullptr)
	, custom_params_checkbox_(nullptr)
{
	setWindowTitle(tr("Proxy Settings"));

	const ProxyManager::ProxyParams params =
		ProxyManager::proxy_params_from_config();

	QVBoxLayout *layout = new QVBoxLayout(this);

	if (!footage_.isEmpty()) {
		QGroupBox *footage_group = new QGroupBox(tr("Selected Footage"));
		layout->addWidget(footage_group);
		QVBoxLayout *footage_layout = new QVBoxLayout(footage_group);

		footage_tree_ = new QTreeWidget();
		footage_tree_->setHeaderLabels({ tr("Footage"), tr("Proxy State") });
		footage_tree_->setRootIsDecorated(false);
		footage_layout->addWidget(footage_tree_);
		refresh_footage_list();

		custom_params_checkbox_ =
			new QCheckBox(tr("Use custom settings for selected footage"));
		bool any_custom = false;
		for (const Footage *item : footage_) {
			if (item->has_custom_proxy_params()) {
				any_custom = true;
				break;
			}
		}
		custom_params_checkbox_->setChecked(any_custom);
		footage_layout->addWidget(custom_params_checkbox_);
	}

	QGroupBox *settings_group = new QGroupBox(tr("Global Proxy Settings"));
	layout->addWidget(settings_group);
	QGridLayout *settings_layout = new QGridLayout(settings_group);

	int row = 0;

	settings_layout->addWidget(new QLabel(tr("Proxy Resolution:")), row, 0);
	resolution_combo_ = new QComboBox();
	resolution_combo_->addItem(tr("Custom size"), 1);
	resolution_combo_->addItem(tr("1/2 of source"), 2);
	resolution_combo_->addItem(tr("1/4 of source"), 4);
	resolution_combo_->addItem(tr("1/8 of source"), 8);
	const int divider_index = resolution_combo_->findData(params.divider);
	if (divider_index >= 0) {
		resolution_combo_->setCurrentIndex(divider_index);
	}
	settings_layout->addWidget(resolution_combo_, row, 1, 1, 3);

	row++;

	settings_layout->addWidget(new QLabel(tr("Proxy Width:")), row, 0);
	width_slider_ = new IntegerSlider();
	width_slider_->set_minimum(160);
	width_slider_->set_maximum(4096);
	width_slider_->set_value(params.width);
	settings_layout->addWidget(width_slider_, row, 1);

	settings_layout->addWidget(new QLabel(tr("Proxy Height:")), row, 2);
	height_slider_ = new IntegerSlider();
	height_slider_->set_minimum(120);
	height_slider_->set_maximum(2160);
	height_slider_->set_value(params.height);
	settings_layout->addWidget(height_slider_, row, 3);

	// Absolute size only applies in "Custom size" mode
	const auto update_size_sliders_enabled = [this]() {
		const bool custom_size = resolution_combo_->currentData().toInt() == 1;
		width_slider_->setEnabled(custom_size);
		height_slider_->setEnabled(custom_size);
	};
	connect(resolution_combo_, &QComboBox::currentIndexChanged, this,
			update_size_sliders_enabled);
	update_size_sliders_enabled();

	row++;

	settings_layout->addWidget(new QLabel(tr("Proxy CRF:")), row, 0);
	crf_slider_ = new IntegerSlider();
	crf_slider_->set_minimum(0);
	crf_slider_->set_maximum(51);
	crf_slider_->set_value(params.crf);
	settings_layout->addWidget(crf_slider_, row, 1);

	settings_layout->addWidget(new QLabel(tr("Proxy Preset:")), row, 2);
	preset_combo_ = new QComboBox();
	const QStringList presets = {
		QStringLiteral("ultrafast"), QStringLiteral("superfast"),
		QStringLiteral("veryfast"),	 QStringLiteral("faster"),
		QStringLiteral("fast"),		 QStringLiteral("medium"),
		QStringLiteral("slow"),		 QStringLiteral("slower"),
		QStringLiteral("veryslow"),
	};
	for (const QString &preset : presets) {
		preset_combo_->addItem(preset);
	}
	preset_combo_->setCurrentText(params.preset);
	settings_layout->addWidget(preset_combo_, row, 3);

	row++;

	include_audio_checkbox_ = new QCheckBox(tr("Include audio in proxies"));
	include_audio_checkbox_->setChecked(params.include_audio);
	settings_layout->addWidget(include_audio_checkbox_, row, 0, 1, 2);

	row++;

	settings_layout->addWidget(new QLabel(tr("ffmpeg Executable:")), row, 0);
	ffmpeg_path_edit_ = new QLineEdit(OAK_CONFIG("FFmpegPath").toString());
	ffmpeg_path_edit_->setPlaceholderText(tr("Auto-detect"));
	settings_layout->addWidget(ffmpeg_path_edit_, row, 1);

	QPushButton *ffmpeg_browse_btn = new QPushButton(tr("Browse..."));
	connect(ffmpeg_browse_btn, &QPushButton::clicked, this,
			&ProxyDialog::browse_for_f_fmpeg);
	settings_layout->addWidget(ffmpeg_browse_btn, row, 2);

	QHBoxLayout *button_layout = new QHBoxLayout();
	layout->addLayout(button_layout);

	if (!footage_.isEmpty()) {
		QPushButton *generate_btn = new QPushButton(tr("Generate Proxies"));
		connect(generate_btn, &QPushButton::clicked, this,
				&ProxyDialog::generate_proxies);
		button_layout->addWidget(generate_btn);

		QPushButton *delete_btn = new QPushButton(tr("Delete Proxies"));
		connect(delete_btn, &QPushButton::clicked, this,
				&ProxyDialog::delete_proxies);
		button_layout->addWidget(delete_btn);
	}

	button_layout->addStretch();

	QPushButton *close_btn = new QPushButton(tr("Close"));
	connect(close_btn, &QPushButton::clicked, this, &ProxyDialog::accept);
	button_layout->addWidget(close_btn);
}

void ProxyDialog::accept()
{
	save_global_settings();

	if (!footage_.isEmpty()) {
		for (Footage *item : footage_) {
			if (custom_params_checkbox_->isChecked()) {
				item->set_custom_proxy_params(current_params());
			} else {
				item->clear_custom_proxy_params();
			}
		}
	}

	QDialog::accept();
}

int ProxyDialog::proxy_width() const
{
	return static_cast<int>(width_slider_->get_value());
}

int ProxyDialog::proxy_height() const
{
	return static_cast<int>(height_slider_->get_value());
}

int ProxyDialog::proxy_divider() const
{
	return resolution_combo_->currentData().toInt();
}

int ProxyDialog::proxy_crf() const
{
	return static_cast<int>(crf_slider_->get_value());
}

QString ProxyDialog::proxy_preset() const
{
	return preset_combo_->currentText();
}

bool ProxyDialog::proxy_include_audio() const
{
	return include_audio_checkbox_->isChecked();
}

QString ProxyDialog::f_fmpeg_path() const
{
	return ffmpeg_path_edit_->text();
}

void ProxyDialog::set_proxy_width(int width)
{
	width_slider_->set_value(width);
}

void ProxyDialog::set_proxy_height(int height)
{
	height_slider_->set_value(height);
}

void ProxyDialog::set_proxy_divider(int divider)
{
	const int index = resolution_combo_->findData(divider);
	if (index >= 0) {
		resolution_combo_->setCurrentIndex(index);
	}
}

void ProxyDialog::set_proxy_crf(int crf)
{
	crf_slider_->set_value(crf);
}

void ProxyDialog::set_proxy_preset(const QString &preset)
{
	preset_combo_->setCurrentText(preset);
}

void ProxyDialog::set_proxy_include_audio(bool include_audio)
{
	include_audio_checkbox_->setChecked(include_audio);
}

void ProxyDialog::set_f_fmpeg_path(const QString &path)
{
	ffmpeg_path_edit_->setText(path);
}

ProxyManager::ProxyParams ProxyDialog::current_params() const
{
	ProxyManager::ProxyParams params = ProxyManager::proxy_params_from_config();
	params.width = static_cast<int>(width_slider_->get_value());
	params.height = static_cast<int>(height_slider_->get_value());
	params.divider = resolution_combo_->currentData().toInt();
	params.crf = static_cast<int>(crf_slider_->get_value());
	params.preset = preset_combo_->currentText();
	params.include_audio = include_audio_checkbox_->isChecked();
	return params;
}

void ProxyDialog::save_global_settings()
{
	OAK_CONFIG("ProxyWidth") = static_cast<int>(width_slider_->get_value());
	OAK_CONFIG("ProxyHeight") = static_cast<int>(height_slider_->get_value());
	OAK_CONFIG("ProxyDivider") = resolution_combo_->currentData().toInt();
	OAK_CONFIG("ProxyCRF") = static_cast<int>(crf_slider_->get_value());
	OAK_CONFIG("ProxyPreset") = preset_combo_->currentText();
	OAK_CONFIG("ProxyIncludeAudio") = include_audio_checkbox_->isChecked();
	OAK_CONFIG("FFmpegPath") = ffmpeg_path_edit_->text().trimmed();
}

void ProxyDialog::refresh_footage_list()
{
	if (!footage_tree_) {
		return;
	}

	footage_tree_->clear();
	for (const Footage *item : footage_) {
		QTreeWidgetItem *tree_item = new QTreeWidgetItem(footage_tree_);
		tree_item->setText(0, item->filename());
		QString state = ProxyManager::proxy_state_to_string(item->proxy_state());
		if (item->has_custom_proxy_params()) {
			state = tr("%1 (custom settings)").arg(state);
		}
		tree_item->setText(1, state);
	}
}

void ProxyDialog::generate_proxies()
{
	if (!ProxyManager::instance()) {
		qWarning() << "ProxyDialog::GenerateProxies: ProxyManager unavailable";
		return;
	}

	for (Footage *item : footage_) {
		const VideoParams video = item->get_first_enabled_video_stream();
		if (!video.is_valid()) {
			qWarning()
				<< "ProxyDialog::GenerateProxies: skipping item with no valid video stream"
				<< item->filename();
			continue;
		}

		const ProxyManager::ProxyParams params =
			custom_params_checkbox_->isChecked() ? current_params()
												 : item->get_effective_proxy_params();
		const ProxyManager::Proxy proxy =
			ProxyManager::instance()->get_or_start_proxy(
				item->project()->cache_path(), item->filename(),
				video.stream_index(), params);
		item->set_proxy(proxy.filename, proxy.state, video.stream_index(),
					   params.version, true);
		item->invalidate_all(Footage::k_filename_input);
	}

	refresh_footage_list();
}

void ProxyDialog::delete_proxies()
{
	for (Footage *item : footage_) {
		if (item->proxy_path().isEmpty()) {
			continue;
		}

		QFile::remove(item->proxy_path());
		QFile::remove(ProxyManager::get_working_proxy_filename(item->proxy_path()));
		item->clear_proxy();
		item->invalidate_all(Footage::k_filename_input);
	}

	refresh_footage_list();
}

void ProxyDialog::browse_for_f_fmpeg()
{
	const QString file =
		QFileDialog::getOpenFileName(this, tr("Select ffmpeg Executable"));
	if (!file.isEmpty()) {
		ffmpeg_path_edit_->setText(file);
	}
}

}
