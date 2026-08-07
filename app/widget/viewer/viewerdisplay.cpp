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

#include "viewerdisplay.h"

#include <OpenImageIO/imagebuf.h>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLTexture>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTextEdit>

#include <cstring>

#include <olive/core/util/timecodefunctions.h>

#include "oakutil/define.h"
#include "common/htmlapp.h"
#include "oakengine/gizmo.h"
#include "oakengine/timeline.h"
#include "oakengine/videoparams.h"
#include "oakengine/display.h"
#include "oakengine/undo.h"
#include "widget/viewer/vieweroutpututils.h"
#include "oakutil/qtutils.h"
#include "common/configwrapper.h"
#include "core.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

#define super ManagedDisplayWidget

ViewerDisplayWidget::ViewerDisplayWidget(QWidget *parent)
	: super(parent)
	, texture_(nullptr)
	, deinterlace_texture_(nullptr)
	, backend_neutral_texture_(nullptr)
	, backend_neutral_cpu_display_frame_(nullptr)
	, backend_neutral_cpu_source_frame_(nullptr)
	, backend_neutral_cpu_source_texture_(nullptr)
	, deinterlace_shader_(nullptr)
	, blank_shader_(nullptr)
	, signal_cursor_color_(false)
	, gizmos_(nullptr)
	, gizmo_params_(empty_video_params())
	, current_gizmo_(nullptr)
	, gizmo_drag_started_(false)
	, show_subtitles_(true)
	, subtitle_tracks_(nullptr)
	, hand_dragging_(false)
	, deinterlace_(false)
	, show_fps_(false)
	, frames_skipped_(0)
	, show_widget_background_(false)
	, playback_speed_(0)
	, push_mode_(k_push_null)
	, add_band_(false)
	, queue_starved_(false)
	, text_edit_(nullptr)
{
	connect(Core::instance(), &Core::tool_changed, this,
			&ViewerDisplayWidget::tool_changed);

	// Initializes cursor based on tool
	update_cursor();

	const int k_frame_rate_average_count = 8;
	frame_rate_averages_.resize(k_frame_rate_average_count);

	inner_widget()->setAcceptDrops(true);

	bridge_ = new EngineEventBridge(this);
	connect(bridge_, &EngineEventBridge::sequence_subtitles_changed, this,
			[this](OakEngineSequence *, qint64, qint64) { update(); });

	// Issue 20: reuse the issue 7 undo signal so subtitle-track changes
	// replayed from the undo stack refresh the viewer display.
	connect(Core::instance(), &Core::undo_index_changed, this,
			static_cast<void (QWidget::*)()>(&QWidget::update));
}

ViewerDisplayWidget::~ViewerDisplayWidget()
{
	delete text_edit_;

	MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR_INNER;
}

void ViewerDisplayWidget::assign_texture(void *t, bool owned)
{
	if (owned_texture_) {
		oakengine_display_texture_free(owned_texture_);
	}
	if (t && !owned) {
		oakengine_display_texture_retain(t);
	}
	owned_texture_ = t;
	texture_ = t;
}

void ViewerDisplayWidget::set_matrix_translate(const QMatrix4x4 &mat)
{
	translate_matrix_ = mat;

	update_matrix();
}

void ViewerDisplayWidget::set_matrix_zoom(const QMatrix4x4 &mat)
{
	scale_matrix_ = mat;

	update_matrix();
}

void ViewerDisplayWidget::set_matrix_crop(const QMatrix4x4 &mat)
{
	crop_matrix_ = mat;

	update();
}

void ViewerDisplayWidget::update_cursor()
{
	if (Core::instance()->tool() == Tool::k_hand) {
		this->inner_widget()->setCursor(Qt::OpenHandCursor);
	} else if (Core::instance()->tool() == Tool::k_add) {
		this->inner_widget()->setCursor(Qt::CrossCursor);
	} else {
		this->inner_widget()->unsetCursor();
	}
}

void ViewerDisplayWidget::set_signal_cursor_color_enabled(bool e)
{
	signal_cursor_color_ = e;
	set_inner_mouse_tracking(e);
}

void ViewerDisplayWidget::set_image(const QVariant &buffer)
{
	load_frame_ = buffer;

	if (load_frame_.isNull()) {
		push_mode_ = k_push_null;
	} else {
		push_mode_ = k_push_frame;
	}

	update();
}

void ViewerDisplayWidget::set_blank()
{
	push_mode_ = k_push_blank;

	update();
}

void ViewerDisplayWidget::tool_changed()
{
	update_cursor();
}

void ViewerDisplayWidget::set_deinterlacing(bool e)
{
	deinterlace_ = e;

	if (!deinterlace_) {
		if (deinterlace_shader_) {
			oakengine_display_renderer_destroy_shader(renderer(), deinterlace_shader_);
			deinterlace_shader_ = nullptr;
		}
		if (deinterlace_texture_) {
			oakengine_display_texture_free(deinterlace_texture_);
			deinterlace_texture_ = nullptr;
		}
	}

	update();
}

const ViewerSafeMarginInfo &ViewerDisplayWidget::get_safe_margin() const
{
	return safe_margin_;
}

void ViewerDisplayWidget::set_safe_margins(const ViewerSafeMarginInfo &safe_margin)
{
	if (safe_margin_ != safe_margin) {
		safe_margin_ = safe_margin;

		update();
	}
}

void ViewerDisplayWidget::set_gizmos(OakEngineNode *node)
{
	if (gizmos_ != node) {
		gizmos_ = node;

		update();
	}
}

void ViewerDisplayWidget::set_video_params(const oak::VideoParams &params)
{
	gizmo_params_ = params;

	if (gizmos_) {
		update();
	}
}

void ViewerDisplayWidget::set_audio_params(const AudioParams &params)
{
	gizmo_audio_params_ = params;

	if (gizmos_) {
		update();
	}
}

void ViewerDisplayWidget::set_time(const Rational &time)
{
	time_ = time;

	if (gizmos_) {
		update();
	}
}

void ViewerDisplayWidget::set_subtitle_tracks(OakEngineSequence *list)
{
	if (subtitle_tracks_) {
		bridge_->unsubscribe(subtitle_sub_);
		subtitle_sub_ = 0;
	}

	subtitle_tracks_ = list;

	if (subtitle_tracks_) {
		subtitle_sub_ = bridge_->subscribe(
			reinterpret_cast<void *>(subtitle_tracks_),
			OAKENGINE_EVENT_SEQUENCE_SUBTITLES_CHANGED);
	}

	update();
}

QPointF
ViewerDisplayWidget::transform_viewer_space_to_buffer_space(const QPointF &pos)
{
	/*
  * Inversion will only fail if the viewer has been scaled by 0 in any direction
  * which I think should never happen.
  */
	return pos * generate_display_transform().inverted();
}

void ViewerDisplayWidget::reset_fps_timer()
{
	fps_timer_start_ = QDateTime::currentMSecsSinceEpoch();
	fps_timer_update_count_ = 0;
	frames_skipped_ = 0;
	frame_rate_average_count_ = 0;

	Core::instance()->clear_status_bar_message();
}

void ViewerDisplayWidget::increment_skipped_frames()
{
	frames_skipped_++;

	Core::instance()->show_status_bar_message(
		tr("%n skipped frame(s) detected during playback", nullptr,
		   frames_skipped_),
		10000);
}

