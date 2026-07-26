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

#include "footageviewer.h"

#include <QDrag>
#include <QMimeData>

#include "common/configwrapper.h"
#include "node/project.h"
#include "oakengine/project.h"
#include "oakengine/timeline.h"

namespace olive
{

#define super ViewerWidget

FootageViewerWidget::FootageViewerWidget(QWidget *parent)
	: super(parent)
{
	connect(display_widget(), &ViewerDisplayWidget::drag_started, this,
			&FootageViewerWidget::start_footage_drag);

	controls_->set_audio_video_drag_buttons_visible(true);
	connect(controls_, &PlaybackControls::video_clicked, this,
			&FootageViewerWidget::video_button_clicked);
	connect(controls_, &PlaybackControls::audio_clicked, this,
			&FootageViewerWidget::audio_button_clicked);
	connect(controls_, &PlaybackControls::video_dragged, this,
			&FootageViewerWidget::start_video_drag);
	connect(controls_, &PlaybackControls::audio_dragged, this,
			&FootageViewerWidget::start_audio_drag);

	override_workarea_ = reinterpret_cast<TimelineWorkArea *>(
		oakengine_workarea_create());
}

FootageViewerWidget::~FootageViewerWidget()
{
	if (override_workarea_) {
		oakengine_workarea_free(
			reinterpret_cast<OakEngineWorkarea *>(override_workarea_));
	}
}

void FootageViewerWidget::override_work_area(const TimeRange &r)
{
	oakengine_workarea_set_enabled(
		reinterpret_cast<OakEngineWorkarea *>(override_workarea_), 1);
	oakengine_workarea_set_range(
		reinterpret_cast<OakEngineWorkarea *>(override_workarea_),
		r.in().numerator(), r.in().denominator(),
		r.out().numerator(), r.out().denominator());
	this->connect_work_area(override_workarea_);
}

void FootageViewerWidget::reset_work_area()
{
	if (get_connected_work_area() == override_workarea_) {
		this->connect_work_area(
			get_connected_node() ? get_connected_node()->get_work_area() : nullptr);
	}
}

void FootageViewerWidget::start_footage_drag_internal(bool enable_video,
												   bool enable_audio)
{
	if (!get_connected_node()) {
		return;
	}

	QDrag *drag = new QDrag(this);
	QMimeData *mimedata = new QMimeData();

	QByteArray encoded_data;
	QDataStream data_stream(&encoded_data, QIODevice::WriteOnly);

	QVector<Track::Reference> streams =
		get_connected_node()->get_enabled_streams_as_references();

	// Disable streams that have been disabled
	if (!enable_video || !enable_audio) {
		for (int i = 0; i < streams.size(); i++) {
			const Track::Reference &ref = streams.at(i);

			if ((ref.type() == Track::k_video && !enable_video) ||
				(ref.type() == Track::k_audio && !enable_audio)) {
				streams.removeAt(i);
				i--;
			}
		}
	}

	if (!streams.isEmpty()) {
		data_stream << streams
					<< reinterpret_cast<quintptr>(get_connected_node());

		mimedata->setData(QString::fromUtf8(oakengine_project_item_mime_type()), encoded_data);
		drag->setMimeData(mimedata);

		drag->exec();
	}
}

void FootageViewerWidget::start_footage_drag()
{
	start_footage_drag_internal(true, true);
}

void FootageViewerWidget::start_video_drag()
{
	start_footage_drag_internal(true, false);
}

void FootageViewerWidget::start_audio_drag()
{
	start_footage_drag_internal(false, true);
}

void FootageViewerWidget::video_button_clicked()
{
	this->set_waveform_mode(k_wf_automatic);
}

void FootageViewerWidget::audio_button_clicked()
{
	this->set_waveform_mode(k_wf_waveform_only);
}

}
