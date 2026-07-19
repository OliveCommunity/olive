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

#include "viewersizer.h"

#include <QApplication>
#include <QEvent>
#include <QMatrix4x4>
#include <QWheelEvent>

#include "widget/handmovableview/handmovableview.h"

namespace olive
{

ViewerSizer::ViewerSizer(QWidget *parent)
	: QWidget(parent)
	, widget_(nullptr)
	, width_(0)
	, height_(0)
	, pixel_aspect_(1)
	, zoom_(-1)
	, current_widget_scale_(0)
{
	horiz_scrollbar_ = new QScrollBar(Qt::Horizontal, this);
	horiz_scrollbar_->setVisible(false);
	connect(horiz_scrollbar_, &QScrollBar::valueChanged, this,
			&ViewerSizer::scroll_bar_moved);

	vert_scrollbar_ = new QScrollBar(Qt::Vertical, this);
	vert_scrollbar_->setVisible(false);
	connect(vert_scrollbar_, &QScrollBar::valueChanged, this,
			&ViewerSizer::scroll_bar_moved);

	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ViewerSizer::set_widget(QWidget *widget)
{
	// Delete any previous widgets occupying this space
	delete widget_;

	widget_ = widget;

	if (widget_ != nullptr) {
		widget_->setParent(this);
		widget_->installEventFilter(this);

		update_size();
	}
}

QSize ViewerSizer::get_container_size() const
{
	double s = get_real_current_zoom();
	return QSize(std::min(this->width(), int(width_ * s)) -
					 vert_scrollbar_->width(),
				 std::min(int(height_ * s), this->height()) -
					 horiz_scrollbar_->height());
}

void ViewerSizer::set_child_size(int width, int height)
{
	width_ = width;
	height_ = height;

	update_size();
}

void ViewerSizer::set_pixel_aspect_ratio(const Rational &pixel_aspect)
{
	pixel_aspect_ = pixel_aspect;

	update_size();
}

void ViewerSizer::set_zoom(double percent)
{
	zoom_ = percent;

	update_size();
}

void ViewerSizer::set_zoom_anchored(double next_scale, double cursor_x,
								  double cursor_y)
{
	if (next_scale > 0) {
		double cur_scale = get_real_current_zoom();

		// Clamp scale within safe values
		next_scale = std::clamp(next_scale, k_zoom_levels[0],
								k_zoom_levels[k_zoom_level_count - 1]);

		int anchor_x = qRound(double(cursor_x + horiz_scrollbar_->value()) /
								  cur_scale * next_scale -
							  cursor_x);
		int anchor_y = qRound(double(cursor_y + vert_scrollbar_->value()) /
								  cur_scale * next_scale -
							  cursor_y);

		set_zoom(next_scale);

		horiz_scrollbar_->setValue(anchor_x);
		vert_scrollbar_->setValue(anchor_y);
	} else {
		set_zoom(-1);

		horiz_scrollbar_->setValue(0);
		vert_scrollbar_->setValue(0);
	}
}

void ViewerSizer::hand_drag_move(int x, int y)
{
	if (horiz_scrollbar_->isVisible()) {
		horiz_scrollbar_->setValue(horiz_scrollbar_->value() - x);
	}

	if (vert_scrollbar_->isVisible()) {
		vert_scrollbar_->setValue(vert_scrollbar_->value() - y);
	}
}

bool ViewerSizer::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == widget_) {
		if (event->type() == QEvent::Wheel) {
			QWheelEvent *w = static_cast<QWheelEvent *>(event);

			if (HandMovableView::WheelEventIsAZoomEvent(w)) {
				double next_scale = get_real_current_zoom() *
									HandMovableView::get_scroll_zoom_multiplier(w);
				QPointF cursor_pos = w->position();
				set_zoom_anchored(next_scale, cursor_pos.x(), cursor_pos.y());
			} else {
				// Pass scroll values to scrollbars
				QPoint p = w->pixelDelta();
				horiz_scrollbar_->setValue(horiz_scrollbar_->value() - p.x());
				vert_scrollbar_->setValue(vert_scrollbar_->value() - p.y());
			}
			return true;
		}
	}

	return QWidget::eventFilter(watched, event);
}

void ViewerSizer::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	update_size();
}

