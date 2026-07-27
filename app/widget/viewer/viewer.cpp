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

#include "viewer.h"

#include <QDateTime>
#include <QFontDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QResizeEvent>
#include <QScreen>
#include <QtMath>
#include <QVBoxLayout>

#include "audio/audiomanager.h"
#include "oakengine/audio.h"
#include "oakengine/encoding.h"
#include "oakengine/project.h"
#include "olive/core/oakcore/audioparams.h"
#include "dialog/ratiodialog.h"
#include "common/configwrapper.h"
#include "core.h"
#include "engineeventbridge.h"
#include "node/block/gap/gap.h"
#include "oakengine/encoding.h"
#include "oakengine/display.h"
#include "widget/viewer/displaybuffer.h"
#include "oakengine/viewer.h"
#include "oakengine/videoparams.h"
#include "node/generator/shape/shapenodebase.h"
#include "node/project.h"
#include "panel/multicam/multicampanel.h"
#include "panel/panelmanager.h"
#include "oakengine/preview.h"
#include "oakengine/renderer.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "olive/core/oakcore/samplebuffer.h"
#include "olive/core/render/samplebuffer.h"
#include "codec/frame.h"
#include <cstring>
#include "viewerpreventsleep.h"
#include "widget/audiomonitor/audiomonitor.h"
#include "widget/menu/menu.h"
#include "widget/multicam/multicamdisplay.h"
#include "widget/timelinewidget/tool/add.h"
#include "widget/timeruler/timeruler.h"

#include "widget/viewer/vieweroutpututils.h"
namespace olive
{

#define super TimeBasedWidget

QVector<ViewerWidget *> ViewerWidget::instances;

namespace {

// Convert olive::core::AudioParams to a temporary OakAudioParams*.
// The caller must free the result with oakcore_audioparams_free().
OakAudioParams *ap_to_oak(const olive::core::AudioParams &ap)
{
	return oakcore_audioparams_create(
		ap.sample_rate(),
		ap.channel_layout(),
		static_cast<int>(ap.format()));
}

// Extract a SampleBuffer from an OakEnginePreviewRequest's audio result.
// Returns an unallocated (null) buffer on failure.
static SampleBuffer preview_req_to_sample_buffer(OakEnginePreviewRequest *req)
{
	OakSampleBuffer *sb = oakcore_samplebuffer_create();
	if (oakengine_preview_request_has_result(req)) {
		int channels = oakengine_preview_request_get_audio_channel_count(req);
		int sample_rate = oakengine_preview_request_get_audio_sample_rate(req);
		if (channels > 0 && sample_rate > 0) {
			OakAudioParams *ap = oakcore_audioparams_create(sample_rate, 0, 0);
			oakcore_samplebuffer_set_audio_params(sb, ap);
			oakcore_audioparams_free(ap);

			// Determine sample count from first channel
			int n = oakengine_preview_request_get_audio_samples(req, 0, nullptr, 1 << 30);
			if (n > 0) {
				oakcore_samplebuffer_set_sample_count(sb, size_t(n));
				oakcore_samplebuffer_allocate(sb);
				for (int ch = 0; ch < channels; ch++) {
					float *dst = oakcore_samplebuffer_data(sb, ch);
					oakengine_preview_request_get_audio_samples(req, ch, dst, n);
				}
			}
		}
	}
	return SampleBuffer::from_handle(sb);
}

// Bridge from OakEnginePreviewRequest C callback to ViewerWidget main-thread
// slot.  Each trampoline calls the appropriate method on the widget; the
// method finds the completed request by scanning its list.

static void nonqueue_finished_cb(void *userdata)
{
	QMetaObject::invokeMethod(static_cast<ViewerWidget *>(userdata),
							  "renderer_generated_frame", Qt::QueuedConnection);
}

static void queue_finished_cb(void *userdata)
{
	QMetaObject::invokeMethod(static_cast<ViewerWidget *>(userdata),
							  "renderer_generated_frame_for_queue", Qt::QueuedConnection);
}

static void audio_playback_finished_cb(void *userdata)
{
	QMetaObject::invokeMethod(
		static_cast<ViewerWidget *>(userdata),
		"received_audio_buffer_for_playback", Qt::QueuedConnection);
}

static void audio_scrub_finished_cb(void *userdata)
{
	QMetaObject::invokeMethod(
		static_cast<ViewerWidget *>(userdata),
		"received_audio_buffer_for_scrubbing", Qt::QueuedConnection);
}

static void dry_run_finished_cb(void *userdata)
{
	QMetaObject::invokeMethod(static_cast<ViewerWidget *>(userdata),
							  "dry_run_finished", Qt::QueuedConnection);
}

} // namespace

// NOTE: Hardcoded interval of size of audio chunk to render and send to the output at a time.
//       We want this to be as long as possible so the code has plenty of time to send the audio
//       while also being as short as possible so users get relatively immediate feedback when
//       changing values. 1/4 second seems to be a good middleground.
const Rational ViewerWidget::k_audio_playback_interval = Rational(1, 4);

const Rational k_video_playback_interval = Rational(1, 10);

ViewerWidget::ViewerWidget(ViewerDisplayWidget *display, QWidget *parent)
	: super(false, true, parent)
	, playback_speed_(0)
	, color_menu_enabled_(true)
	, time_changed_from_timer_(false)
	, prequeuing_video_(false)
	, prequeuing_audio_(0)
	, record_armed_(false)
	, recording_(false)
	, first_requeue_watcher_(nullptr)
	, enable_audio_scrubbing_(true)
	, waveform_mode_(k_wf_automatic)
	, ignore_scrub_(0)
	, multicam_panel_(nullptr)
	, bridge_(new EngineEventBridge(this))
	, audio_processor_(oakengine_audio_processor_create())
	, overlay_(nullptr)
	, info_chip_(nullptr)
	, safe_frame_btn_(nullptr)
	, overlay_zoom_index_(5)
{
	// Set up main layout
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	// Create main OpenGL-based view and sizer
	sizer_ = new ViewerSizer();
	layout->addWidget(sizer_);

	display_widget_ = display;
	display_widget_->set_show_widget_background(true);
	playback_devices_.append(display_widget_);
	connect(display_widget_, &ViewerDisplayWidget::customContextMenuRequested,
			this, &ViewerWidget::show_context_menu);
	connect(display_widget_, &ViewerDisplayWidget::cursor_color, this,
			&ViewerWidget::cursor_color);
	connect(display_widget_, &ViewerDisplayWidget::color_processor_changed, this,
			&ViewerWidget::color_processor_changed);
	connect(
		display_widget_, &ViewerDisplayWidget::color_processor_changed, this,
		[](ColorProcessorHandlePtr processor) {
			oakengine_render_cache_set_display_color_processor(
				processor ? processor.get() : nullptr);
		});
	oakengine_render_cache_set_display_color_processor(
		display_widget_->get_current_color_processor().get());
	connect(display_widget_, &ViewerDisplayWidget::color_manager_changed, this,
			&ViewerWidget::color_manager_changed);
	connect(display_widget_, &ViewerDisplayWidget::drag_entered, this,
			&ViewerWidget::drag_entered);
	connect(display_widget_, &ViewerDisplayWidget::dropped, this,
			&ViewerWidget::dropped);
	connect(display_widget_, &ViewerDisplayWidget::texture_changed, this,
			&ViewerWidget::texture_changed);
	connect(display_widget_, &ViewerDisplayWidget::queue_starved, this,
			&ViewerWidget::queue_starved);
	connect(display_widget_, &ViewerDisplayWidget::queue_no_longer_starved, this,
			&ViewerWidget::queue_no_longer_starved);
	connect(display_widget_, &ViewerDisplayWidget::create_addable_at, this,
			&ViewerWidget::create_addable_at);
	connect(sizer_, &ViewerSizer::request_scale, display_widget_,
			&ViewerDisplayWidget::set_matrix_zoom);
	connect(sizer_, &ViewerSizer::request_translate, display_widget_,
			&ViewerDisplayWidget::set_matrix_translate);
	connect(display_widget_, &ViewerDisplayWidget::hand_drag_moved, sizer_,
			&ViewerSizer::hand_drag_move);
	sizer_->set_widget(display_widget_);

	// Create the design-reference overlay (info chip + zoom/safe-frame buttons)
	create_overlay();

	// Make the display widget the first tabbable widget. While the viewer display cannot actually
	// be interacted with by tabbing, it prevents the actual first tabbable widget (the playhead
	// slider in `controls_`) from getting auto-focused any time the panel is maximized (with `)
	display_widget_->setFocusPolicy(Qt::TabFocus);

	// Create waveform view when audio is connected and video isn't
	waveform_view_ = new AudioWaveformView();
	connect_timeline_view(waveform_view_);
	layout->addWidget(waveform_view_);

	// Per the Oak UI design reference the viewer no longer shows its own time
	// ruler: it duplicates the timeline's ruler and clutters the viewer. The
	// ruler object still exists (created by TimeBasedWidget) and continues to
	// back marker/work-area/snapping logic and the scrollbar; we simply do not
	// add it to the visible layout. Scrubbing is provided by the transport
	// controls and the scrollbar below.
	// layout->addWidget(ruler());

	// Create scrollbar
	layout->addWidget(scrollbar());

	// Create lower controls
	controls_ = new PlaybackControls();
	controls_->set_timecode_enabled(true);
	controls_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
	connect(controls_, &PlaybackControls::play_clicked, this,
			static_cast<void (ViewerWidget::*)()>(&ViewerWidget::play));
	connect(controls_, &PlaybackControls::pause_clicked, this,
			&ViewerWidget::pause);
	connect(controls_, &PlaybackControls::prev_frame_clicked, this,
			&ViewerWidget::prev_frame);
	connect(controls_, &PlaybackControls::next_frame_clicked, this,
			&ViewerWidget::next_frame);
	connect(controls_, &PlaybackControls::begin_clicked, this,
			&ViewerWidget::go_to_start);
	connect(controls_, &PlaybackControls::end_clicked, this,
			&ViewerWidget::go_to_end);
	layout->addWidget(controls_);

	// FIXME: Magic number
	SetScale(48.0);

	// Ensures that seeking on the waveform view updates the time as expected
	connect(waveform_view_, &AudioWaveformView::customContextMenuRequested,
			this, &ViewerWidget::show_context_menu);

	connect(&playback_backup_timer_, &QTimer::timeout, this,
			&ViewerWidget::playback_timer_update);

	set_auto_max_scroll_bar(true);

	instances.append(this);

	update_waveform_view_from_mode();

	connect(Core::instance(), &Core::color_picker_enabled, this,
			&ViewerWidget::set_signal_cursor_color_enabled);
	connect(this, &ViewerWidget::cursor_color, Core::instance(),
			&Core::color_picker_color_emitted);
	connect(bridge_, &EngineEventBridge::audio_output_params_changed, this,
			&ViewerWidget::update_audio_processor);
}

ViewerWidget::~ViewerWidget()
{
	instances.removeOne(this);

	auto windows = windows_;

	foreach (ViewerWindow *window, windows) {
		delete window;
	}

	delete display_widget_;
	display_widget_ = nullptr;

	oakengine_audio_processor_free(audio_processor_);
	audio_processor_ = nullptr;
}

void ViewerWidget::TimeChangedEvent(const Rational &time)
{
	if (!time_changed_from_timer_) {
		pause_internal();
	}

	if (record_armed_) {
		disarm_recording();
	}

	controls_->set_time(time);

	if (get_connected_node() && last_time_ != time) {
		if (!is_playing()) {
			update_texture_from_node();

			push_scrubbed_audio();

			// We don't clear the FPS timer on pause in case users want to see it immediately after, but by
			// the time a new texture is drawn, assume that the FPS no longer needs to be shown.
			display_widget_->reset_fps_timer();
		}

		display_widget_->set_time(time);
	}

	// Send time to auto-cacher
	oakengine_preview_cacher_set_playhead(time.numerator(), time.denominator());

	last_time_ = time;
}

void ViewerWidget::ConnectNodeEvent(ViewerOutput *n)
{
	OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(n);

	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_SIZE_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_LENGTH_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_INTERLACING_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_VIDEO_PARAMS_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_AUDIO_PARAMS_CHANGED);
	bridge_->subscribe(handle, OAKENGINE_EVENT_VIEWER_TEXTURE_INPUT_CHANGED);