bool ViewerDisplayWidget::eventFilter(QObject *o, QEvent *e)
{
	if (o == this->inner_widget()) {
		switch (e->type()) {
		case QEvent::MouseButtonPress: {
			QMouseEvent *mouse = static_cast<QMouseEvent *>(e);
			if (!(mouse->flags() & Qt::MouseEventCreatedDoubleClick)) {
				if (on_mouse_press(mouse)) {
					return true;
				}
			}
			break;
		}
		case QEvent::MouseMove:
			emit_color_at_cursor(static_cast<QMouseEvent *>(e));
			if (on_mouse_move(static_cast<QMouseEvent *>(e))) {
				return true;
			}
			break;
		case QEvent::MouseButtonRelease:
			if (on_mouse_release(static_cast<QMouseEvent *>(e))) {
				return true;
			}
			break;
		case QEvent::MouseButtonDblClick:
			if (on_mouse_double_click(static_cast<QMouseEvent *>(e))) {
				return true;
			}
			break;
		case QEvent::ShortcutOverride:
		case QEvent::KeyPress:
			if (on_key_press(static_cast<QKeyEvent *>(e))) {
				return true;
			}
			break;
		case QEvent::KeyRelease:
			if (on_key_release(static_cast<QKeyEvent *>(e))) {
				return true;
			}
			break;
		case QEvent::DragEnter: {
			auto drag_enter = static_cast<QDragEnterEvent *>(e);
			if (text_edit_) {
				forward_drag_event_to_text_edit(drag_enter);
			} else {
				emit drag_entered(drag_enter);
			}

			if (drag_enter->isAccepted()) {
				return true;
			}
			break;
		}
		case QEvent::DragMove: {
			auto drag_move = static_cast<QDragMoveEvent *>(e);
			if (text_edit_) {
				forward_drag_event_to_text_edit(drag_move);
			}

			if (drag_move->isAccepted()) {
				return true;
			}
			break;
		}
		case QEvent::DragLeave: {
			auto drag_leave = static_cast<QDragLeaveEvent *>(e);
			if (text_edit_) {
				forward_drag_event_to_text_edit(drag_leave);
			} else {
				emit drag_left(drag_leave);
			}

			if (drag_leave->isAccepted()) {
				return true;
			}
			break;
		}
		case QEvent::Drop: {
			auto drop = static_cast<QDropEvent *>(e);
			if (text_edit_) {
				forward_drag_event_to_text_edit(drop);
			} else {
				emit dropped(drop);
			}

			if (drop->isAccepted()) {
				return true;
			}
			break;
		}
		default:
			break;
		}
	} else if (o == text_edit_) {
		switch (e->type()) {
		case QEvent::Paint:
			update();
			return true;
		default:
			break;
		}
	}

	return super::eventFilter(o, e);
}

namespace {

/**
 * @brief Map oakengine_text_gizmo::vertical_alignment (0=AlignTop,
 * 1=AlignBottom, 2=AlignVCenter; oakengine/gizmo.h) to Qt::Alignment.
 * Must stay in sync with the engine-side mapping in
 * engine/src/capi/gizmo.cpp.
 */
Qt::Alignment text_gizmo_v_align_from_pod(int v)
{
	switch (v) {
	case 1:
		return Qt::AlignBottom;
	case 2:
		return Qt::AlignVCenter;
	default:
		return Qt::AlignTop;
	}
}

/**
 * @brief Fetch the text gizmo POD of a text v3 node at the given time
 * (replaces TextGizmo::get_rect()/get_vertical_alignment() member calls).
 * False when the node has no text gizmo.
 */
bool get_text_gizmo_pod(OakEngineNode *node, const Rational &t,
						oakengine_text_gizmo *out)
{
	return oakengine_text_gizmo_get(node, t.numerator(), t.denominator(),
									out) == OAKENGINE_OK;
}

} // namespace