void ViewerSizer::update_size()
{
	if (widget_ == nullptr) {
		return;
	}

	// If the aspect ratio is 0, default to taking all space
	if (!width_ || !height_) {
		widget_->move(0, 0);
		widget_->resize(width(), height());
		return;
	}

	// Calculate how much UI space is available (it will be less if we have to show scrollbars)
	int available_width = width();
	int available_height = height();

	// Determine if we need scrollbars for the zoom we want
	horiz_scrollbar_->setVisible(zoom_ > 0 &&
								 get_zoomed_value(width_) > available_width);
	vert_scrollbar_->setVisible(zoom_ > 0 &&
								get_zoomed_value(height_) > available_height);

	// Horizontal scrollbar will reduce the available height
	if (horiz_scrollbar_->isVisible()) {
		available_height -= horiz_scrollbar_->sizeHint().height();
	}

	// Vertical scrollbar will reduce the available width
	if (vert_scrollbar_->isVisible()) {
		available_width -= vert_scrollbar_->sizeHint().width();
	}

	// Set correct values on horizontal scrollbar
	if (horiz_scrollbar_->isVisible()) {
		horiz_scrollbar_->resize(available_width,
								 horiz_scrollbar_->sizeHint().height());
		horiz_scrollbar_->move(0,
							   this->height() - horiz_scrollbar_->height() - 1);
		horiz_scrollbar_->setMaximum(get_zoomed_value(width_) - available_width);
		horiz_scrollbar_->setPageStep(available_width);
	}

	// Set correct values on vertical scrollbar
	if (vert_scrollbar_->isVisible()) {
		vert_scrollbar_->resize(vert_scrollbar_->sizeHint().width(),
								available_height);
		vert_scrollbar_->move(this->width() - vert_scrollbar_->width() - 1, 0);
		vert_scrollbar_->setMaximum(get_zoomed_value(height_) - available_height);
		vert_scrollbar_->setPageStep(available_height);
	}

	// Size widget to the UI space we've calculated
	widget_->resize(available_width, available_height);

	// Adjust to aspect ratio
	double sequence_aspect_ratio =
		double(width_) / double(height_) * pixel_aspect_.to_double();
	double our_aspect_ratio =
		double(available_width) / double(available_height);

	QMatrix4x4 child_matrix;
	double current_scale;

	if (our_aspect_ratio > sequence_aspect_ratio) {
		// This container is wider than the image, scale by height
		child_matrix.scale(sequence_aspect_ratio / our_aspect_ratio, 1.0);
		current_scale = double(available_height) / double(height_);

	} else {
		// This container is taller than the image, scale by width
		child_matrix.scale(1.0, our_aspect_ratio / sequence_aspect_ratio);
		current_scale = double(available_width) / double(width_);
	}

	current_widget_scale_ = current_scale;

	if (zoom_ > 0) {
		// Scale to get to the requested zoom
		double zoom_diff = zoom_ / current_scale;
		child_matrix.scale(zoom_diff, zoom_diff, 1.0);
	}

	emit request_scale(child_matrix);

	scroll_bar_moved();
}

int ViewerSizer::get_zoomed_value(int value)
{
	return qRound(value * zoom_);
}

double ViewerSizer::get_real_current_zoom() const
{
	if (zoom_ < 0) {
		// Currently set to "fit"
		return current_widget_scale_;
	} else {
		// Explicit zoom set
		return zoom_;
	}
}

void ViewerSizer::scroll_bar_moved()
{
	QMatrix4x4 mat;

	float x_scroll, y_scroll;

	if (horiz_scrollbar_->isVisible()) {
		int zoomed_width = get_zoomed_value(width_);
		x_scroll = (zoomed_width / 2 - horiz_scrollbar_->value() -
					widget_->width() / 2) *
				   (2.0 / zoomed_width);
	} else {
		x_scroll = 0;
	}

	if (vert_scrollbar_->isVisible()) {
		int zoomed_height = get_zoomed_value(height_);
		y_scroll = (zoomed_height / 2 - vert_scrollbar_->value() -
					widget_->height() / 2) *
				   (2.0 / zoomed_height);
	} else {
		y_scroll = 0;
	}

	// Zero translate is centered, so we need to determine how much "off center" we are
	mat.translate(x_scroll, y_scroll);

	emit request_translate(mat);
}

}