	connect(bridge_, &EngineEventBridge::viewer_size_changed, this,
			[this](OakEngineNode *, int w, int h) {
				set_viewer_resolution(w, h);
			});
	connect(bridge_, &EngineEventBridge::viewer_pixel_aspect_changed, this,
			[this](OakEngineNode *, qint64 num, qint64 den) {
				set_viewer_pixel_aspect(Rational(num, den));
			});
	connect(bridge_, &EngineEventBridge::viewer_length_changed, this,
			[this](OakEngineNode *, qint64 num, qint64 den) {
				length_changed_slot(Rational(num, den));
			});
	connect(bridge_, &EngineEventBridge::viewer_interlacing_changed, this,
			[this](OakEngineNode *, int mode) {
				interlacing_changed_slot(mode);
			});
	connect(bridge_, &EngineEventBridge::viewer_video_params_changed, this,
			&ViewerWidget::update_renderer_video_parameters);
	connect(bridge_, &EngineEventBridge::viewer_video_params_changed, this,
			&ViewerWidget::update_texture_from_node, Qt::QueuedConnection);
	connect(bridge_, &EngineEventBridge::viewer_audio_params_changed, this,
			&ViewerWidget::update_renderer_audio_parameters);
	connect(bridge_, &EngineEventBridge::viewer_texture_input_changed, this,
			&ViewerWidget::update_waveform_view_from_mode);

	// FrameHashCache invalidated events
	if (OakEngineFrameCache *cache = oakengine_viewer_get_frame_cache(handle)) {
		bridge_->subscribe(cache, OAKENGINE_EVENT_FRAME_CACHE_INVALIDATED);
		connect(bridge_, &EngineEventBridge::frame_cache_invalidated, this,
				[this](void *, qint64 a, qint64 b) {
					viewer_invalidated_video_range(
						TimeRange(Rational(a, 1), Rational(b, 1)));
				});
	}

	// Connect controls to set_playhead via facade
	connect(controls_, &PlaybackControls::time_changed, this,
			[handle](const Rational &time) {
				oakengine_viewer_set_playhead(
					handle, time.numerator(), time.denominator());
			});

	oak_video_params vp;
	oakengine_viewer_get_video_params(handle, 0, &vp);

	interlacing_changed_slot(vp.interlacing);

	ruler()->set_playback_cache(
		reinterpret_cast<PlaybackCache *>(
			oakengine_viewer_get_playback_cache(handle)));

	set_viewer_resolution(vp.width, vp.height);
	set_viewer_pixel_aspect(Rational(vp.pixel_aspect_num, vp.pixel_aspect_den));
	last_length_ = 0;

	{
		int64_t len_num, len_den;
		oakengine_viewer_get_length(handle, &len_num, &len_den);
		length_changed_slot(Rational(len_num, len_den));
	}

	update_audio_processor();

	OakEngineColorManager *color_manager = oak_color_manager(n->project()->color_manager());

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->connect_color_manager(color_manager);
	}

	update_waveform_view_from_mode();

	waveform_view_->set_viewer(get_connected_node());

	update_renderer_video_parameters();
	update_renderer_audio_parameters();

	// Set texture to new texture (or null if no viewer node is available)
	update_texture_from_node();
}

void ViewerWidget::DisconnectNodeEvent(ViewerOutput *n)
{
	pause_internal();

	// Disconnect all bridge signal connections to this receiver
	disconnect(bridge_, nullptr, this, nullptr);

	// Unsubscribe all bridge subscriptions
	// (EngineEventBridge does not expose bulk unsubscribe; the bridge
	//  is per-widget so stale subscriptions are harmless until the next
	//  ConnectNodeEvent, but we clear the ones we know about)
	// For now the subscription callbacks filter by source handle internally.

	timeline_selected_blocks_.clear();
	node_view_selected_.clear();
	if (multicam_panel_) {
		multicam_panel_->set_multicam_node(nullptr, nullptr, nullptr,
										 Rational::na_n);
	}

	close_audio_processor();
	audio_scrub_watchers_.clear();

	set_display_image(nullptr);

	ruler()->set_playback_cache(nullptr);

	// Effectively disables the viewer and clears the state
	set_viewer_resolution(0, 0);

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->disconnect_color_manager();
	}

	waveform_view_->set_viewer(nullptr);

	// Queue an UpdateStack so that when it runs, the viewer node will be fully disconnected
	QMetaObject::invokeMethod(this, &ViewerWidget::update_waveform_view_from_mode,
							  Qt::QueuedConnection);

	set_gizmos(nullptr);
}

void ViewerWidget::ConnectedNodeChangeEvent(ViewerOutput *n)
{
	display_widget_->set_subtitle_tracks(dynamic_cast<Sequence *>(n));
}

void ViewerWidget::ConnectedWorkAreaChangeEvent(TimelineWorkArea *workarea)
{
	waveform_view_->set_work_area(workarea);
}

void ViewerWidget::ConnectedMarkersChangeEvent(TimelineMarkerList *markers)
{
	waveform_view_->set_markers(markers);
}

void ViewerWidget::ScaleChangedEvent(const double &s)
{
	super::ScaleChangedEvent(s);

	waveform_view_->set_scale(s);
}

void ViewerWidget::resizeEvent(QResizeEvent *event)
{
	super::resizeEvent(event);

	update_minimum_scale();

	position_overlay();
}

OakEnginePreviewRequest *ViewerWidget::get_single_frame(const Rational &t,
														  bool dry)
{
	OakEngineNode *viewer =
		reinterpret_cast<OakEngineNode *>(get_connected_node());
	return oakengine_preview_request_single_frame(
		viewer, t.numerator(), t.denominator(), dry ? 1 : 0);
}

void ViewerWidget::toggle_play_pause()
{
	if (is_playing()) {
		pause();
	} else {
		play();
	}
}

bool ViewerWidget::is_playing() const
{
	return playback_speed_ != 0;
}

void ViewerWidget::set_color_menu_enabled(bool enabled)
{
	color_menu_enabled_ = enabled;
}

void ViewerWidget::set_matrix(const QMatrix4x4 &mat)
{
	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_matrix_crop(mat);
	}
}

void ViewerWidget::set_full_screen(QScreen *screen)
{
	if (!screen) {
		// Try to find the screen that contains the mouse cursor currently
		foreach (QScreen *test, QGuiApplication::screens()) {
			if (test->geometry().contains(QCursor::pos())) {
				screen = test;
				break;
			}
		}

		// Fallback, just use the first screen
		if (!screen) {
			screen = QGuiApplication::screens().first();
		}
	}

	if (windows_.contains(screen)) {
		ViewerWindow *vw = windows_.take(screen);
		vw->deleteLater();
		return;
	}

	ViewerWindow *vw = new ViewerWindow(this);

	vw->setGeometry(screen->geometry());
	vw->showFullScreen();
	vw->display_widget()->connect_color_manager(color_manager());
	connect(vw, &ViewerWindow::destroyed, this,
			&ViewerWidget::window_about_to_close);
	connect(vw->display_widget(),
			&ViewerDisplayWidget::customContextMenuRequested, this,
			&ViewerWidget::show_context_menu);

	if (get_connected_node()) {
		vw->set_video_params(viewer_output_video_params(get_connected_node()));
		vw->display_widget()->set_deinterlacing(
			vw->display_widget()->is_deinterlacing());
	}

	vw->display_widget()->set_image(
		QVariant::fromValue(oak_make_shared_texture(
			oakengine_display_texture_retain(
				display_widget()->get_current_texture()))));

	playback_devices_.append(vw->display_widget());

	(*vw->display_widget()->queue()) = *playback_devices_.first()->queue();
	if (is_playing()) {
		vw->display_widget()->play(get_timestamp(), playback_speed_, timebase(),
								   true);
	}

	windows_.insert(screen, vw);
}

void ViewerWidget::cache_entire_sequence()
{
	oakengine_preview_cacher_force_cache_range(
		reinterpret_cast<OakEngineNode *>(get_connected_node()),
		0, 1,
		get_connected_node()->get_video_length().numerator(),
		get_connected_node()->get_video_length().denominator());
}