void ViewerDisplayWidget::on_paint()
{
	const bool backend_neutral = is_backend_neutral();

	QPainter bg_painter;
	bool bg_painter_active = false;

	if (backend_neutral) {
		// Backend-neutral path: draw background directly with QPainter. The
		// image itself will be rendered offscreen, downloaded, and painted below.
		bg_painter.begin(paint_device());
		bg_painter_active = true;
		bg_painter.fillRect(get_inner_rect(), show_widget_background_ ?
												palette().window().color() :
												Qt::black);
	} else {
		// Clear background to empty
		QColor bg_color = show_widget_background_ ? palette().window().color() :
													Qt::black;
		oakengine_display_renderer_clear(renderer(), bg_color.redF(),
										 bg_color.greenF(), bg_color.blueF());
	}

	oak_video_params device_params = {};
	oak_color_transform_job ctj = {};
	bool have_ctj = false;

	// We only draw if we have a pipeline
	if (push_mode_ != k_push_null) {
		// Draw texture through color transform
		device_params = get_viewport_params();

		if (push_mode_ == k_push_blank) {
			if (!backend_neutral) {
				draw_blank(device_params);
			}
		} else if (color_service()) {
			bool drew_backend_neutral_frame = false;

			// Extract handle from QVariant (stored as OakSharedBufferPtr)
			OakSharedBufferPtr buf = load_frame_.value<OakSharedBufferPtr>();
			void *load_handle = buf ? buf->handle : nullptr;
			int load_type = buf ? static_cast<int>(buf->type) : -1;

			if (load_handle && load_type == OakSharedBuffer::k_frame) {
				// CPU frame path: upload to GPU texture
				oak_video_params frame_vp = {};
				oakengine_codec_frame_get_params(load_handle, &frame_vp);
				if (!drew_backend_neutral_frame &&
					(!texture_ ||
					 oakengine_display_texture_renderer(texture_) !=
						 renderer() ||
					 oakengine_display_texture_width(texture_) != frame_vp.width ||
					 oakengine_display_texture_height(texture_) != frame_vp.height ||
					 oakengine_display_texture_format(texture_) != frame_vp.format ||
					 oakengine_display_texture_channel_count(texture_) != 4)) {
					assign_texture(oakengine_display_texture_create(
									   renderer(), &frame_vp,
									   oakengine_codec_frame_data(load_handle),
									   oakengine_codec_frame_linesize(load_handle)),
								   true);
				} else if (!drew_backend_neutral_frame) {
					oakengine_display_texture_upload(
						texture_, oakengine_codec_frame_data(load_handle),
						oakengine_codec_frame_linesize(load_handle));
				}
			} else if (load_handle && load_type == OakSharedBuffer::k_texture) {
				// GPU texture path
				void *src_ren = oakengine_display_texture_renderer(load_handle);
				if (!drew_backend_neutral_frame && load_handle &&
					src_ren && src_ren != renderer()) {
					if (oakengine_display_renderer_is_open_gl(src_ren) &&
						oakengine_display_renderer_is_open_gl(renderer())) {
						assign_texture(load_handle, false);
					} else {
						// Cross-backend: download and re-upload
						void *tmp_frame = oakengine_codec_frame_create();
						oak_video_params tex_params = {};
						oakengine_display_texture_get_params(load_handle, &tex_params);
						oakengine_codec_frame_set_video_params(tmp_frame, &tex_params);
						if (oakengine_codec_frame_allocate(tmp_frame)) {
							oakengine_display_renderer_download_from_texture(
								src_ren,
								oakengine_display_texture_id(load_handle),
								&tex_params,
								oakengine_codec_frame_data(tmp_frame),
								oakengine_codec_frame_linesize(tmp_frame));
							assign_texture(oakengine_display_texture_create(
											   renderer(), &tex_params,
											   oakengine_codec_frame_data(tmp_frame),
											   oakengine_codec_frame_linesize(tmp_frame)),
										   true);
						} else {
							assign_texture(load_handle, false);
						}
						oakengine_codec_frame_free(tmp_frame);
					}
				} else if (!drew_backend_neutral_frame) {
					assign_texture(load_handle, false);
				}
			} else {
				assign_texture(load_custom_texture_from_frame(load_frame_), true);
			}

			if (drew_backend_neutral_frame) {
				assign_texture(nullptr, true);
			}

			emit texture_changed(texture_);

			push_mode_ = k_push_unnecessary;

			if (!drew_backend_neutral_frame) {
				void *texture_to_draw = texture_;

				if (!texture_to_draw ||
					oakengine_display_texture_is_dummy(texture_to_draw)) {
					if (!backend_neutral) {
						draw_blank(device_params);
					}
				} else {
					if (deinterlace_) {
						if (!deinterlace_shader_) {
							QString src = FileFunctions::read_file_as_string(
								QStringLiteral(":/shaders/deinterlace.frag"));
							deinterlace_shader_ =
								oakengine_display_renderer_create_shader(
									renderer(), src.toUtf8().constData(), nullptr);
						}

						if (!deinterlace_texture_ ||
							!oakengine_display_texture_params_equal(
								deinterlace_texture_, texture_to_draw)) {
							if (deinterlace_texture_) {
								oakengine_display_texture_free(deinterlace_texture_);
							}
							oak_video_params tex_params = {};
							oakengine_display_texture_get_params(texture_to_draw,
																 &tex_params);
							deinterlace_texture_ =
								oakengine_display_texture_create(
									renderer(), &tex_params, nullptr, 0);
						}

						oakengine_display_renderer_blit_shader_vec2_to_texture(
							renderer(), deinterlace_shader_, texture_to_draw,
							"resolution_in",
							static_cast<float>(oakengine_display_texture_width(texture_to_draw)),
							static_cast<float>(oakengine_display_texture_height(texture_to_draw)),
							deinterlace_texture_);

						texture_to_draw = deinterlace_texture_;
					}

					// Build color transform job POD
					ctj.processor = color_service().get();
					ctj.input_texture = texture_to_draw;
					ctj.input_alpha_association =
						OAK_CONFIG("ReassocLinToNonLin").toBool() ? 1 : 0;
					ctj.clear_destination = 0;
					ctj.force_opaque = 1;
					memcpy(ctj.matrix, combined_matrix_flipped_.constData(),
						   16 * sizeof(float));
					memcpy(ctj.crop_matrix, crop_matrix_.constData(),
						   16 * sizeof(float));

					have_ctj = true;
				}
			}
		} else {
		}
	}

	if (have_ctj) {
		if (backend_neutral) {
			draw_backend_neutral(ctj, &bg_painter);
		} else {
			oakengine_display_renderer_blit_color_managed(
				renderer(), &ctj, nullptr, &device_params);
		}
	}

	if (bg_painter_active) {
		bg_painter.end();
	}

	// Draw gizmos if we have any
	if (gizmos_) {
		QPainter p(paint_device());

		generate_gizmo_transforms();

		p.setWorldTransform(gizmo_last_draw_transform_);

		OakEngineNode *gizmos_handle =
			gizmos_;
		oakengine_node_update_gizmo_positions(
			gizmos_handle, &gizmo_db_, gizmo_params_.width(),
			gizmo_params_.height(), gizmo_draw_time_.in().numerator(),
			gizmo_draw_time_.in().denominator());
		const int gizmo_count = oakengine_node_gizmo_count(gizmos_handle);
		for (int i = 0; i < gizmo_count; i++) {
			void *gizmo = oakengine_node_gizmo_at(gizmos_handle, i);
			if (oakengine_gizmo_is_visible(gizmo)) {
				oakengine_gizmo_draw(gizmo, &p);
			}
		}

		if (text_edit_) {
			QPixmap pm(text_edit_->width(), text_edit_->height());
			pm.fill(Qt::transparent);

			QPainter pixp(&pm);
			Qt::Alignment v_align = Qt::AlignTop;
			oakengine_text_gizmo tg;
			if (get_text_gizmo_pod(gizmos_handle, get_gizmo_time(), &tg)) {
				v_align = text_gizmo_v_align_from_pod(tg.vertical_alignment);
			}
			text_edit_->paint(&pixp, v_align);

			p.drawPixmap(text_edit_pos_, pm);
		}
	}

	// Draw action/title safe areas
	if (safe_margin_.is_enabled()) {
		QPainter p(paint_device());
		p.setWorldTransform(generate_world_transform());

		p.setPen(QPen(Qt::lightGray, 0));
		p.setBrush(Qt::NoBrush);

		int x = 0, y = 0, w = width(), h = height();

		if (safe_margin_.custom_ratio()) {
			double widget_ar =
				static_cast<double>(width()) / static_cast<double>(height());

			if (widget_ar > safe_margin_.ratio()) {
				// Widget is wider than margins
				w = h * safe_margin_.ratio();
				x = width() / 2 - w / 2;
			} else {
				h = w / safe_margin_.ratio();
				y = height() / 2 - h / 2;
			}
		}

		p.drawRect(w / 20 + x, h / 20 + y, w / 10 * 9, h / 10 * 9);
		p.drawRect(w / 10 + x, h / 10 + y, w / 10 * 8, h / 10 * 8);

		int cross = qMin(w, h) / 32;

		QLine lines[] = {
			QLine(rect().center().x() - cross, rect().center().y(),
				  rect().center().x() + cross, rect().center().y()),
			QLine(rect().center().x(), rect().center().y() - cross,
				  rect().center().x(), rect().center().y() + cross)
		};

		p.drawLines(lines, 2);
	}

	if (show_fps_) {
		{
			qint64 now = QDateTime::currentMSecsSinceEpoch();
			double frame_rate;
			if (now == fps_timer_start_) {
				// This will cause a divide by zero, so we do nothing here
				frame_rate = 0;
			} else {
				frame_rate = double(fps_timer_update_count_) /
							 double((now - fps_timer_start_) / 1000.0);
			}

			if (frame_rate > 0) {
				frame_rate_averages_[frame_rate_average_count_ %
									 frame_rate_averages_.size()] = frame_rate;
				frame_rate_average_count_++;
			}
		}

		if (frame_rate_average_count_ >= frame_rate_averages_.size()) {
			QPainter p(paint_device());

			double average = 0.0;
			for (int i = 0; i < frame_rate_averages_.size(); i++) {
				average += frame_rate_averages_[i];
			}
			average /= double(frame_rate_averages_.size());

			draw_text_with_crude_shadow(
				&p, get_inner_rect(),
				tr("%1 FPS").arg(QString::number(average, 'f', 1)));

			if (frames_skipped_ > 0) {
				draw_text_with_crude_shadow(
					&p,
					get_inner_rect().adjusted(0, p.fontMetrics().height(), 0, 0),
					tr("%1 frames skipped").arg(frames_skipped_));
			}
		}
	}

	// Extraordinarily basic subtitle renderer. Hoping to swap this out with libass at some point.
	draw_subtitle_tracks();

	if (add_band_) {
		QPainter p(paint_device());
		QColor highlight = palette().highlight().color();
		p.setPen(highlight);
		highlight.setAlpha(128);
		p.setBrush(highlight);
		p.drawRect(QRect(add_band_start_, add_band_end_).normalized());
	}

	// In backend-neutral mode there is no native buffer swap, so Qt will not
	// emit frameSwapped automatically. Emit it ourselves so the playback queue
	// keeps advancing (UpdateFromQueue is connected to it during Play()).
	if (backend_neutral) {
		emit frame_swapped();
	}
}

void ViewerDisplayWidget::on_destroy()
{
	if (deinterlace_shader_) {
		oakengine_display_renderer_destroy_shader(renderer(), deinterlace_shader_);
		deinterlace_shader_ = nullptr;
	}
	if (blank_shader_) {
		oakengine_display_renderer_destroy_shader(renderer(), blank_shader_);
		blank_shader_ = nullptr;
	}

	super::on_destroy();

	assign_texture(nullptr, true);
	if (deinterlace_texture_) {
		oakengine_display_texture_free(deinterlace_texture_);
		deinterlace_texture_ = nullptr;
	}
	if (backend_neutral_texture_) {
		oakengine_display_texture_free(backend_neutral_texture_);
		backend_neutral_texture_ = nullptr;
	}
	backend_neutral_buffer_.clear();
	backend_neutral_cpu_image_ = QImage();
	backend_neutral_cpu_display_frame_ = nullptr;
	backend_neutral_cpu_source_frame_ = nullptr;
	backend_neutral_cpu_source_texture_ = nullptr;
	backend_neutral_cpu_color_id_.clear();
	if (load_frame_.isNull()) {
		push_mode_ = k_push_null;
	} else {
		push_mode_ = k_push_frame;
	}
}

QPointF ViewerDisplayWidget::get_texture_position(const QPoint &screen_pos)
{
	return get_texture_position(screen_pos.x(), screen_pos.y());
}

QPointF ViewerDisplayWidget::get_texture_position(const QSize &size)
{
	return get_texture_position(size.width(), size.height());
}

QPointF ViewerDisplayWidget::get_texture_position(const double &x,
												const double &y)
{
	return QPointF(x / gizmo_params_.width(), y / gizmo_params_.height());
}

void ViewerDisplayWidget::draw_text_with_crude_shadow(QPainter *painter,
												  const QRect &rect,
												  const QString &text,
												  const QTextOption &opt)
{
	painter->setPen(Qt::black);
	painter->drawText(rect.adjusted(1, 1, 0, 0), text, opt);
	painter->setPen(Qt::white);
	painter->drawText(rect, text, opt);
}

