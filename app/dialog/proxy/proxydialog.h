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

#ifndef OAK_PROXYDIALOG_H
#define OAK_PROXYDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QTreeWidget>

#include "codec/proxymanager.h"
#include "node/project/footage/footage.h"
#include "widget/slider/integerslider.h"

namespace olive
{

class ProxyDialog : public QDialog {
	Q_OBJECT
public:
	ProxyDialog(QWidget *parent, const QVector<Footage *> &footage = {});

	virtual void accept() override;

	int proxy_width() const;

	int proxy_height() const;

	int proxy_crf() const;

	QString proxy_preset() const;

	bool proxy_include_audio() const;

	QString f_fmpeg_path() const;

	void set_proxy_width(int width);

	void set_proxy_height(int height);

	void set_proxy_crf(int crf);

	void set_proxy_preset(const QString &preset);

	void set_proxy_include_audio(bool include_audio);

	void set_f_fmpeg_path(const QString &path);

private:
	ProxyManager::ProxyParams current_params() const;

	void save_global_settings();

	void refresh_footage_list();

	QVector<Footage *> footage_;

	QTreeWidget *footage_tree_;

	QCheckBox *custom_params_checkbox_;

	IntegerSlider *width_slider_;

	IntegerSlider *height_slider_;

	IntegerSlider *crf_slider_;

	QComboBox *preset_combo_;

	QCheckBox *include_audio_checkbox_;

	QLineEdit *ffmpeg_path_edit_;

private slots:
	void generate_proxies();

	void delete_proxies();

	void browse_for_f_fmpeg();
};

}

#endif // OAK_PROXYDIALOG_H