void ViewerWidget::cache_sequence_in_out()
{
	if (get_connected_node() && get_connected_node()->get_work_area()->enabled()) {
		const auto &r = get_connected_node()->get_work_area()->range();
		oakengine_preview_cacher_force_cache_range(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			r.in().numerator(), r.in().denominator(),
			r.out().numerator(), r.out().denominator());
	} else {
		QMessageBox::warning(this, tr("Error"),
							 tr("No in or out points are set to cache."),
							 QMessageBox::Ok);
	}
}

void ViewerWidget::set_gizmos(Node *node)
{
	display_widget_->set_time_target(get_connected_node());
	display_widget_->set_gizmos(node);
}

void ViewerWidget::start_capture(TimelineWidget *source, const TimeRange &time,
								const Track::Reference &track)
{
	oakengine_viewer_set_playhead(
		reinterpret_cast<OakEngineNode *>(get_connected_node()),
		time.in().numerator(), time.in().denominator());
	arm_for_recording();

	recording_callback_ = source;
	recording_range_ = time;
	recording_track_ = track;
}

void ViewerWidget::connect_multicam_widget(MulticamWidget *p)
{
	if (multicam_panel_) {
		disconnect(multicam_panel_, &MulticamWidget::switched, this,
				   &ViewerWidget::detect_multicam_node_now);
	}

	multicam_panel_ = p;

	if (multicam_panel_) {
		connect(multicam_panel_, &MulticamWidget::switched, this,
				&ViewerWidget::detect_multicam_node_now);
	}
}

bool ViewerWidget::should_force_waveform() const
{
	return get_connected_node() &&
		   !get_connected_node()->get_connected_texture_output() &&
		   get_connected_node()->get_connected_sample_output();
}

void ViewerWidget::set_empty_image()
{
	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_blank();
	}
}

void ViewerWidget::update_auto_cacher()
{
	Rational t = get_connected_node()->get_playhead();
	oakengine_preview_cacher_set_playhead(t.numerator(), t.denominator());
}

void ViewerWidget::decrement_prequeued_audio()
{
	prequeuing_audio_--;
	if (!prequeuing_audio_) {
		finish_play_preprocess();
	}
}

void ViewerWidget::arm_for_recording()
{
	controls_->start_play_blink();
	record_armed_ = true;
}

void ViewerWidget::disarm_recording()
{
	controls_->stop_play_blink();
	record_armed_ = false;
}

void ViewerWidget::update_audio_processor()
{
	if (get_connected_node()) {
		close_audio_processor();

		AudioParams ap = viewer_output_audio_params(get_connected_node());
		if (ap.sample_rate() <= 0 || ap.channel_count() <= 0) {
			ap = AudioParams(
				OAK_CONFIG("DefaultSequenceAudioFrequency").toInt(),
				OAK_CONFIG("DefaultSequenceAudioLayout").toULongLong(),
				static_cast<SampleFormat::Format>(
					oakengine_viewer_default_sample_format()));
		}
		ap.set_format(
			static_cast<SampleFormat::Format>(
				oakengine_viewer_default_sample_format()));

		AudioParams packed(
			OAK_CONFIG("AudioOutputSampleRate").toInt(),
			OAK_CONFIG("AudioOutputChannelLayout").toULongLong(),
			SampleFormat::from_string(OAK_CONFIG("AudioOutputSampleFormat")
										  .toString()
										  .toStdString()));

		qDebug() << "ViewerWidget::UpdateAudioProcessor: from sample_rate="
				 << ap.sample_rate() << "channels=" << ap.channel_count()
				 << "layout_mask=0x" << Qt::hex << ap.channel_layout()
				 << "to sample_rate=" << packed.sample_rate()
				 << "channels=" << packed.channel_count()
				 << "layout_mask=0x" << packed.channel_layout()
				 << Qt::dec;

		OakAudioParams *from = ap_to_oak(ap);
		OakAudioParams *to = ap_to_oak(packed);
		oakengine_audio_processor_open(
			audio_processor_, from, to,
			(playback_speed_ == 0) ? 1 : std::abs(playback_speed_));
		oakcore_audioparams_free(from);
		oakcore_audioparams_free(to);
	}
}

void ViewerWidget::create_addable_at(const QRectF &f)
{
	if (Sequence *s = dynamic_cast<Sequence *>(get_connected_node())) {
		Track::Type type = Track::k_video;
		int track_index = -1;
		TrackList *list = s->track_list(type);
		const Rational &in = get_connected_node()->get_playhead();
		Rational length = OAK_CONFIG("DefaultStillLength").value<Rational>();
		Rational out = in + length;

		// Find a free track where we won't overwrite anything
		while (true) {
			track_index++;

			if (track_index >= list->get_track_count()) {
				// Just create a new track
				break;
			}

			Track *track = list->get_track_at(track_index);
			if (track->is_locked()) {
				continue;
			}

			Block *b = track->nearest_block_before_or_at(in);
			if (!b || (dynamic_cast<GapBlock *>(b) && b->out() >= out)) {
				break;
			}
		}

		void *command = oakengine_undo_command_create_multi();
		Node *clip = AddTool::create_addable_clip(
			command, s, Track::Reference(type, track_index), in, length);

		if (ShapeNodeBase *shape = dynamic_cast<ShapeNodeBase *>(clip)) {
			const VideoParams vp = viewer_output_video_params(s);
			oak_video_params pod = {};
			pod.width = vp.width();
			pod.height = vp.height();
			pod.time_base_num = vp.time_base().numerator();
			pod.time_base_den = vp.time_base().denominator();
			pod.format = vp.format();
			pod.pixel_aspect_num = vp.pixel_aspect_ratio().numerator();
			pod.pixel_aspect_den = vp.pixel_aspect_ratio().denominator();
			pod.interlacing = vp.interlacing();
			pod.divider = vp.divider();
			oakengine_shape_set_rect_undoable(
				reinterpret_cast<OakEngineNode *>(shape), f.x(), f.y(),
				f.width(), f.height(), &pod, command);
		}

		oakengine_undo_push(command, tr("Created Shape").toUtf8().constData());
		set_gizmos(clip);
	}
}

void ViewerWidget::handle_first_requeue_destroy()
{
	// Extra protection to ensure we don't reference a destroyed object
	if (first_requeue_watcher_ && !queue_watchers_.contains(first_requeue_watcher_)) {
		first_requeue_watcher_ = nullptr;
	}
}

void ViewerWidget::show_subtitle_properties()
{
	QFont f(OAK_CONFIG("DefaultSubtitleFamily").toString(),
			OAK_CONFIG("DefaultSubtitleSize").toInt(),
			OAK_CONFIG("DefaultSubtitleWeight").toInt());
	QFontDialog fd(f, this);

	if (fd.exec() == QDialog::Accepted) {
		f = fd.selectedFont();
		OAK_CONFIG("DefaultSubtitleSize") = f.pointSize();
		OAK_CONFIG("DefaultSubtitleFamily") = f.family();
		OAK_CONFIG("DefaultSubtitleWeight") = f.weight();
		display_widget_->update();
	}
}

void ViewerWidget::dry_run_finished()
{
	if (!dry_run_watchers_.isEmpty()) {
		OakEnginePreviewRequest *req = dry_run_watchers_.takeFirst();
		oakengine_preview_request_free(req);
		request_next_dry_run();
	}
}

void ViewerWidget::request_next_dry_run()
{
	if (is_playing()) {
		Rational next_time =
			Timecode::timestamp_to_time(dry_run_next_frame_, timebase());
		if (frame_exists_at_time(next_time)) {
			if (next_time > get_connected_node()->get_playhead() +
								Rational(10)) {
				QTimer::singleShot(timebase().to_double() / playback_speed_,
								   this, &ViewerWidget::request_next_dry_run);
			} else {
				OakEnginePreviewRequest *r = get_single_frame(next_time, true);
				if (r) {
					oakengine_preview_request_set_finished_callback(
						r, dry_run_finished_cb, this);
					dry_run_next_frame_ += playback_speed_;
					dry_run_watchers_.append(r);
				}
			}
		}
	}
}

void ViewerWidget::save_frame_as_image()
{
	Core::instance()->open_export_dialog_for_viewer(
		reinterpret_cast<OakEngineNode *>(get_connected_node()), true);
}

void ViewerWidget::detect_multicam_node_now()
{
	if (get_connected_node()) {
		detect_multicam_node(get_connected_node()->get_playhead());
	}
}

void ViewerWidget::close_audio_processor()
{
	oakengine_audio_processor_close(audio_processor_);
}

void ViewerWidget::set_waveform_mode(WaveformMode wf)
{
	waveform_mode_ = wf;
	update_waveform_view_from_mode();
}

void ViewerWidget::detect_multicam_node(const Rational &time)
{
	// Look for multicam node
	MultiCamNode *multicam = nullptr;
	ClipBlock *clip = nullptr;

	// Faster way to do this
	if (multicam_panel_ && multicam_panel_->isVisible()) {
		if (Sequence *s = dynamic_cast<Sequence *>(get_connected_node())) {
			// Prefer selected nodes
			for (Node *n : qAsConst(node_view_selected_)) {
				if ((multicam = dynamic_cast<MultiCamNode *>(n))) {
					// Found multicam, now try to find corresponding clip from selected timeline blocks
					for (Block *b : qAsConst(timeline_selected_blocks_)) {
						if (ClipBlock *c = dynamic_cast<ClipBlock *>(b)) {
							if (c->range().contains(time) &&
								c->context_contains_node(multicam)) {
								clip = c;
								break;
							}
						}
					}
					break;
				}
			}

			// Next, prefer multicam from selected block
			if (!multicam) {
				for (Block *b : qAsConst(timeline_selected_blocks_)) {
					if (b->range().contains(time)) {
						if ((clip = dynamic_cast<ClipBlock *>(b))) {
							if ((multicam = reinterpret_cast<MultiCamNode *>(
									oakengine_clip_find_multicam(
										reinterpret_cast<OakEngineNode *>(clip))))) {
								break;
							}
						}
					}
				}
			}

			if (!multicam) {
				const QVector<Track *> &tracks = s->get_tracks();
				for (Track *t : tracks) {
					if (t->is_locked()) {
						continue;
					}

					Block *b = t->nearest_block_before_or_at(time);
					if ((clip = dynamic_cast<ClipBlock *>(b))) {
						if ((multicam = reinterpret_cast<MultiCamNode *>(
								oakengine_clip_find_multicam(
									reinterpret_cast<OakEngineNode *>(clip))))) {
							break;
						}
					}
				}
			}
		}
	}

	if (multicam) {
		if (multicam_panel_) {
			multicam_panel_->set_multicam_node(get_connected_node(), multicam, clip,
											 time);
		}
		// FIXME: Really dirty
		oakengine_render_cache_set_multicam_node(
			reinterpret_cast<OakEngineNode *>(multicam));
	} else {
		oakengine_render_cache_set_multicam_node(nullptr);
		if (multicam_panel_) {
			multicam_panel_->set_multicam_node(nullptr, nullptr, nullptr, time);
		}
	}
}