Rational ViewerDisplayWidget::get_gizmo_time()
{
	// `0` mirrors the engine's Node::k_transform_towards_input ordinal
	// (see timetarget.h — the direction parameter is an int now).
	return get_adjusted_time(get_time_target(), gizmos_, time_, 0);
}

bool ViewerDisplayWidget::is_hand_drag(QMouseEvent *event) const
{
	return event->button() == Qt::MiddleButton ||
		   Core::instance()->tool() == Tool::k_hand;
}

void ViewerDisplayWidget::update_matrix()
{
	combined_matrix_ = scale_matrix_ * translate_matrix_;

	combined_matrix_flipped_ = combined_matrix_;
	// OpenGL's framebuffer origin is bottom-left and texture data is uploaded
	// top-down, so the viewer matrix must flip Y to display images right-side
	// up. Vulkan's framebuffer and texture coordinate origins are both top-left,
	// so the same flip would invert the image. Default to the OpenGL flip when
	// no renderer is available yet.
	if (!renderer() || !oakengine_display_renderer_is_vulkan(renderer())) {
		QMatrix4x4 flip;
		flip.scale(1.0f, -1.0f, 1.0f);
		combined_matrix_flipped_ = flip * combined_matrix_flipped_;
	}

	update();
}

QTransform ViewerDisplayWidget::generate_world_transform()
{
	/*
   * Get matrix elements (roughly) as below in column major order
   *
   * | Sx 0  0  Tx |
   * | 0  Sy 0  Ty |
   * | 0  0  Sz Tz |
   * | 0  0  0  1  |
   */
	float *d = combined_matrix_.data();
	QTransform world;
	// Move corner of canvas to correct point
	world.translate(width() * 0.5 - width() * *(d) * 0.5,
					height() * 0.5 - height() * *(d + 5) * 0.5);
	// Scale
	world.scale(*(d), *(d + 5));
	// Translate for mouse movement
	world.translate(*(d + 12) * width() * 0.5 / *(d),
					*(d + 13) * height() * 0.5 / *(d + 5));

	return world;
}

QTransform ViewerDisplayWidget::generate_display_transform()
{
	QVector2D viewer_scale(get_texture_position(size()));
	QTransform gizmo_transform = generate_world_transform();
	gizmo_transform.scale(viewer_scale.x(), viewer_scale.y());
	gizmo_transform.scale(
		gizmo_params_.pixel_aspect_ratio().flipped().to_double(), 1);
	return gizmo_transform;
}

QTransform ViewerDisplayWidget::generate_gizmo_transform(OakEngineNode *gizmos,
													   OakEngineNode *target,
													   const TimeRange &range)
{
	QTransform t = generate_display_transform();
	if (target) {
		OakEngineNode *target_handle = target;
		// ViewerOutput targets resolve to their connected texture output
		// (replaces dynamic_cast<ViewerOutput*> +
		// get_connected_texture_output()).
		if (oakengine_node_is_viewer_output(target_handle)) {
			if (OakEngineNode *n =
					oakengine_viewer_output_get_connected_texture(
						target_handle)) {
				target_handle = n;
			}
		}

		double m[6];
		oakengine_traverse_transform(
			gizmos, target_handle,
			range.in().numerator(), range.in().denominator(),
			range.out().numerator(), range.out().denominator(), nullptr, m);
		QTransform nt(m[0], m[1], m[2], m[3], m[4], m[5]);

		t.translate(gizmo_params_.width() * 0.5, gizmo_params_.height() * 0.5);
		t.scale(gizmo_params_.width(), gizmo_params_.height());

		t = nt * t;

		t.scale(1.0 / gizmo_params_.width(), 1.0 / gizmo_params_.height());
		t.translate(-gizmo_params_.width() * 0.5,
					-gizmo_params_.height() * 0.5);
	}

	return t;
}

void *ViewerDisplayWidget::try_gizmo_press(const QPointF &p)
{
	if (!gizmos_) {
		return nullptr;
	}

	// The engine-side hit test mirrors the original per-type picking logic
	// (PointGizmo clicking rect / PolygonGizmo containsPoint / PathGizmo
	// contains / ScreenGizmo always hittable) and checks visibility itself;
	// see oakengine/gizmo.h.
	OakEngineNode *gizmos_handle = gizmos_;
	const double transform6[6] = { gizmo_last_draw_transform_.m11(),
								   gizmo_last_draw_transform_.m12(),
								   gizmo_last_draw_transform_.m21(),
								   gizmo_last_draw_transform_.m22(),
								   gizmo_last_draw_transform_.dx(),
								   gizmo_last_draw_transform_.dy() };
	const int gizmo_count = oakengine_node_gizmo_count(gizmos_handle);
	for (int i = gizmo_count - 1; i >= 0; i--) {
		void *gizmo = oakengine_node_gizmo_at(gizmos_handle, i);
		if (oakengine_gizmo_hit_test(gizmo, transform6, p.x(), p.y())) {
			return gizmo;
		}
	}

	return nullptr;
}

void ViewerDisplayWidget::open_text_gizmo(void *text, QMouseEvent *event)
{
	generate_gizmo_transforms();
	OakEngineNode *gizmos_handle = gizmos_;
	oakengine_node_update_gizmo_positions(
		gizmos_handle, &gizmo_db_, gizmo_params_.width(),
		gizmo_params_.height(), gizmo_draw_time_.in().numerator(),
		gizmo_draw_time_.in().denominator());

	active_text_gizmo_ = text;
	text_transform_ = generate_gizmo_transform();
	text_transform_inverted_ = text_transform_.inverted();

	// Create text editor
	text_edit_ = new ViewerTextEditor(text_transform_.m11(), this);

	// Install ourselves as event filter so we can receive the text editor's paint events
	text_edit_->installEventFilter(this);

	// Disable focus on text editor
	text_edit_->setFocusPolicy(Qt::NoFocus);

	// Disable mouse events on text editor
	text_edit_->setAttribute(Qt::WA_TransparentForMouseEvents);

	// "Show" text editor so that it throws paint events, even though its paint event is disabled
	text_edit_->show();

	// Convert HTML to Qt document (fetched through the text gizmo facade)
	QString html;
	{
		const Rational gizmo_time = get_gizmo_time();
		const int html_len = oakengine_text_gizmo_get_html(
			gizmos_handle, gizmo_time.numerator(), gizmo_time.denominator(),
			nullptr, 0);
		if (html_len > 0) {
			QByteArray html_buf(html_len, '\0');
			oakengine_text_gizmo_get_html(
				gizmos_handle, gizmo_time.numerator(),
				gizmo_time.denominator(), html_buf.data(),
				static_cast<int>(html_buf.size()));
			html = QString::fromUtf8(html_buf.constData());
		}
	}
	Html::html_to_doc(text_edit_->document(), html);

	// Connect text change event to propagate back to node
	connect(text_edit_, &ViewerTextEditor::textChanged, this,
			&ViewerDisplayWidget::text_edit_changed);

	// Connect destroyed signal to cleanup after destruction
	connect(text_edit_, &ViewerTextEditor::destroyed, this,
			&ViewerDisplayWidget::text_edit_destroyed);

	// Set text editor's size to logical size
	QRectF text_rect = update_active_text_gizmo_size();

	// Emit text gizmo activation signal
	oakengine_text_gizmo_activated(
		gizmos_);

	// Create toolbar
	text_toolbar_ = new ViewerTextEditorToolBar(text_edit_);
	text_toolbar_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
	connect(text_toolbar_, &ViewerTextEditorToolBar::vertical_alignment_changed,
			this, [this](Qt::Alignment align) {
				oakengine_text_gizmo_set_vertical_alignment(
					gizmos_,
					static_cast<int>(align));
			});
	{
		// The POD carries 0/1/2 (see oakengine/gizmo.h), map it to the real
		// Qt alignment flag before handing it to the toolbar.
		oakengine_text_gizmo _tg;
		Qt::Alignment v_align = Qt::AlignTop;
		if (get_text_gizmo_pod(gizmos_handle, get_gizmo_time(), &_tg)) {
			v_align = text_gizmo_v_align_from_pod(_tg.vertical_alignment);
		}
		text_toolbar_->set_vertical_alignment(
			static_cast<Qt::AlignmentFlag>(static_cast<int>(v_align)));
	}
	text_edit_->connect_tool_bar(text_toolbar_);

	QPoint toolbar_pos =
		mapToGlobal(text_transform_.map(text_edit_pos_).toPoint());
	if (QScreen *screen = qApp->screenAt(toolbar_pos)) {
		// Determine whether to anchor to the top of the rect of the bottom
		if (toolbar_pos.y() - text_toolbar_->height() >=
			screen->geometry().top()) {
			toolbar_pos.setY(toolbar_pos.y() - text_toolbar_->height());
		} else {
			toolbar_pos.setY(
				toolbar_pos.y() +
				text_transform_.map(text_rect).boundingRect().height());
		}

		// Clamp X
		if (toolbar_pos.x() + text_toolbar_->width() >
			screen->geometry().right()) {
			toolbar_pos.setX(screen->geometry().right() -
							 text_toolbar_->width());
		}

		// Clamp Y
		if (toolbar_pos.y() + text_toolbar_->height() >
			screen->geometry().bottom()) {
			toolbar_pos.setY(screen->geometry().bottom() -
							 text_toolbar_->height());
		}
	} else {
		// Fallback
		toolbar_pos.setY(toolbar_pos.y() - text_toolbar_->height());
	}

	text_toolbar_->move(toolbar_pos);
	text_toolbar_->show();

	// Allow widget to take keyboard focus
	inner_widget()->setFocusPolicy(Qt::StrongFocus);
	inner_widget()->setMouseTracking(true);

	connect(qApp, &QApplication::focusChanged, this,
			&ViewerDisplayWidget::focus_changed);

	// Start text cursor where the user clicked
	if (event) {
		QPoint click_pos = text_transform_inverted_.map(event->position().toPoint()) -
						   text_edit_pos_.toPoint();
		text_edit_->setTextCursor(text_edit_->cursorForPosition(click_pos));
	}

	// Grab focus back from the toolbar
	connect(text_toolbar_, &ViewerTextEditorToolBar::first_paint, this, [this] {
		Core::instance()->main_window()->activateWindow();
		inner_widget()->setFocus();
	});
}