bool ViewerWidget::is_video_visible() const
{
	return viewer_output_video_params(get_connected_node()).video_type() !=
			   1 &&
		   (display_widget_->isVisible() || !windows_.isEmpty());
}

void ViewerWidget::update_waveform_view_from_mode()
{
	bool prefer_waveform = should_force_waveform();

	sizer_->setVisible(waveform_mode_ == k_wf_viewer_and_waveform ||
					   waveform_mode_ == k_wf_viewer_only ||
					   (waveform_mode_ == k_wf_automatic && !prefer_waveform));
	waveform_view_->setVisible(
		waveform_mode_ == k_wf_viewer_and_waveform ||
		waveform_mode_ == k_wf_waveform_only ||
		(waveform_mode_ == k_wf_automatic && prefer_waveform));

	waveform_view_->setSizePolicy(QSizePolicy::Expanding,
								  waveform_mode_ == k_wf_viewer_and_waveform ?
									  QSizePolicy::Maximum :
									  QSizePolicy::Expanding);

	if (get_connected_node()) {
		oakengine_viewer_set_waveform_enabled(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			waveform_view_->isVisible() ? 1 : 0);

		if (waveform_view_->isVisible()) {
			waveform_view_->set_viewer(get_connected_node());
		} else {
			waveform_view_->set_viewer(nullptr);
		}
	}
}

void ViewerWidget::queue_next_audio_buffer()
{
	Rational queue_end =
		audio_playback_queue_time_ + (k_audio_playback_interval * playback_speed_);

	// Clamp queue end by zero and the audio length
	queue_end = std::clamp(queue_end, Rational(0),
						   get_connected_node()->get_audio_length());
	if ((playback_speed_ > 0 && queue_end <= audio_playback_queue_time_) ||
		(playback_speed_ < 0 && queue_end >= audio_playback_queue_time_)) {
		// This will queue nothing, so stop the loop here
		if (prequeuing_audio_) {
			decrement_prequeued_audio();
		}
		return;
	}

	OakEngineNode *viewer = reinterpret_cast<OakEngineNode *>(get_connected_node());
	OakEnginePreviewRequest *req = oakengine_preview_request_audio_range(
		viewer,
		audio_playback_queue_time_.numerator(),
		audio_playback_queue_time_.denominator(),
		queue_end.numerator(), queue_end.denominator());
	if (req) {
		oakengine_preview_request_set_finished_callback(
			req, audio_playback_finished_cb, this);
		audio_playback_queue_.push_back(req);
	}

	audio_playback_queue_time_ = queue_end;
}

void ViewerWidget::received_audio_buffer_for_playback()
{
	while (!audio_playback_queue_.empty() &&
		   oakengine_preview_request_is_done(audio_playback_queue_.front())) {
		OakEnginePreviewRequest *req = audio_playback_queue_.front();
		audio_playback_queue_.pop_front();

		if (oakengine_preview_request_has_result(req)) {
			SampleBuffer samples = preview_req_to_sample_buffer(req);
			if (samples.is_allocated()) {
				// If the samples must be reversed, reverse them now
				if (playback_speed_ < 0) {
					samples.reverse();
				}

				// Convert to packed data for audio output
				const void *pack_data = nullptr;
				int pack_size = 0;
				int r = oakengine_audio_processor_convert(
					audio_processor_, samples.to_raw_ptrs().data(),
					samples.sample_count(), &pack_data, &pack_size);

				// TempoProcessor may have emptied the array
				if (r >= 0) {
					if (pack_size > 0) {
						if (prequeuing_audio_) {
							// Add to prequeued audio buffer
							prequeued_audio_.append(
								static_cast<const char *>(pack_data), pack_size);
						} else {
							// Push directly to audio manager
							{
								OakAudioParams *oap =
									oakengine_audio_processor_output_params(
										audio_processor_);
								oakengine_audio_push_to_output(
									oap, static_cast<const char *>(pack_data),
									pack_size, nullptr, 0);
								oakcore_audioparams_free(oap);
							}
						}
					}
				} else {
					qCritical() << "Failed to process audio for playback:" << r;
				}
			}
		}

		if (prequeuing_audio_) {
			decrement_prequeued_audio();
		}

		oakengine_preview_request_free(req);
	}
}

void ViewerWidget::received_audio_buffer_for_scrubbing()
{
	if (audio_scrub_watchers_.empty()) {
		return;
	}

	OakEnginePreviewRequest *req = audio_scrub_watchers_.front();
	audio_scrub_watchers_.pop_front();

	if (oakengine_preview_request_has_result(req)) {
		SampleBuffer samples = preview_req_to_sample_buffer(req);
		if (samples.is_allocated()) {
			if (samples.audio_params().channel_count() > 0) {
				const void *pack_data = nullptr;
				int pack_size = 0;
				int r = oakengine_audio_processor_convert(
					audio_processor_, samples.to_raw_ptrs().data(),
					samples.sample_count(), &pack_data, &pack_size);

				if (r >= 0) {
					if (pack_size > 0) {
						QString error;
						oakengine_audio_clear_buffered_output();
						char errbuf[256];
						OakAudioParams *oap =
							oakengine_audio_processor_output_params(
								audio_processor_);
						if (!oakengine_audio_push_to_output(
								oap, static_cast<const char *>(pack_data),
								pack_size, errbuf, sizeof(errbuf))) {
							oakcore_audioparams_free(oap);
							Core::instance()->show_status_bar_message(
								tr("Audio scrubbing failed: %1").arg(QString::fromUtf8(errbuf)));
						} else {
							oakcore_audioparams_free(oap);
						}
						AudioMonitor::push_sample_buffer_on_all(samples);
					}
				} else {
					qCritical()
						<< "Failed to process audio for scrubbing:" << r;
				}
			}
		}
	}

	oakengine_preview_request_free(req);
}

void ViewerWidget::queue_starved()
{
	static const int k_maximum_wait_time_ms = 250;
	static const Rational k_maximum_wait_time(k_maximum_wait_time_ms, 1000);
	qint64 now = QDateTime::currentMSecsSinceEpoch();

	if (!queue_starved_start_) {
		queue_starved_start_ = now;
	} else if (now > queue_starved_start_ + k_maximum_wait_time_ms) {
		if (first_requeue_watcher_ && !queue_watchers_.isEmpty()) {
			// Stale check: request still pending below timeout
			return;
		}

		force_requeue_from_current_time();
		queue_starved_start_ = 0;
	}
}

void ViewerWidget::queue_no_longer_starved()
{
	queue_starved_start_ = 0;
}

void ViewerWidget::force_requeue_from_current_time()
{
	// Defer the requeue to the next event-loop iteration. This function is often
	// called from paintEvent paths (QueueStarved) where synchronously cancelling
	// watchers can re-enter the same request and deadlock.
	QMetaObject::invokeMethod(
		this, [this]() { force_requeue_from_current_time_internal(); },
		Qt::QueuedConnection);
}

void ViewerWidget::force_requeue_from_current_time_internal()
{
	// Allow half a second for requeue to complete
	static const Rational k_requeue_wait_time(1);

	oakengine_preview_cacher_clear_single_frame_renders(0);
	queue_watchers_.clear();
	int queue = determine_playback_queue_size();
	playback_queue_next_frame_ =
		get_timestamp() +
		playback_speed_ * Timecode::time_to_timestamp(
							  k_requeue_wait_time, timebase(), Timecode::k_floor);
	;
	first_requeue_watcher_ = nullptr;
	for (int i = 0; i < queue; i++) {
		OakEnginePreviewRequest *req = request_next_frame_for_queue();
		if (!first_requeue_watcher_) {
			first_requeue_watcher_ = req;
		}
	}
}

void ViewerWidget::update_texture_from_node()
{
	if (!get_connected_node()) {
		return;
	}

	if (is_playing()) {
		qWarning() << "UpdateTextureFromNode called while playing";
		return;
	}

	Rational time = get_connected_node()->get_playhead();
	bool frame_exists = frame_exists_at_time(time);
	bool frame_might_be_still = viewer_might_be_a_still();

	if (frame_exists || frame_might_be_still) {
		// Frame was not in queue, will require rendering or decoding from cache
		// Not playing, run a task to get the frame either from the cache or the renderer
		OakEnginePreviewRequest *req = get_frame(time);
		if (req) {
			oakengine_preview_request_set_finished_callback(
				req, nonqueue_finished_cb, this);
			nonqueue_watchers_.append(req);
		}

		// Clear queue because we want this frame more than any others
		oakengine_preview_cacher_clear_single_frame_renders(1);

		detect_multicam_node(time);
	} else {
		// There is definitely no frame here, we can immediately flip to showing nothing
		nonqueue_watchers_.clear();
		set_empty_image();
		return;
	}
}
void ViewerWidget::play_internal(int speed, bool in_to_out_only)
{
	Q_ASSERT(speed != 0);

	if (!get_connected_node()) {
		// Do nothing if no viewer node is attached
		return;
	}

	if (timebase().isNull()) {
		qCritical() << "ViewerWidget can't play with an invalid timebase";
		return;
	}

	// Kindly tell all viewers to stop playing and caching so all resources can be used for playback
	foreach (ViewerWidget *viewer, instances) {
		if (viewer != this) {
			viewer->pause_internal();
		}
	}
	oakengine_preview_cacher_set_thumbnails_paused(1);

	oakengine_render_manager_set_aggressive_garbage_collection(1);

	// Disarm recording if armed
	if (record_armed_) {
		disarm_recording();
	}

	// If the playhead is beyond the end, restart at 0
	if (!recording_) {
		Rational last_frame = get_connected_node()->get_length() - timebase();
		if (!in_to_out_only &&
			get_connected_node()->get_playhead() >= last_frame) {
			if (speed > 0) {
				oakengine_viewer_set_playhead(
				reinterpret_cast<OakEngineNode *>(get_connected_node()),
				0, 1);
			} else {
				oakengine_viewer_set_playhead(
				reinterpret_cast<OakEngineNode *>(get_connected_node()),
				last_frame.numerator(), last_frame.denominator());
			}
		}
	}

	playback_speed_ = speed;
	play_in_to_out_only_ = in_to_out_only;

	playback_queue_next_frame_ = get_timestamp() + playback_speed_;

	controls_->show_pause_button();

	queue_starved_start_ = 0;

	// Attempt to fill playback queue
	if (is_video_visible()) {
		prequeue_length_ = determine_playback_queue_size();

		if (prequeue_length_ > 0) {
			prequeuing_video_ = true;
			prequeue_count_ = 0;

			for (int i = 0; i < prequeue_length_; i++) {
				request_next_frame_for_queue();
			}

			dry_run_next_frame_ = playback_queue_next_frame_;
			request_next_dry_run();
		}
	}

	AudioParams ap = viewer_output_audio_params(get_connected_node());
	qDebug() << "ViewerWidget::PlayInternal: audio params valid=" << ap.is_valid()
			 << "channel_count=" << ap.channel_count();
	if (ap.is_valid() && ap.channel_count() != 0) {
		update_audio_processor();

		// Verify audio processor output params are valid before using them
		OakAudioParams *output_params =
			oakengine_audio_processor_output_params(audio_processor_);
		const bool output_valid =
			output_params && oakcore_audioparams_is_valid(output_params);
		qDebug() << "ViewerWidget::PlayInternal: audio processor output params valid="
				 << output_valid;
		if (!output_valid) {
			qWarning()
				<< "Audio processor output params are invalid, skipping audio playback";
		} else {
			oakengine_audio_set_output_notify_interval(
				oakcore_audioparams_time_to_bytes(
					output_params, k_audio_playback_interval.to_double()));
			audio_notify_sub_ = oakengine_event_subscribe(
				oakengine_audio_manager_handle(),
				OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_NOTIFY,
				[](const oakengine_event *, void *userdata) {
					static_cast<ViewerWidget *>(userdata)->queue_next_audio_buffer();
				},
				this);

			static const int prequeue_count = 2;
			prequeuing_audio_ =
				prequeue_count; // Queue two buffers ahead of time
			audio_playback_queue_time_ = get_connected_node()->get_playhead();
			qDebug() << "ViewerWidget::PlayInternal: prequeuing audio start time="
					 << audio_playback_queue_time_.to_double();
			for (int i = 0; i < prequeue_count; i++) {
				queue_next_audio_buffer();
			}
		}
		oakcore_audioparams_free(output_params);
	}

	// If there's nothing to prequeue, start playback immediately so the
	// playhead advances even when only the audio waveform is visible.
	if (!prequeuing_video_ && !prequeuing_audio_) {
		finish_play_preprocess();
	}

	// Force screen to stay awake
	prevent_sleep(true);
}

void ViewerWidget::pause_internal()
{
	if (recording_) {
		oakengine_audio_stop_recording();
		recording_ = false;
		controls_->set_pause_button_recording_state(false);

		recording_callback_->disable_recording_overlay();
		recording_callback_->recording_callback(
			recording_filename_, recording_range_, recording_track_);
	}

	if (is_playing()) {
		playback_speed_ = 0;
		controls_->show_play_button();

		foreach (ViewerDisplayWidget *dw, playback_devices_) {
			dw->pause();
		}

		// Cancel in-flight render tickets before deleting watchers,
		// otherwise the render thread keeps working on stale frames
		// and blocks the single-frame render requested by UpdateTextureFromNode().
		foreach (OakEnginePreviewRequest *req, queue_watchers_) {
			oakengine_preview_request_free(req);
		}
		queue_watchers_.clear();
		oakengine_preview_cacher_clear_single_frame_renders(0);

		playback_backup_timer_.stop();

		// Handle audio
		oakengine_audio_stop_output();
		AudioMonitor::stop_on_all();
		prequeued_audio_.clear();
		if (audio_notify_sub_ > 0) {
			oakengine_event_unsubscribe(audio_notify_sub_);
			audio_notify_sub_ = 0;
		}
		for (auto *req : audio_playback_queue_) { oakengine_preview_request_free(req); }
		audio_playback_queue_.clear();
		update_audio_processor();

		oakengine_preview_cacher_set_thumbnails_paused(0);

		update_texture_from_node();

		oakengine_render_manager_set_aggressive_garbage_collection(false);
	}

	prequeuing_video_ = false;
	prequeuing_audio_ = 0;
	dry_run_watchers_.clear();

	// Reset screen timeout timer
	prevent_sleep(false);
}

void ViewerWidget::push_scrubbed_audio()
{
	if (!is_playing() && get_connected_node() &&
		OAK_CONFIG("AudioScrubbing").toBool() && enable_audio_scrubbing_) {
		if (ignore_scrub_ > 0) {
			ignore_scrub_--;
		}

		if (ignore_scrub_ == 0) {
			// Get audio src device from renderer
			const AudioParams &params = viewer_output_audio_params(get_connected_node());

			if (params.is_valid()) {
				// NOTE: Hardcoded scrubbing interval (20ms)
				Rational interval = Rational(20, 1000);

				OakEngineNode *viewer =
					reinterpret_cast<OakEngineNode *>(get_connected_node());
				OakEnginePreviewRequest *req =
					oakengine_preview_request_audio_range(
						viewer,
						get_connected_node()->get_playhead().numerator(),
						get_connected_node()->get_playhead().denominator(),
						(get_connected_node()->get_playhead() + interval).numerator(),
						(get_connected_node()->get_playhead() + interval).denominator());
				if (req) {
					oakengine_preview_request_set_finished_callback(
						req, audio_scrub_finished_cb, this);
					audio_scrub_watchers_.push_back(req);
				}
			}
		}
	}
}
void ViewerWidget::update_minimum_scale()
{
	if (!get_connected_node()) {
		return;
	}

	if (get_connected_node()->get_length().isNull()) {
		// Avoids divide by zero
		set_minimum_scale(0);
	} else {
		double min_scale = static_cast<double>(ruler()->width()) /
						   get_connected_node()->get_length().to_double();
		// Ensure min_scale doesn't exceed max_scale to prevent crash
		min_scale = qMin(min_scale, get_maximum_scale());
		set_minimum_scale(min_scale);
	}
}

void ViewerWidget::set_color_transform(const ColorTransform &transform,
									 ViewerDisplayWidget *sender)
{
	sender->set_color_transform(transform);
}

QString ViewerWidget::get_cached_filename_from_time(const Rational &time)
{
	if (frame_exists_at_time(time)) {
		return get_connected_node()->video_frame_cache()->get_valid_cache_filename(
			time);
	}

	return QString();
}

bool ViewerWidget::frame_exists_at_time(const Rational &time)
{
	return get_connected_node() && time >= 0 &&
		   time < get_connected_node()->get_video_length();
}

bool ViewerWidget::viewer_might_be_a_still()
{
	return get_connected_node() &&
		   get_connected_node()->get_connected_texture_output() &&
		   get_connected_node()->get_video_length().isNull();
}

void ViewerWidget::set_display_image(OakEnginePreviewRequest *req)
{
	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		QVariant push;
		if (req && oakengine_preview_request_has_result(req)) {
			oak_playback_frame frame;
			if (dynamic_cast<MulticamDisplay *>(dw)) {
				// Multicam: use the default frame for now
				if (oakengine_preview_request_get_frame(req, &frame) == 0) {
					void *f = oakengine_codec_frame_create();
					{
						oak_video_params pod = {};
						pod.width = frame.width;
						pod.height = frame.height;
						pod.format = frame.format;
						oakengine_codec_frame_set_video_params(f, &pod);
					}
					oakengine_codec_frame_allocate(f);
					int fls = oakengine_codec_frame_linesize_bytes(f);
					if (frame.data && frame.linesize > 0 && fls > 0) {
						memcpy(oakengine_codec_frame_data(f), frame.data,
							   qMin(fls, frame.linesize) * frame.height);
					}
					push = QVariant::fromValue(oak_make_shared_frame(f));
				}
			} else {
				if (oakengine_preview_request_get_frame(req, &frame) == 0) {
					void *f = oakengine_codec_frame_create();
					{
						oak_video_params pod = {};
						pod.width = frame.width;
						pod.height = frame.height;
						pod.format = frame.format;
						oakengine_codec_frame_set_video_params(f, &pod);
					}
					oakengine_codec_frame_allocate(f);
					int fls = oakengine_codec_frame_linesize_bytes(f);
					if (frame.data && frame.linesize > 0 && fls > 0) {
						memcpy(oakengine_codec_frame_data(f), frame.data,
							   qMin(fls, frame.linesize) * frame.height);
					}
					push = QVariant::fromValue(oak_make_shared_frame(f));
				}
			}
		}
		dw->set_image(push);
	}
}

void ViewerWidget::set_display_image(RenderTicketPtr ticket)
{
	// Legacy compat: convert to OakEnginePreviewRequest* not available,
	// but this path is no longer called internally.
	// Call the new overload with nullptr to clear the display.
	set_display_image(static_cast<OakEnginePreviewRequest *>(nullptr));
}

OakEnginePreviewRequest *ViewerWidget::request_next_frame_for_queue(bool increment)
{
	OakEnginePreviewRequest *req = nullptr;

	Rational next_time =
		Timecode::timestamp_to_time(playback_queue_next_frame_, timebase());

	if (frame_exists_at_time(next_time) || viewer_might_be_a_still()) {
		if (increment) {
			playback_queue_next_frame_ += playback_speed_;
		}

		req = get_frame(next_time);
		if (req) {
			oakengine_preview_request_set_finished_callback(
				req, queue_finished_cb, this);
			queue_watchers_.append(req);
		}
	}

	return req;
}