bool ViewerDisplayWidget::on_mouse_press(QMouseEvent *event)
{
	if (is_hand_drag(event)) {
		// Handle hand drag
		hand_last_drag_pos_ = event->position().toPoint();
		hand_dragging_ = true;
		emit hand_drag_started();
		inner_widget()->setCursor(Qt::ClosedHandCursor);

		return true;

	} else if (text_edit_ && forward_mouse_event_to_text_edit(event, true)) {
		return true;

	} else if (event->button() == Qt::LeftButton) {
		if (Core::instance()->tool() == Tool::k_add &&
			(Core::instance()->get_selected_addable_object() ==
				 Tool::k_addable_shape ||
			 Core::instance()->get_selected_addable_object() ==
				 Tool::k_addable_title)) {
			add_band_start_ = event->position().toPoint();
			add_band_end_ = add_band_start_;
			add_band_ = true;

		} else if ((current_gizmo_ = try_gizmo_press(
						gizmo_last_draw_transform_inverted_.map(
							event->position().toPoint())))) {
			// Handle gizmo click
			gizmo_start_drag_ = event->position().toPoint();
			gizmo_last_drag_ = gizmo_start_drag_;
			const TimeRange gizmo_time = generate_gizmo_time();
			oakengine_gizmo_set_globals(
				current_gizmo_, gizmo_params_.width(), gizmo_params_.height(),
				gizmo_time.in().numerator(), gizmo_time.in().denominator());

		} else {
			// Handle standard drag
			emit drag_started(event->position().toPoint());
		}

		return true;
	}

	return false;
}

bool ViewerDisplayWidget::on_mouse_move(QMouseEvent *event)
{
	// Handle hand dragging
	if (hand_dragging_) {
		// Emit movement
		emit hand_drag_moved(event->position().toPoint().x() - hand_last_drag_pos_.x(),
						   event->position().toPoint().y() - hand_last_drag_pos_.y());

		hand_last_drag_pos_ = event->position().toPoint();

		return true;

	} else if (text_edit_ && forward_mouse_event_to_text_edit(event)) {
		return true;

	} else if (add_band_) {
		add_band_end_ = event->position().toPoint();
		update();
		return true;

	} else if (current_gizmo_) {
		// Signal movement
		int drag_behavior = oakengine_gizmo_get_drag_value_behavior(current_gizmo_);
		if (drag_behavior >= 0) {
			if (!gizmo_drag_started_) {
				QPointF start = screen_to_scene_point(gizmo_start_drag_);

				Rational gizmo_time = get_gizmo_time();
				oakengine_traverse_generate_row(
					gizmos_,
					gizmo_time.numerator(), gizmo_time.denominator(),
					(gizmo_time + gizmo_params_.frame_rate_as_time_base())
						.numerator(),
					(gizmo_time + gizmo_params_.frame_rate_as_time_base())
						.denominator(),
					nullptr, 0, 0, &gizmo_db_);

				oakengine_gizmo_drag_start(current_gizmo_, &gizmo_db_,
					start.x(), start.y(), gizmo_time.numerator(),
					gizmo_time.denominator());
				gizmo_drag_started_ = true;
			}

			QPointF v = screen_to_scene_point(event->position().toPoint());
			switch (drag_behavior) {
			case 1:
				v -= screen_to_scene_point(gizmo_last_drag_);
				gizmo_last_drag_ = event->position().toPoint();
				break;
			case 2:
				v -= screen_to_scene_point(gizmo_start_drag_);
				break;
			}

			oakengine_gizmo_drag_move(current_gizmo_, v.x(), v.y(),
				static_cast<int>(event->modifiers()));

			return true;
		}
	}

	return false;
}

bool ViewerDisplayWidget::on_mouse_release(QMouseEvent *e)
{
	if (hand_dragging_) {
		// Handle hand drag
		emit hand_drag_ended();
		hand_dragging_ = false;
		update_cursor();

		return true;

	} else if (text_edit_ && forward_mouse_event_to_text_edit(e)) {
		return true;

	} else if (add_band_) {
		QRect band_rect = QRect(add_band_start_, add_band_end_).normalized();
		if (band_rect.width() > 1 && band_rect.height() > 1) {
			QRectF r = generate_display_transform().inverted().mapRect(band_rect);
			emit create_addable_at(r);
		}

		add_band_ = false;
		return true;

	} else if (current_gizmo_) {
		// Handle gizmo
		if (gizmo_drag_started_) {
			void *command = oakengine_undo_command_create_multi();
			oakengine_gizmo_drag_end(current_gizmo_, command);
			oakengine_undo_push(command, tr("Dragged Gizmo").toUtf8().constData());
			gizmo_drag_started_ = false;
		}
		current_gizmo_ = nullptr;

		return true;
	}

	return false;
}

bool ViewerDisplayWidget::on_mouse_double_click(QMouseEvent *event)
{
	if (text_edit_ && forward_mouse_event_to_text_edit(event)) {
		return true;
	} else if (event->button() == Qt::LeftButton && gizmos_) {
		QPointF ptr = transform_viewer_space_to_buffer_space(event->position().toPoint());
		OakEngineNode *gizmos_handle =
			gizmos_;
		// A text gizmo only exists on a text v3 node, so the node type id is
		// the predicate (replaces dynamic_cast<TextGizmo*>). The id string
		// must stay in sync with TextGeneratorV3::id()
		// (engine/node/generator/text/textv3.cpp). The rect comes from the
		// text gizmo POD; the pointer handed to open_text_gizmo() is only an
		// opaque marker (the engine resolves the text gizmo from the node).
		if (viewer_output_node_type_is(gizmos_handle,
									   "org.olivevideoeditor.Olive.text3")) {
			oakengine_text_gizmo tg;
			if (get_text_gizmo_pod(gizmos_handle, get_gizmo_time(), &tg) &&
				QRectF(tg.rect_x, tg.rect_y, tg.rect_w, tg.rect_h)
					.contains(ptr)) {
				open_text_gizmo(oakengine_node_gizmo_at(gizmos_handle, 0),
								event);
				return true;
			}
		}
	}

	return false;
}

bool ViewerDisplayWidget::on_key_press(QKeyEvent *e)
{
	if (text_edit_) {
		if (e->key() == Qt::Key_Escape) {
			close_text_editor();
			return true;
		} else {
			return forward_event_to_text_edit(e);
		}
	}
	return false;
}

bool ViewerDisplayWidget::on_key_release(QKeyEvent *e)
{
	if (text_edit_) {
		return forward_event_to_text_edit(e);
	}
	return false;
}

void ViewerDisplayWidget::emit_color_at_cursor(QMouseEvent *e)
{
	// Do this no matter what, emits signal to any pixel samplers
	if (signal_cursor_color_) {
		Color reference, display;

		if (texture_) {
			QPointF pixel_pos =
				generate_display_transform().inverted().map(e->position().toPoint());
			oak_video_params tp = {};
			oakengine_display_texture_get_params(texture_, &tp);
			pixel_pos /= (tp.divider > 0 ? tp.divider : 1);

			make_current();

			double rgba[4] = {};
			oakengine_display_renderer_get_pixel(
				renderer(), texture_,
				static_cast<int>(pixel_pos.x()),
				static_cast<int>(pixel_pos.y()), rgba);
			reference = Color(rgba[0], rgba[1], rgba[2], rgba[3]);
			if (color_service()) {
				display = oak_convert_color(color_service(), reference);
			} else {
				display = reference;
			}
		}

		emit cursor_color(reference, display);
	}
}

void ViewerDisplayWidget::draw_subtitle_tracks()
{
	if (!show_subtitles_ || !subtitle_tracks_) {
		return;
	}

	OakEngineSequence *subtitle_seq = subtitle_tracks_;
	int subtitle_track_count = 0;
	oakengine_sequence_track_count(subtitle_seq, nullptr, nullptr,
								   &subtitle_track_count);
	if (subtitle_track_count <= 0) {
		return;
	}

	// Scale font size by transform
	QTransform display_transform = generate_display_transform();
	qreal font_sz = OAK_CONFIG("DefaultSubtitleSize").toInt();
	font_sz *= display_transform.m11();
	if (qIsNaN(font_sz)) {
		return;
	}

	QPainterPath path;

	QTransform transform = generate_world_transform();
	QRect bounding_box = transform.mapRect(rect());

	QFont f;
	f.setPointSizeF(font_sz);

	QString family = OAK_CONFIG("DefaultSubtitleFamily").toString();
	if (!family.isEmpty()) {
		f.setFamily(family);
	}

	f.setWeight(static_cast<QFont::Weight>(
		OAK_CONFIG("DefaultSubtitleWeight").toInt()));

	bounding_box.adjust(bounding_box.width() / 10, bounding_box.height() / 10,
						-bounding_box.width() / 10,
						-bounding_box.height() / 10);

	QFontMetrics fm(f);

	// visible_block_at_time takes a frame timestamp in the sequence's
	// frame-rate timebase (see oakengine/timeline.h).
	const int64_t time_ts = core::Timecode::time_to_timestamp(
		time_, sequence_timebase(subtitle_seq));

	for (int j = subtitle_track_count - 1; j >= 0; j--) {
		if (!oakengine_track_is_muted(subtitle_seq,
									  OAKENGINE_TRACK_TYPE_SUBTITLE, j)) {
			OakEngineTrack *sub_track = oakengine_sequence_track_at(
				subtitle_seq, OAKENGINE_TRACK_TYPE_SUBTITLE, j);
			OakEngineBlock *sub =
				oakengine_track_visible_block_at_time(sub_track, time_ts);
			// Subtitle predicate: SubtitleBlock::id() string compare
			// (replaces dynamic_cast<SubtitleBlock*>); must stay in sync
			// with engine/node/block/subtitle/subtitle.cpp.
			if (sub && viewer_output_node_type_is(
						   sub, "org.olivevideoeditor.Olive.subtitle")) {
				// Split into lines
				char text_buf[4096];
				const int len = oakengine_subtitle_get_text(
					reinterpret_cast<OakEngineNode *>(sub), text_buf,
					sizeof(text_buf));
				QStringList list = QtUtils::word_wrap_string(
					QString::fromUtf8(text_buf, len), fm,
					bounding_box.width());

				for (int i = list.size() - 1; i >= 0; i--) {
					int w = QtUtils::q_font_metrics_width(fm, list.at(i));
					path.addText(bounding_box.width() / 2 - w / 2,
								 bounding_box.height() -
									 fm.height() * (list.size() - i) +
									 fm.ascent(),
								 f, list.at(i));
				}
			}
		}
	}

	bool antialias = OAK_CONFIG("AntialiasSubtitles").toBool();

	QPixmap *aa_pixmap;
	QPainter *text_painter;
	if (antialias) {
		// QPainter only supports anti-aliasing in software, so to achieve it, we draw to a
		// software buffer first and then draw that onto the hardware
		aa_pixmap = new QPixmap(bounding_box.width(), bounding_box.height());
		aa_pixmap->fill(Qt::transparent);
		text_painter = new QPainter(aa_pixmap);
	} else {
		// Just draw straight to the hardware
		text_painter = new QPainter(paint_device());

		// Offset path by however much is necessary
		path.translate(bounding_box.x(), bounding_box.y());
	}

	text_painter->setPen(QPen(Qt::black, f.pointSizeF() / 16));
	text_painter->setBrush(Qt::white);
	text_painter->setRenderHint(QPainter::Antialiasing);

	text_painter->drawPath(path);

	delete text_painter;

	if (antialias) {
		// We just drew to a software buffer, now draw this image onto the hardware device
		QPainter p(paint_device());
		p.drawPixmap(bounding_box.x(), bounding_box.y(), *aa_pixmap);
		delete aa_pixmap;
	}
}

template <typename T> void ViewerDisplayWidget::forward_drag_event_to_text_edit(T *e)
{
	// HACK: Absolutely filthy hack. We need to be able to transform the mouse coordinates for our
	//       proxied QTextEdit, however unlike QMouseEvents, Qt's drag events don't allow modifying
	//       the position after construction. Unhelpfully, Qt also explicitly forbids users creating
	//       their own drag events because they "rely on Qt's internal state". So in order to forward
	//       drag events, we defy this by creating our own events, but DON'T process them through Qt's
	//       event queue and instead just send them directly to the widget (requiring its protected
	//       drag events to be made public). That way Qt stays happy, because as far as it's
	//       concerned it's only interfacing with this widget, and the QTextEdit gets to receive
	//       transformed events. It's a terrible hack, but seems to work.

	if constexpr (std::is_same_v<T, QDragLeaveEvent>) {
		text_edit_->dragLeaveEvent(e);
	} else {
		T relay(adjust_pos_by_v_align(get_virtual_pos_for_text_edit(e->position().toPoint())).toPoint(),
				e->possibleActions(), e->mimeData(), e->buttons(),
				e->modifiers());

		if (e->type() == QEvent::DragEnter) {
			text_edit_->dragEnterEvent(static_cast<QDragEnterEvent *>(&relay));
		} else if (e->type() == QEvent::DragMove) {
			text_edit_->dragMoveEvent(static_cast<QDragMoveEvent *>(&relay));
		} else if (e->type() == QEvent::Drop) {
			text_edit_->dropEvent(&relay);
		}

		if (relay.isAccepted()) {
			e->accept();
		}
	}
}

bool ViewerDisplayWidget::forward_mouse_event_to_text_edit(QMouseEvent *event,
													  bool check_if_outside)
{
	if (current_gizmo_) {
		return false;
	}

	// Transform screen mouse coords to world mouse coords
	QPointF local_pos = get_virtual_pos_for_text_edit(event->position().toPoint());

	if (event->type() == QEvent::MouseMove &&
		event->buttons() == Qt::NoButton) {
		QPointF mapped =
			text_transform_inverted_.map(event->position().toPoint()) - text_edit_pos_;
		if (mapped.x() >= 0 && mapped.y() >= 0 &&
			mapped.x() < text_edit_->width() &&
			mapped.y() < text_edit_->height()) {
			inner_widget()->setCursor(Qt::IBeamCursor);
		} else {
			inner_widget()->unsetCursor();
		}
	}

	if (check_if_outside) {
		if (local_pos.x() < 0 || local_pos.x() >= text_edit_->width() ||
			local_pos.y() < 0 || local_pos.y() >= text_edit_->height()) {
			// Allow clicking other gizmos so the user can resize while the text editor is active
			if ((current_gizmo_ = try_gizmo_press(
					 gizmo_last_draw_transform_inverted_.map(event->position().toPoint())))) {
				return false;
			} else {
				close_text_editor();
				return true;
			}
		}
	}

	local_pos = adjust_pos_by_v_align(local_pos);

	QMouseEvent derived(event->type(), local_pos, event->scenePosition(),
						event->globalPosition(), event->button(), event->buttons(),
						event->modifiers(), event->source());
	return forward_event_to_text_edit(&derived);
}