OakEnginePreviewRequest *ViewerWidget::get_frame(const Rational &t)
{
	if (is_playing() || prequeuing_video_) {
		return get_single_frame(t);
	}

	QString cache_fn =
		get_connected_node()->video_frame_cache()->get_valid_cache_filename(t);

	if (!QFileInfo::exists(cache_fn)) {
		// Frame hasn't been cached, start render job
		return get_single_frame(t);
	} else {
		// Frame has been cached, grab the frame via preview request
		OakEngineNode *viewer =
			reinterpret_cast<OakEngineNode *>(get_connected_node());
		return oakengine_preview_request_single_frame(
			viewer, t.numerator(), t.denominator(), 0);
	}
}

void ViewerWidget::finish_play_preprocess()
{
	// Check if we're still waiting for video or audio respectively
	if (prequeuing_video_ || prequeuing_audio_) {
		return;
	}

	int64_t playback_start_time = get_timestamp();

	// Restart the audio output clock for this playback run; the playback
	// timer uses it as its master clock
	oakengine_audio_reset_output_clock();

	// Start audio waveform playback
	if (!prequeued_audio_.isEmpty()) {
		char errbuf[256];
		OakAudioParams *oap =
			oakengine_audio_processor_output_params(audio_processor_);
		if (!oakengine_audio_push_to_output(oap,
											 prequeued_audio_.constData(),
											 prequeued_audio_.size(),
											 errbuf, sizeof(errbuf))) {
			oakcore_audioparams_free(oap);
			QMessageBox::critical(
				this, tr("Audio Error"),
				tr("Failed to start audio: %1\n\n"
				   "Please check your audio preferences and try again.")
					.arg(QString::fromUtf8(errbuf)));
		} else {
			oakcore_audioparams_free(oap);
		}
		prequeued_audio_.clear();

		AudioMonitor::start_waveform_on_all(
			get_connected_node()->get_connected_waveform(),
			get_connected_node()->get_playhead(), playback_speed_);
	}

	display_widget_->reset_fps_timer();

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->play(playback_start_time, playback_speed_, timebase(),
				 is_video_visible());
	}

	// This is our timer for loading the queue and setting the time
	playback_backup_timer_.setInterval(
		qMax(1, qFloor(timebase_dbl() * 1000.0)));
	playback_backup_timer_.start();

	playback_timer_update();
}

int ViewerWidget::determine_playback_queue_size()
{
	if (playback_speed_ == 0) {
		return 0;
	}

	int64_t end_ts;

	if (playback_speed_ > 0) {
		end_ts = Timecode::time_to_timestamp(
			get_connected_node()->get_video_length(), timebase());
	} else {
		end_ts = 0;
	}

	int remaining_frames = (end_ts - get_timestamp() - 1) / playback_speed_;

	// Generate maximum queue
	int max_frames =
		qCeil(k_video_playback_interval.to_double() / timebase().to_double());

	return qMin(max_frames, remaining_frames);
}

void ViewerWidget::context_menu_set_full_screen(QAction *action)
{
	set_full_screen(QGuiApplication::screens().at(action->data().toInt()));
}

void ViewerWidget::context_menu_set_playback_res(QAction *action)
{
	int div = action->data().toInt();

	void *c = oakengine_viewer_set_preview_divider_command(
		reinterpret_cast<OakEngineNode *>(get_connected_node()), div);
	oakengine_undo_push(c, tr("Changed Playback Resolution").toUtf8().constData());
}

void ViewerWidget::context_menu_disable_safe_margins()
{
	context_menu_widget_->set_safe_margins(ViewerSafeMarginInfo(false));
}

void ViewerWidget::context_menu_set_safe_margins()
{
	context_menu_widget_->set_safe_margins(ViewerSafeMarginInfo(true));
}

void ViewerWidget::context_menu_set_custom_safe_margins()
{
	bool ok;

	double new_ratio = get_float_ratio_from_user(this, tr("Safe Margins"), &ok);

	if (ok) {
		context_menu_widget_->set_safe_margins(
			ViewerSafeMarginInfo(true, new_ratio));
	}
}

void ViewerWidget::window_about_to_close()
{
	ViewerWindow *vw = static_cast<ViewerWindow *>(sender());
	windows_.remove(windows_.key(vw));
	playback_devices_.removeOne(vw->display_widget());
}

void ViewerWidget::renderer_generated_frame()
{
	if (nonqueue_watchers_.isEmpty()) {
		return;
	}

	OakEnginePreviewRequest *req = nonqueue_watchers_.takeFirst();

	if (oakengine_preview_request_has_result(req)) {
		set_display_image(req);
	}

	oakengine_preview_request_free(req);
}

void ViewerWidget::renderer_generated_frame_for_queue()
{
	if (queue_watchers_.isEmpty()) {
		return;
	}

	OakEnginePreviewRequest *req = queue_watchers_.takeFirst();

	if (oakengine_preview_request_has_result(req)) {
		oak_playback_frame pf;
		bool has_frame = (oakengine_preview_request_get_frame(req, &pf) == 0);
		bool drop_frame = false;

		// Ignore this signal if we've paused now
		if (is_playing() || prequeuing_video_) {
			if (!drop_frame && has_frame) {
				void *f = oakengine_codec_frame_create();
				{
					oak_video_params pod = {};
					pod.width = pf.width;
					pod.height = pf.height;
					pod.format = pf.format;
					oakengine_codec_frame_set_video_params(f, &pod);
				}
				oakengine_codec_frame_allocate(f);
				int fls = oakengine_codec_frame_linesize_bytes(f);
				if (pf.data && pf.linesize > 0 && fls > 0) {
					memcpy(oakengine_codec_frame_data(f), pf.data,
						   qMin(fls, pf.linesize) * pf.height);
				}
				QVariant frame = QVariant::fromValue(f);

				foreach (ViewerDisplayWidget *dw, playback_devices_) {
					dw->queue()->append_timewise({ Rational(), frame },
												playback_speed_);
				}
			}

			if (prequeuing_video_) {
				prequeue_count_++;

				if (prequeue_count_ == prequeue_length_) {
					prequeuing_video_ = false;
					finish_play_preprocess();
				}
			}
		}
	}

	if (first_requeue_watcher_ == req) {
		first_requeue_watcher_ = nullptr;
	}

	oakengine_preview_request_free(req);
}