bool ViewerDisplayWidget::forward_event_to_text_edit(QEvent *event)
{
	qApp->sendEvent(text_edit_->viewport(), event);
	bool e = event->isAccepted();
	if (e) {
		update();
	}
	return e;
}

QPointF ViewerDisplayWidget::adjust_pos_by_v_align(QPointF p)
{
	Qt::Alignment v_align = Qt::AlignTop;
	oakengine_text_gizmo tg;
	if (get_text_gizmo_pod(gizmos_,
						   get_gizmo_time(), &tg)) {
		v_align = text_gizmo_v_align_from_pod(tg.vertical_alignment);
	}

	switch (v_align) {
	case Qt::AlignTop:
		// Do nothing
		break;
	case Qt::AlignVCenter:
		p.setY(p.y() - text_edit_->height() / 2 +
			   text_edit_->document()->size().height() / 2);
		break;
	case Qt::AlignBottom:
		p.setY(p.y() - text_edit_->height() +
			   text_edit_->document()->size().height());
		break;
	}

	return p;
}

void ViewerDisplayWidget::close_text_editor()
{
	text_edit_->deleteLater();
	text_edit_ = nullptr;

	active_text_gizmo_ = nullptr;
}

void ViewerDisplayWidget::generate_gizmo_transforms()
{
	gizmo_draw_time_ = generate_gizmo_time();

	if (gizmos_) {
		oakengine_traverse_generate_row(
			gizmos_,
			gizmo_draw_time_.in().numerator(),
			gizmo_draw_time_.in().denominator(),
			gizmo_draw_time_.out().numerator(),
			gizmo_draw_time_.out().denominator(), nullptr, 0, 0, &gizmo_db_);
	}

	gizmo_last_draw_transform_ = generate_gizmo_transform(
		gizmos_, get_time_target(), gizmo_draw_time_);
	gizmo_last_draw_transform_inverted_ = gizmo_last_draw_transform_.inverted();
}

void ViewerDisplayWidget::draw_blank(const oak_video_params &device_params)
{
	if (!blank_shader_) {
		blank_shader_ = oakengine_display_renderer_create_blank_shader(renderer());
	}

	oakengine_display_renderer_blit_blank(
		renderer(), blank_shader_,
		combined_matrix_flipped_.constData(),
		crop_matrix_.constData(),
		&device_params);
}

bool ViewerDisplayWidget::draw_backend_neutral_frame(void *frame,
												  QPainter *painter)
{
	if (!frame || !oakengine_codec_frame_is_allocated(frame) || !painter ||
		!painter->isActive() || !color_service()) {
		return false;
	}

	const QString color_id = oak_query_string([this](char *buf, int size) {
		return oakengine_color_processor_id(color_service().get(), buf, size);
	});
	if (backend_neutral_cpu_source_frame_ == frame &&
		backend_neutral_cpu_color_id_ == color_id &&
		!backend_neutral_cpu_image_.isNull()) {
		painter->save();
		painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter->setWorldTransform(generate_world_transform(), false);
		painter->drawImage(rect(), backend_neutral_cpu_image_);
		painter->restore();
		return true;
	}

	// Do not run OCIO CPU conversion from paintEvent. Some OCIO processors are
	// not safe to apply on this GUI path and a crash here kills preview. Worker
	// frames tagged with display:<processor-id> have already been color managed;
	// untagged frames are drawn directly as a safe fallback.
	const int frame_fmt = oakengine_codec_frame_format(frame);
	const int frame_ch = oakengine_codec_frame_channel_count(frame);
	const int frame_w = oakengine_codec_frame_width(frame);
	const int frame_h = oakengine_codec_frame_height(frame);
	const int frame_ls = oakengine_codec_frame_linesize_bytes(frame);
	const char *frame_data =
		reinterpret_cast<const char *>(oakengine_codec_frame_const_data(frame));

	QImage source_image;
	if (frame_fmt == PixelFormat::u8 && frame_ch == 4) {
		backend_neutral_cpu_display_frame_ = frame;
		backend_neutral_cpu_image_ = QImage(
			reinterpret_cast<const uchar *>(frame_data), frame_w, frame_h,
			frame_ls, QImage::Format_RGBA8888);
		source_image = backend_neutral_cpu_image_;
	} else if (frame_fmt == PixelFormat::u8 && frame_ch == 3) {
		backend_neutral_cpu_display_frame_ = frame;
		backend_neutral_cpu_image_ = QImage(
			reinterpret_cast<const uchar *>(frame_data), frame_w, frame_h,
			frame_ls, QImage::Format_RGB888);
		source_image = backend_neutral_cpu_image_;
	} else {
		backend_neutral_cpu_display_frame_ = nullptr;
		const int bpp =
			oakengine_video_params_bytes_per_pixel(frame_fmt, frame_ch);
		if (backend_neutral_cpu_image_.size() != QSize(frame_w, frame_h) ||
			backend_neutral_cpu_image_.format() != QImage::Format_RGBA8888) {
			backend_neutral_cpu_image_ =
				QImage(frame_w, frame_h, QImage::Format_RGBA8888);
		}

		for (int y = 0; y < frame_h; ++y) {
			uchar *dst = backend_neutral_cpu_image_.scanLine(y);
			const char *src = frame_data + y * frame_ls;
			for (int x = 0; x < frame_w; ++x) {
				Color c(src + x * bpp,
						static_cast<PixelFormat::Format>(frame_fmt), frame_ch);
				dst[x * 4 + 0] =
					static_cast<uchar>(qBound(0, int(c.red() * 255.0), 255));
				dst[x * 4 + 1] =
					static_cast<uchar>(qBound(0, int(c.green() * 255.0), 255));
				dst[x * 4 + 2] =
					static_cast<uchar>(qBound(0, int(c.blue() * 255.0), 255));
				dst[x * 4 + 3] = 255;
			}
		}
		source_image = backend_neutral_cpu_image_;
	}

	backend_neutral_cpu_source_frame_ = frame;
	backend_neutral_cpu_color_id_ = color_id;

	painter->save();
	painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter->setWorldTransform(generate_world_transform(), false);
	painter->drawImage(rect(), source_image);
	painter->restore();
	return true;
}

bool ViewerDisplayWidget::draw_backend_neutral_texture(void *texture,
													QPainter *painter)
{
	if (!texture || oakengine_display_texture_is_dummy(texture) ||
		!oakengine_display_texture_renderer(texture) || !painter ||
		!painter->isActive() || !color_service()) {
		return false;
	}

	const QString color_id = oak_query_string([this](char *buf, int size) {
		return oakengine_color_processor_id(color_service().get(), buf, size);
	});
	if (backend_neutral_cpu_source_texture_ == texture &&
		backend_neutral_cpu_color_id_ == color_id &&
		!backend_neutral_cpu_image_.isNull()) {
		painter->save();
		painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter->setWorldTransform(generate_world_transform(), false);
		painter->drawImage(rect(), backend_neutral_cpu_image_);
		painter->restore();
		return true;
	}

	void *tmp_frame = oakengine_codec_frame_create();
	oak_video_params tex_params = {};
	oakengine_display_texture_get_params(texture, &tex_params);
	oakengine_codec_frame_set_video_params(tmp_frame, &tex_params);
	if (!oakengine_codec_frame_allocate(tmp_frame)) {
		oakengine_codec_frame_free(tmp_frame);
		return false;
	}

	oakengine_display_texture_download(texture,
									   oakengine_codec_frame_data(tmp_frame),
									   oakengine_codec_frame_linesize(tmp_frame));

	if (!draw_backend_neutral_frame(tmp_frame, painter)) {
		oakengine_codec_frame_free(tmp_frame);
		return false;
	}

	backend_neutral_cpu_source_texture_ = texture;
	backend_neutral_cpu_color_id_ = color_id;
	oakengine_codec_frame_free(tmp_frame);
	return true;
}

// Renders a backend-neutral frame by drawing into an offscreen backend texture,
// downloading it to CPU memory, then painting that image with QPainter.
void ViewerDisplayWidget::draw_backend_neutral(const oak_color_transform_job &ctj,
											 QPainter *painter)
{
	if (!painter || !painter->isActive()) {
		return;
	}

	const int texture_width = static_cast<int>(width() * devicePixelRatioF());
	const int texture_height = static_cast<int>(height() * devicePixelRatioF());

	oak_video_params offscreen_pod = {};
	offscreen_pod.width = texture_width;
	offscreen_pod.height = texture_height;
	offscreen_pod.format = PixelFormat::u8;

	if (!backend_neutral_texture_ ||
		oakengine_display_texture_width(backend_neutral_texture_) != texture_width ||
		oakengine_display_texture_height(backend_neutral_texture_) != texture_height) {
		// The offscreen texture is sized in device pixels so high-DPI widgets
		// draw one downloaded pixel per device pixel after setDevicePixelRatio().
		if (backend_neutral_texture_) {
			oakengine_display_texture_free(backend_neutral_texture_);
		}
		backend_neutral_texture_ = oakengine_display_texture_create(
			renderer(), &offscreen_pod, nullptr, 0);
		backend_neutral_buffer_.resize(
			texture_width * texture_height *
			oakengine_video_params_bytes_per_pixel(0, // PixelFormat::u8
										  4));
	}

	if (!backend_neutral_texture_ ||
		oakengine_display_texture_is_dummy(backend_neutral_texture_)) {
		return;
	}

	oak_color_transform_job local_ctj = ctj;
	local_ctj.clear_destination = 1;

	// Reuse the normal color-management shader path, but render into a texture
	// instead of an OpenGL widget framebuffer.
	oakengine_display_renderer_blit_color_managed(
		renderer(), &local_ctj, backend_neutral_texture_, nullptr);

	oakengine_display_texture_download(backend_neutral_texture_,
									   backend_neutral_buffer_.data(), 0);

	const int bytes_per_pixel = oakengine_video_params_bytes_per_pixel(0, 4); // u8, RGBA

	QImage img(
		reinterpret_cast<const uchar *>(backend_neutral_buffer_.constData()),
		texture_width, texture_height, texture_width * bytes_per_pixel,
		QImage::Format_RGBA8888_Premultiplied);
	img.setDevicePixelRatio(devicePixelRatioF());

	// QImage references backend_neutral_buffer_ directly; draw it before the
	// buffer can be resized or reused by a later paint.
	painter->drawImage(QPoint(0, 0), img);
}

void ViewerDisplayWidget::set_show_fps(bool e)
{
	show_fps_ = e;

	update();
}

void ViewerDisplayWidget::request_start_editing_text()
{
	if (gizmos_) {
		OakEngineNode *gizmos_handle =
			gizmos_;
		// Same text v3 type-id predicate as the double-click path (replaces
		// dynamic_cast<TextGizmo*>); must stay in sync with
		// TextGeneratorV3::id() (engine/node/generator/text/textv3.cpp).
		if (viewer_output_node_type_is(gizmos_handle,
									   "org.olivevideoeditor.Olive.text3")) {
			open_text_gizmo(oakengine_node_gizmo_at(gizmos_handle, 0));
		}
	}
}

void ViewerDisplayWidget::play(const int64_t &start_timestamp,
							   const int &playback_speed,
							   const Rational &timebase, bool start_updating)
{
	playback_timebase_ = timebase;
	playback_speed_ = playback_speed;

	timer_.start(start_timestamp, playback_speed, timebase.to_double());

	if (start_updating) {
		connect(this, &ViewerDisplayWidget::frame_swapped, this,
				&ViewerDisplayWidget::update_from_queue);

		update();
	}
}

void ViewerDisplayWidget::pause()
{
	disconnect(this, &ViewerDisplayWidget::frame_swapped, this,
			   &ViewerDisplayWidget::update_from_queue);

	queue_.clear();
	queue_starved_ = false;
}

QPointF ViewerDisplayWidget::screen_to_scene_point(const QPoint &p)
{
	if (gizmo_last_draw_transform_.isIdentity()) {
		generate_gizmo_transforms();
	}

	return p * gizmo_last_draw_transform_inverted_;
}

void ViewerDisplayWidget::update_from_queue()
{
	int64_t t = timer_.get_timestamp_now();

	Rational time = Timecode::timestamp_to_time(t, playback_timebase_);

	if (qEnvironmentVariableIsSet("OAK_DEBUG_PLAYBACK")) {
		qWarning("PLAYBACK-DEBUG: update_from_queue t=%lld time=%lld/%lld qlen=%d front_ts=%s",
				 (long long)t, (long long)time.numerator(),
				 (long long)time.denominator(), int(queue_.size()),
				 queue_.empty() ? "-" : qPrintable(QStringLiteral("%1/%2").arg(
												 queue_.front().timestamp.numerator())
											 .arg(
												 queue_.front().timestamp.denominator())));
	}

	bool popped = false;

	if (queue_.empty()) {
		queue_starved_ = true;
		emit queue_starved();
	} else {
		while (!queue_.empty()) {
			const ViewerPlaybackFrame &pf = queue_.front();

			if (pf.timestamp == time) {
				// Frame was in queue, no need to decode anything
				set_image(pf.frame);

				if (queue_starved_) {
					queue_starved_ = false;
					emit queue_no_longer_starved();
				}
				return;

			} else if ((pf.timestamp > time) == (playback_speed_ > 0)) {
				// The next frame in the queue is too new, so just do a regular update. Either the
				// frame we want will arrive in time, or we'll just have to skip it.
				break;

			} else {
				queue_.pop_front();

				if (popped) {
					// We've already popped a frame in this loop, meaning a frame has been skipped
					increment_skipped_frames();
				} else {
					// Shown a frame and progressed to the next one
					increment_frame_count();
					popped = true;
				}

				if (queue_.empty()) {
					queue_starved_ = true;
					emit queue_starved();
					break;
				}
			}
		}
	}

	update();
}

void ViewerDisplayWidget::text_edit_changed()
{
	ViewerTextEditor *editor = static_cast<ViewerTextEditor *>(sender());

	QString html = Html::doc_to_html(editor->document());
	oakengine_text_gizmo_update_html(
		gizmos_,
		html.toUtf8().constData(),
		get_gizmo_time().numerator(), get_gizmo_time().denominator());
}

void ViewerDisplayWidget::text_edit_destroyed()
{
	oakengine_text_gizmo_deactivated(
		gizmos_);
	text_edit_ = nullptr;
	text_toolbar_ = nullptr;
	inner_widget()->setMouseTracking(false);
	inner_widget()->setFocusPolicy(Qt::NoFocus);
	update_cursor();
	disconnect(qApp, &QApplication::focusChanged, this,
			   &ViewerDisplayWidget::focus_changed);
}

void ViewerDisplayWidget::subtitles_changed(const TimeRange &r)
{
	if (time_ >= r.in() && time_ < r.out()) {
		update();
	}
}

void ViewerDisplayWidget::focus_changed(QWidget *old, QWidget *now)
{
	if (!now) {
		// Ignore this
		return;
	}

	bool unfocused = true;

	while (now) {
		if (now == text_toolbar_ || now == this) {
			unfocused = false;
			break;
		} else {
			now = now->parentWidget();
		}
	}

	if (unfocused) {
		close_text_editor();
	}
}

QRectF ViewerDisplayWidget::update_active_text_gizmo_size()
{
	QRectF text_rect;
	oakengine_text_gizmo tg;
	if (get_text_gizmo_pod(gizmos_,
						   get_gizmo_time(), &tg)) {
		text_rect = QRectF(tg.rect_x, tg.rect_y, tg.rect_w, tg.rect_h);
	}
	text_edit_pos_ = text_rect.topLeft();
	text_edit_->setGeometry(text_rect.toRect());
	return text_rect;
}

}