void ViewerWidget::show_context_menu(const QPoint &pos)
{
	if (!get_connected_node()) {
		return;
	}

	Menu menu(static_cast<QWidget *>(sender()));

	context_menu_widget_ = dynamic_cast<ViewerDisplayWidget *>(sender());

	// ViewerDisplayWidget options
	if (context_menu_widget_) {
		// Color options
		if (context_menu_widget_->color_manager() && color_menu_enabled_) {
			{
				Menu *ocio_colorspace_menu =
					context_menu_widget_->get_color_space_menu(&menu);
				menu.addMenu(ocio_colorspace_menu);
			}

			{
				Menu *ocio_display_menu =
					context_menu_widget_->get_display_menu(&menu);
				menu.addMenu(ocio_display_menu);
			}

			{
				Menu *ocio_view_menu = context_menu_widget_->get_view_menu(&menu);
				menu.addMenu(ocio_view_menu);
			}

			{
				Menu *ocio_look_menu = context_menu_widget_->get_look_menu(&menu);
				menu.addMenu(ocio_look_menu);
			}

			menu.addSeparator();
		}

		{
			// Viewer Zoom Level
			Menu *zoom_menu = new Menu(tr("Zoom"), &menu);
			menu.addMenu(zoom_menu);

			zoom_menu->addAction(tr("Fit"))->setData(-1);
			for (int i = 0; i < ViewerSizer::k_zoom_level_count; i++) {
				double z = ViewerSizer::k_zoom_levels[i];
				zoom_menu->addAction(tr("%1%").arg(z * 100.0))->setData(z);
			}

			connect(zoom_menu, &QMenu::triggered, this,
					&ViewerWidget::set_zoom_from_menu);
		}

		{
			// Full Screen Menu
			Menu *full_screen_menu = new Menu(tr("Full Screen"), &menu);
			menu.addMenu(full_screen_menu);

			for (int i = 0; i < QGuiApplication::screens().size(); i++) {
				QScreen *s = QGuiApplication::screens().at(i);

				QAction *a = full_screen_menu->addAction(
					tr("Screen %1: %2x%3")
						.arg(QString::number(i),
							 QString::number(s->size().width()),
							 QString::number(s->size().height())));

				a->setData(i);
				a->setCheckable(true);
				a->setChecked(
					windows_.contains(QGuiApplication::screens().at(i)));
			}

			connect(full_screen_menu, &QMenu::triggered, this,
					&ViewerWidget::context_menu_set_full_screen);
		}

		{
			// Playback Resolution Menu
			Menu *playback_res_menu =
				new Menu(tr("Playback Resolution"), &menu);
			menu.addMenu(playback_res_menu);

			{
			const int n = oakengine_video_params_supported_divider_count();
			for (int i = 0; i < n; i++) {
				int d = oakengine_video_params_supported_divider_at(i);
				char name_buf[64];
				oakengine_video_params_divider_name(d, name_buf, sizeof(name_buf));
				playback_res_menu->add_action_with_data(
					QString::fromUtf8(name_buf), d,
					viewer_output_video_params(get_connected_node()).divider());
			}

			connect(playback_res_menu, &QMenu::triggered, this,
					&ViewerWidget::context_menu_set_playback_res);
			}
		}

		{
			// Deinterlace Option
			if (viewer_output_video_params(get_connected_node()).interlacing() !=
				0) {
				QAction *deinterlace_action = menu.addAction(tr("Deinterlace"));
				deinterlace_action->setCheckable(true);
				deinterlace_action->setChecked(
					display_widget_->is_deinterlacing());
				connect(deinterlace_action, &QAction::triggered,
						display_widget_,
						&ViewerDisplayWidget::set_deinterlacing);
			}
		}

		menu.addSeparator();

		/* TEMP: Hide sequence cache options. Want to see if clip caching supersedes it.
    {
      Menu* cache_menu = new Menu(tr("Cache"), &menu);
      menu.addMenu(cache_menu);

      // Cache Entire Sequence
      QAction* cache_entire_sequence = cache_menu->addAction(tr("Cache Entire Sequence"));
      connect(cache_entire_sequence, &QAction::triggered, this, &ViewerWidget::CacheEntireSequence);

      // Cache In/Out Sequence
      QAction* cache_inout_sequence = cache_menu->addAction(tr("Cache Sequence In/Out"));
      connect(cache_inout_sequence, &QAction::triggered, this, &ViewerWidget::CacheSequenceInOut);
    }*/

		menu.addSeparator();

		{
			// Safe Margins
			Menu *safe_margin_menu = new Menu(tr("Safe Margins"), &menu);
			menu.addMenu(safe_margin_menu);

			QAction *safe_margin_off = safe_margin_menu->addAction(tr("Off"));
			safe_margin_off->setCheckable(true);
			safe_margin_off->setChecked(
				!context_menu_widget_->get_safe_margin().is_enabled());
			connect(safe_margin_off, &QAction::triggered, this,
					&ViewerWidget::context_menu_disable_safe_margins);

			QAction *safe_margin_on = safe_margin_menu->addAction(tr("On"));
			safe_margin_on->setCheckable(true);
			safe_margin_on->setChecked(
				context_menu_widget_->get_safe_margin().is_enabled() &&
				!context_menu_widget_->get_safe_margin().custom_ratio());
			connect(safe_margin_on, &QAction::triggered, this,
					&ViewerWidget::context_menu_set_safe_margins);

			QAction *safe_margin_custom =
				safe_margin_menu->addAction(tr("Custom Aspect"));
			safe_margin_custom->setCheckable(true);
			safe_margin_custom->setChecked(
				context_menu_widget_->get_safe_margin().is_enabled() &&
				context_menu_widget_->get_safe_margin().custom_ratio());
			connect(safe_margin_custom, &QAction::triggered, this,
					&ViewerWidget::context_menu_set_custom_safe_margins);
		}

		menu.addSeparator();
	}

	{
		QAction *stop_playback_on_last_frame =
			menu.addAction(tr("Stop Playback On Last Frame"));
		stop_playback_on_last_frame->setCheckable(true);
		stop_playback_on_last_frame->setChecked(
			OAK_CONFIG("StopPlaybackOnLastFrame").toBool());
		connect(stop_playback_on_last_frame, &QAction::triggered, this,
				[](bool e) { OAK_CONFIG("StopPlaybackOnLastFrame") = e; });

		menu.addSeparator();
	}

	{
		auto waveform_menu = new Menu(tr("Audio Waveform"), &menu);
		menu.addMenu(waveform_menu);

		waveform_menu->add_action_with_data(tr("Automatically Show/Hide"),
										 k_wf_automatic, waveform_mode_);
		waveform_menu->add_action_with_data(tr("Show Waveform Only"),
										 k_wf_waveform_only, waveform_mode_);
		waveform_menu->add_action_with_data(tr("Show Both Viewer And Waveform"),
										 k_wf_viewer_and_waveform, waveform_mode_);

		connect(waveform_menu, &Menu::triggered, this,
				&ViewerWidget::update_waveform_mode_from_menu);
	}

	{
		QAction *show_fps_action = menu.addAction(tr("Show FPS"));
		show_fps_action->setCheckable(true);
		show_fps_action->setChecked(display_widget_->get_show_fps());
		connect(show_fps_action, &QAction::triggered, display_widget_,
				&ViewerDisplayWidget::set_show_fps);
	}

	if (context_menu_widget_ == display_widget_) {
		auto subtitle_menu = new Menu(tr("Subtitles"), &menu);
		menu.addMenu(subtitle_menu);

		QAction *show_subtitles_action =
			subtitle_menu->addAction(tr("Show Subtitles"));
		show_subtitles_action->setCheckable(true);
		show_subtitles_action->setChecked(display_widget_->get_show_subtitles());
		connect(show_subtitles_action, &QAction::triggered, display_widget_,
				&ViewerDisplayWidget::set_show_subtitles);

		subtitle_menu->addSeparator();

		auto subtitle_font_properties =
			subtitle_menu->addAction(tr("Subtitle Properties"));
		connect(subtitle_font_properties, &QAction::triggered, this,
				&ViewerWidget::show_subtitle_properties);

		auto subtitle_antialias =
			subtitle_menu->addAction(tr("Use Anti-aliasing"));
		subtitle_antialias->setCheckable(true);
		subtitle_antialias->setChecked(
			OAK_CONFIG("AntialiasSubtitles").toBool());
		connect(subtitle_antialias, &QAction::triggered, this, [this](bool e) {
			OAK_CONFIG("AntialiasSubtitles") = e;
			display_widget_->update();
		});
	}

	menu.addSeparator();

	auto save_frame_as_image = menu.addAction(tr("Save Frame As Image"));
	connect(save_frame_as_image, &QAction::triggered, this,
			&ViewerWidget::save_frame_as_image);

	menu.exec(static_cast<QWidget *>(sender())->mapToGlobal(pos));
}

void ViewerWidget::play(bool in_to_out_only)
{
	if (in_to_out_only) {
		if (get_connected_node() &&
			get_connected_node()->get_work_area()->enabled()) {
			// Jump to in point
			oakengine_viewer_set_playhead(
				reinterpret_cast<OakEngineNode *>(get_connected_node()),
				get_connected_node()->get_work_area()->in().numerator(),
				get_connected_node()->get_work_area()->in().denominator());
		} else {
			in_to_out_only = false;
		}
	} else if (record_armed_) {
		disarm_recording();

		char fn_buf[512];
		oakengine_project_filename(
			reinterpret_cast<OakEngineProject *>(
				get_connected_node()->project()),
			fn_buf, sizeof(fn_buf));
		if (fn_buf[0] == '\0') {
			QMessageBox::critical(
				this, tr("Audio Recording"),
				tr("Project must be saved before you can record audio."));
			return;
		}

		QDir audio_path(QFileInfo(fn_buf)
							.dir()
							.filePath(tr("audio")));
		if (!audio_path.exists()) {
			audio_path.mkpath(QStringLiteral("."));
		}

		recording_filename_ = audio_path.filePath(QStringLiteral("%1.%2").arg(
			QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss"),
			[]() -> QString {
				char ext_buf[64];
				int fmt = OAK_CONFIG("AudioRecordingFormat").toInt();
				oakengine_encoding_format_extension(fmt, ext_buf, sizeof(ext_buf));
				return QString::fromUtf8(ext_buf);
			}()));

		OakEngineEncodingParams *encode_param =
			oakengine_encoding_params_create();
		oakengine_encoding_params_enable_audio(
			encode_param,
			OAK_CONFIG("AudioRecordingSampleRate").toInt(),
			OAK_CONFIG("AudioRecordingChannelLayout").toULongLong(),
			SampleFormat::from_string(OAK_CONFIG("AudioRecordingSampleFormat")
										  .toString()
										  .toStdString()),
			OAK_CONFIG("AudioRecordingCodec").toInt());
		oakengine_encoding_params_set_filename(
			encode_param, recording_filename_.toUtf8().constData());
		oakengine_encoding_params_set_audio_bit_rate(
			encode_param,
			OAK_CONFIG("AudioRecordingBitRate").toInt() * 1000);

		char errbuf[256];
		const int rec_ret = oakengine_encoding_start_audio_recording(
			encode_param, errbuf, static_cast<int>(sizeof(errbuf)));
		oakengine_encoding_params_destroy(encode_param);

		if (rec_ret == OAKENGINE_OK) {
			recording_ = true;
			controls_->set_pause_button_recording_state(true);
			recording_callback_->enable_recording_overlay(
				TimelineCoordinate(recording_range_.in(), recording_track_));
		} else {
			QMessageBox::critical(
				this, tr("Audio Recording"),
				tr("Failed to start audio recording: %1").arg(
					errbuf[0] ? QString::fromUtf8(errbuf)
							  : tr("Unknown error")));
			return;
		}
	}

	play_internal(1, in_to_out_only);
}

void ViewerWidget::play()
{
	play(false);
}

void ViewerWidget::pause()
{
	pause_internal();
}

void ViewerWidget::shuttle_left()
{
	int current_speed = playback_speed_;

	if (current_speed != 0) {
		pause_internal();
	}

	current_speed--;

	if (current_speed == 0) {
		current_speed--;
	}

	play_internal(current_speed, false);
}

void ViewerWidget::shuttle_stop()
{
	pause();
}

void ViewerWidget::shuttle_right()
{
	int current_speed = playback_speed_;

	if (current_speed != 0) {
		pause_internal();
	}

	current_speed++;

	if (current_speed == 0) {
		current_speed++;
	}

	play_internal(current_speed, false);
}

void ViewerWidget::set_color_transform(const ColorTransform &transform)
{
	set_color_transform(transform, display_widget_);
}

void ViewerWidget::set_signal_cursor_color_enabled(bool e)
{
	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_signal_cursor_color_enabled(e);
	}
}

void ViewerWidget::TimebaseChangedEvent(const Rational &timebase)
{
	super::TimebaseChangedEvent(timebase);

	controls_->set_timebase(timebase);

	controls_->set_time(get_connected_node() ? get_connected_node()->get_playhead() :
											0);
	length_changed_slot(get_connected_node() ? get_connected_node()->get_length() : 0);
}

void ViewerWidget::playback_timer_update()
{
	Q_ASSERT(playback_speed_ != 0);

	Rational current_time = Timecode::timestamp_to_time(
		display_widget_->timer()->get_timestamp_now(), timebase());

	Rational min_time, max_time;

	if (recording_ && recording_range_.out() != recording_range_.in()) {
		// Limit recording range if applicable
		min_time = recording_range_.in();
		max_time = recording_range_.out();

	} else if (play_in_to_out_only_ &&
			   get_connected_node()->get_work_area()->enabled()) {
		// If "play in to out" is enabled or we're looping AND we have a workarea, only play the workarea
		min_time = get_connected_node()->get_work_area()->in();
		max_time = get_connected_node()->get_work_area()->out();

	} else {
		// Otherwise set the bounds to the range of the sequence
		min_time = 0;
		max_time = get_connected_node()->get_length();
	}

	// If we're stopping playback on the last frame rather than after it, subtract our max time
	// by one timebase unit
	if (OAK_CONFIG("StopPlaybackOnLastFrame").toBool()) {
		max_time = qMax(min_time, max_time - timebase());
	}

	Rational time_to_set;
	bool end_of_line = false;
	bool play_after_pause = false;

	if ((!recording_ || recording_range_.out() != recording_range_.in()) &&
		((playback_speed_ < 0 && current_time <= min_time) ||
		 (playback_speed_ > 0 && current_time >= max_time))) {
		// Determine which timestamp we tripped
		Rational tripped_time;

		if (current_time <= min_time) {
			tripped_time = min_time;
		} else {
			tripped_time = max_time;
		}

		// Signal that we've reached the end of whatever range we're playing and should either pause
		// or restart playback
		end_of_line = true;

		if (OAK_CONFIG("Loop").toBool() && !recording_) {
			// If we're looping, jump to the other side of the workarea and continue
			time_to_set = (tripped_time == min_time) ? max_time : min_time;

			// Signal to restart playback after the pause signalled by `end_of_line`
			play_after_pause = true;

		} else {
			// Pause at the boundary we tripped
			time_to_set = tripped_time;
		}

	} else {
		// Sets time normally to whatever we calculated as the "current time"
		time_to_set = current_time;
	}

	// Set the time. By wrapping in this bool, we prevent TimeChangedEvent's default behavior of
	// pausing. Even if we pause it later with `end_of_line`, we prefer pausing after setting the time
	// so that an audio scrub event, etc. isn't sent.
	time_changed_from_timer_ = true;
	oakengine_viewer_set_playhead(
		reinterpret_cast<OakEngineNode *>(get_connected_node()),
		time_to_set.numerator(), time_to_set.denominator());
	time_changed_from_timer_ = false;
	if (end_of_line) {
		// Cache the current speed
		int current_speed = playback_speed_;

		pause_internal();
		if (play_after_pause) {
			play_internal(current_speed, play_in_to_out_only_);
		}
	}

	if (is_playing() && is_video_visible()) {
		while ((int(display_widget_->queue()->size()) +
				queue_watchers_.size()) < determine_playback_queue_size()) {
			if (!request_next_frame_for_queue()) {
				// Prevent infinite loop
				break;
			}
		}
	}

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->queue()->purge_before(current_time, playback_speed_);
	}
}

void ViewerWidget::set_viewer_resolution(int width, int height)
{
	sizer_->set_child_size(width, height);

	foreach (ViewerWindow *vw, windows_) {
		vw->set_resolution(width, height);
	}

	update_info_chip();
}

void ViewerWidget::set_viewer_pixel_aspect(const Rational &ratio)
{
	sizer_->set_pixel_aspect_ratio(ratio);

	foreach (ViewerWindow *vw, windows_) {
		vw->set_pixel_aspect_ratio(ratio);
	}
}

void ViewerWidget::length_changed_slot(const Rational &length)
{
	if (last_length_ != length) {
		controls_->set_end_time(length);
		update_minimum_scale();

		if (get_connected_node() && length < last_length_ &&
			get_connected_node()->get_playhead() >= length) {
			update_texture_from_node();
		}

		last_length_ = length;
	}
}

void ViewerWidget::interlacing_changed_slot(int interlacing)
{
	// Automatically set a "sane" deinterlacing option
	bool deint = interlacing != 0; // k_interlace_none

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_deinterlacing(deint);
	}
}

void ViewerWidget::update_renderer_video_parameters()
{
	VideoParams vp = viewer_output_video_params(get_connected_node());

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_video_params(vp);
	}
}

void ViewerWidget::update_renderer_audio_parameters()
{
	AudioParams ap = viewer_output_audio_params(get_connected_node());

	update_audio_processor();

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_audio_params(ap);
	}
}

void ViewerWidget::set_zoom_from_menu(QAction *action)
{
	auto s = sizer_->get_container_size();
	sizer_->set_zoom_anchored(action->data().toDouble(), s.width() / 2,
							s.height() / 2);
}

void ViewerWidget::create_overlay()
{
	// A thin transparent strip laid over the top of the viewer display. Left
	// side carries an info chip (resolution + frame rate); right side carries
	// zoom in/out/fit and a safe-frame toggle, per the UI design reference.
	overlay_ = new QWidget(sizer_);
	overlay_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
	overlay_->setAutoFillBackground(false);

	auto *overlay_layout = new QHBoxLayout(overlay_);
	overlay_layout->setContentsMargins(6, 4, 6, 4);
	overlay_layout->setSpacing(4);

	info_chip_ = new QLabel(overlay_);
	info_chip_->setStyleSheet(QStringLiteral(
		"QLabel { background-color: rgba(0,0,0,160); color: white; "
		"border-radius: 3px; padding: 2px 6px; }"));
	info_chip_->setVisible(false);
	overlay_layout->addWidget(info_chip_);

	overlay_layout->addStretch();

	const QString btn_style = QStringLiteral(
		"QToolButton { background-color: rgba(0,0,0,160); color: white; "
		"border: none; border-radius: 3px; padding: 2px 6px; } "
		"QToolButton:hover { background-color: rgba(0,0,0,200); } "
		"QToolButton:checked { background-color: rgba(0,120,215,200); }");

	auto make_btn = [this, &btn_style](const QString &text,
									   const QString &tooltip) {
		auto *b = new QToolButton(overlay_);
		b->setText(text);
		b->setToolTip(tooltip);
		b->setStyleSheet(btn_style);
		b->setAutoRaise(false);
		return b;
	};

	QToolButton *zoom_out_btn = make_btn(QStringLiteral("\u2212"), tr("Zoom Out"));
	QToolButton *zoom_in_btn = make_btn(QStringLiteral("+"), tr("Zoom In"));
	QToolButton *zoom_fit_btn = make_btn(QStringLiteral("\u2922"), tr("Fit to Window"));
	safe_frame_btn_ = make_btn(QStringLiteral("\u25A2"), tr("Safe Margins"));
	safe_frame_btn_->setCheckable(true);

	overlay_layout->addWidget(zoom_out_btn);
	overlay_layout->addWidget(zoom_in_btn);
	overlay_layout->addWidget(zoom_fit_btn);
	overlay_layout->addWidget(safe_frame_btn_);

	connect(zoom_out_btn, &QToolButton::clicked, this,
			&ViewerWidget::overlay_zoom_out);
	connect(zoom_in_btn, &QToolButton::clicked, this,
			&ViewerWidget::overlay_zoom_in);
	connect(zoom_fit_btn, &QToolButton::clicked, this,
			&ViewerWidget::overlay_zoom_fit);
	connect(safe_frame_btn_, &QToolButton::toggled, this,
			[this](bool) { overlay_toggle_safe_frame(); });

	overlay_->raise();
}

void ViewerWidget::position_overlay()
{
	if (!overlay_ || !sizer_) {
		return;
	}
	// Span the full width of the sizer, hugging the top edge.
	overlay_->setGeometry(0, 0, sizer_->width(), overlay_->sizeHint().height());
	overlay_->raise();
}

void ViewerWidget::update_info_chip()
{
	if (!info_chip_) {
		return;
	}

	if (!get_connected_node()) {
		info_chip_->setVisible(false);
		return;
	}

	VideoParams vp = viewer_output_video_params(get_connected_node());
	if (vp.width() <= 0 || vp.height() <= 0) {
		info_chip_->setVisible(false);
		return;
	}

	const double fps = vp.frame_rate().to_double();
	const double fps_rounded = qRound(fps);
	QString fps_str = (qFuzzyCompare(fps, fps_rounded))
						  ? QString::number(static_cast<int>(fps_rounded))
						  : QString::number(fps, 'f', 2);

	info_chip_->setText(QStringLiteral("%1\u00d7%2 \u00b7 %3 FPS")
							.arg(vp.width())
							.arg(vp.height())
							.arg(fps_str));
	info_chip_->setVisible(true);
}

void ViewerWidget::overlay_zoom_in()
{
	if (overlay_zoom_index_ < ViewerSizer::k_zoom_level_count - 1) {
		overlay_zoom_index_++;
	}
	auto s = sizer_->get_container_size();
	sizer_->set_zoom_anchored(ViewerSizer::k_zoom_levels[overlay_zoom_index_],
							  s.width() / 2, s.height() / 2);
}

void ViewerWidget::overlay_zoom_out()
{
	if (overlay_zoom_index_ > 0) {
		overlay_zoom_index_--;
	}
	auto s = sizer_->get_container_size();
	sizer_->set_zoom_anchored(ViewerSizer::k_zoom_levels[overlay_zoom_index_],
							  s.width() / 2, s.height() / 2);
}

void ViewerWidget::overlay_zoom_fit()
{
	// The zoom menu uses -1 to mean "fit to window".
	auto s = sizer_->get_container_size();
	sizer_->set_zoom_anchored(-1, s.width() / 2, s.height() / 2);
}

void ViewerWidget::overlay_toggle_safe_frame()
{
	if (!safe_frame_btn_) {
		return;
	}
	display_widget_->set_safe_margins(
		ViewerSafeMarginInfo(safe_frame_btn_->isChecked()));
}

void ViewerWidget::viewer_invalidated_video_range(const TimeRange &range)
{
	// If our current frame is within this range, we need to update
	if (!is_playing() && get_connected_node()->get_playhead() >= range.in() &&
		(get_connected_node()->get_playhead() < range.out() ||
		 range.in() == range.out())) {
		QMetaObject::invokeMethod(this, &ViewerWidget::update_texture_from_node,
								  Qt::QueuedConnection);
	}
}

void ViewerWidget::update_waveform_mode_from_menu(QAction *a)
{
	set_waveform_mode(static_cast<WaveformMode>(a->data().toInt()));
}

void ViewerWidget::drag_entered(QDragEnterEvent *event)
{
	if (event->mimeData()->formats().contains(QString::fromUtf8(oakengine_project_item_mime_type()))) {
		event->accept();
	}
}

void ViewerWidget::dropped(QDropEvent *event)
{
	QByteArray mimedata = event->mimeData()->data(QString::fromUtf8(oakengine_project_item_mime_type()));
	QDataStream stream(&mimedata, QIODevice::ReadOnly);

	// Variables to deserialize into
	quintptr item_ptr = 0;
	QVector<Track::Reference> enabled_streams;

	while (!stream.atEnd()) {
		stream >> enabled_streams >> item_ptr;

		// We only need the one item
		break;
	}

	if (item_ptr) {
		Node *item = reinterpret_cast<Node *>(item_ptr);
		ViewerOutput *viewer = dynamic_cast<ViewerOutput *>(item);

		if (viewer) {
			connect_viewer_node(viewer);
		}
	}
}

}
