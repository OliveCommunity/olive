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
#include "common/ratiodialog.h"
#include "config/config.h"
#include "core.h"
#include "node/block/gap/gap.h"
#include "node/generator/shape/shapenodebase.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "panel/multicam/multicampanel.h"
#include "panel/panelmanager.h"
#include "render/rendermanager.h"
#include "viewerpreventsleep.h"
#include "widget/audiomonitor/audiomonitor.h"
#include "widget/menu/menu.h"
#include "widget/multicam/multicamdisplay.h"
#include "widget/timelinewidget/tool/add.h"
#include "widget/timeruler/timeruler.h"

namespace olive
{

#define super TimeBasedWidget

QVector<ViewerWidget *> ViewerWidget::instances;

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
		[](ColorProcessorPtr processor) {
			RenderManager::instance()->get_cacher()->set_display_color_processor(
				processor);
		});
	RenderManager::instance()->get_cacher()->set_display_color_processor(
		display_widget_->get_current_color_processor());
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

	// Make the display widget the first tabbable widget. While the viewer display cannot actually
	// be interacted with by tabbing, it prevents the actual first tabbable widget (the playhead
	// slider in `controls_`) from getting auto-focused any time the panel is maximized (with `)
	display_widget_->setFocusPolicy(Qt::TabFocus);

	// Create waveform view when audio is connected and video isn't
	waveform_view_ = new AudioWaveformView();
	connect_timeline_view(waveform_view_);
	layout->addWidget(waveform_view_);

	// Create time ruler
	layout->addWidget(ruler());

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
	connect(AudioManager::instance(), &AudioManager::output_params_changed, this,
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
	RenderManager::instance()->get_cacher()->set_playhead(time);

	last_time_ = time;
}

void ViewerWidget::ConnectNodeEvent(ViewerOutput *n)
{
	connect(n, &ViewerOutput::size_changed, this,
			&ViewerWidget::set_viewer_resolution);
	connect(n, &ViewerOutput::pixel_aspect_changed, this,
			&ViewerWidget::set_viewer_pixel_aspect);
	connect(n, &ViewerOutput::length_changed, this,
			&ViewerWidget::length_changed_slot);
	connect(n, &ViewerOutput::interlacing_changed, this,
			&ViewerWidget::interlacing_changed_slot);
	connect(n, &ViewerOutput::video_params_changed, this,
			&ViewerWidget::update_renderer_video_parameters);
	connect(n, &ViewerOutput::video_params_changed, this,
			&ViewerWidget::update_texture_from_node, Qt::QueuedConnection);
	connect(n, &ViewerOutput::audio_params_changed, this,
			&ViewerWidget::update_renderer_audio_parameters);
	if (FrameHashCache *cache = n->video_frame_cache()) {
		connect(cache, &FrameHashCache::invalidated, this,
				&ViewerWidget::viewer_invalidated_video_range);
	}
	connect(n, &ViewerOutput::texture_input_changed, this,
			&ViewerWidget::update_waveform_view_from_mode);

	connect(controls_, &PlaybackControls::time_changed, n,
			&ViewerOutput::set_playhead);

	VideoParams vp = n->get_video_params();

	interlacing_changed_slot(vp.interlacing());

	ruler()->set_playback_cache(n->video_frame_cache());

	set_viewer_resolution(vp.width(), vp.height());
	set_viewer_pixel_aspect(vp.pixel_aspect_ratio());
	last_length_ = 0;
	length_changed_slot(n->get_length());

	update_audio_processor();

	ColorManager *color_manager = n->project()->color_manager();

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

	disconnect(n, &ViewerOutput::size_changed, this,
			   &ViewerWidget::set_viewer_resolution);
	disconnect(n, &ViewerOutput::pixel_aspect_changed, this,
			   &ViewerWidget::set_viewer_pixel_aspect);
	disconnect(n, &ViewerOutput::length_changed, this,
			   &ViewerWidget::length_changed_slot);
	disconnect(n, &ViewerOutput::interlacing_changed, this,
			   &ViewerWidget::interlacing_changed_slot);
	disconnect(n, &ViewerOutput::video_params_changed, this,
			   &ViewerWidget::update_renderer_video_parameters);
	disconnect(n, &ViewerOutput::video_params_changed, this,
			   &ViewerWidget::update_texture_from_node);
	disconnect(n, &ViewerOutput::audio_params_changed, this,
			   &ViewerWidget::update_renderer_audio_parameters);
	if (FrameHashCache *cache = n->video_frame_cache()) {
		disconnect(cache, &FrameHashCache::invalidated, this,
				   &ViewerWidget::viewer_invalidated_video_range);
	}
	disconnect(n, &ViewerOutput::texture_input_changed, this,
			   &ViewerWidget::update_waveform_view_from_mode);

	disconnect(controls_, &PlaybackControls::time_changed, n,
			   &ViewerOutput::set_playhead);

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
}

RenderTicketPtr ViewerWidget::get_single_frame(const Rational &t, bool dry)
{
	return RenderManager::instance()->get_cacher()->get_single_frame(
		this->get_connected_node(), t, dry);
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
		vw->set_video_params(get_connected_node()->get_video_params());
		vw->display_widget()->set_deinterlacing(
			vw->display_widget()->is_deinterlacing());
	}

	vw->display_widget()->set_image(
		QVariant::fromValue(display_widget()->get_current_texture()));

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
	RenderManager::instance()->get_cacher()->force_cache_range(
		get_connected_node(), TimeRange(0, get_connected_node()->get_video_length()));
}

void ViewerWidget::cache_sequence_in_out()
{
	if (get_connected_node() && get_connected_node()->get_work_area()->enabled()) {
		RenderManager::instance()->get_cacher()->force_cache_range(
			get_connected_node(), get_connected_node()->get_work_area()->range());
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
	get_connected_node()->set_playhead(time.in());
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

FramePtr ViewerWidget::decode_cached_image(const QString &cache_path,
										 const QUuid &cache_id,
										 const int64_t &time)
{
	FramePtr frame = FrameHashCache::load_cache_frame(cache_path, cache_id, time);

	if (frame) {
		frame->set_timestamp(time);
	} else {
		qWarning() << "Tried to load cached frame from file but it was null";
	}

	return frame;
}

void ViewerWidget::decode_cached_image(RenderTicketPtr ticket,
									 const QString &cache_path,
									 const QUuid &cache_id, const int64_t &time)
{
	ticket->start();

	FramePtr f = decode_cached_image(cache_path, cache_id, time);

	if (f) {
		ticket->finish(QVariant::fromValue(f));
	} else {
		ticket->finish();
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
	RenderManager::instance()->get_cacher()->set_playhead(
		get_connected_node()->get_playhead());
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

		AudioParams ap = get_connected_node()->get_audio_params();
		if (ap.sample_rate() <= 0 || ap.channel_count() <= 0) {
			ap = AudioParams(
				OAK_CONFIG("DefaultSequenceAudioFrequency").toInt(),
				OAK_CONFIG("DefaultSequenceAudioLayout").toULongLong(),
				ViewerOutput::k_default_sample_format);
		}
		ap.set_format(ViewerOutput::k_default_sample_format);

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

		audio_processor_.open(
			ap, packed, (playback_speed_ == 0) ? 1 : std::abs(playback_speed_));
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

		MultiUndoCommand *command = new MultiUndoCommand();
		Node *clip = AddTool::create_addable_clip(
			command, s, Track::Reference(type, track_index), in, length);

		if (ShapeNodeBase *shape = dynamic_cast<ShapeNodeBase *>(clip)) {
			shape->set_rect(f, s->get_video_params(), command);
		}

		Core::instance()->undo_stack()->push(command, tr("Created Shape"));
		set_gizmos(clip);
	}
}

void ViewerWidget::handle_first_requeue_destroy()
{
	// Extra protection to ensure we don't reference a destroyed object
	if (first_requeue_watcher_ == sender()) {
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
	RenderTicketWatcher *w = static_cast<RenderTicketWatcher *>(sender());

	if (dry_run_watchers_.contains(w)) {
		request_next_dry_run();
	}

	delete w;
}

void ViewerWidget::request_next_dry_run()
{
	if (is_playing()) {
		Rational next_time =
			Timecode::timestamp_to_time(dry_run_next_frame_, timebase());
		if (frame_exists_at_time(next_time)) {
			if (next_time > get_connected_node()->get_playhead() +
								RenderManager::k_dry_run_interval) {
				QTimer::singleShot(timebase().to_double() / playback_speed_,
								   this, &ViewerWidget::request_next_dry_run);
			} else {
				RenderTicketWatcher *watcher = new RenderTicketWatcher(this);
				connect(watcher, &RenderTicketWatcher::finished, this,
						&ViewerWidget::dry_run_finished);
				watcher->set_ticket(get_single_frame(next_time, true));
				dry_run_next_frame_ += playback_speed_;
				dry_run_watchers_.append(watcher);
			}
		}
	}
}

void ViewerWidget::save_frame_as_image()
{
	Core::instance()->open_export_dialog_for_viewer(get_connected_node(), true);
}

void ViewerWidget::detect_multicam_node_now()
{
	if (get_connected_node()) {
		detect_multicam_node(get_connected_node()->get_playhead());
	}
}

void ViewerWidget::close_audio_processor()
{
	audio_processor_.close();
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
							if ((multicam = clip->find_multicam())) {
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
						if ((multicam = clip->find_multicam())) {
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
		RenderManager::instance()->get_cacher()->set_multicam_node(multicam);
	} else {
		RenderManager::instance()->get_cacher()->set_multicam_node(nullptr);
		if (multicam_panel_) {
			multicam_panel_->set_multicam_node(nullptr, nullptr, nullptr, time);
		}
	}
}

bool ViewerWidget::is_video_visible() const
{
	return get_connected_node()->get_video_params().video_type() !=
			   VideoParams::k_video_type_still &&
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
		get_connected_node()->set_waveform_enabled(waveform_view_->isVisible());

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

	RenderTicketWatcher *watcher = new RenderTicketWatcher(this);
	connect(watcher, &RenderTicketWatcher::finished, this,
			&ViewerWidget::received_audio_buffer_for_playback);
	audio_playback_queue_.push_back(watcher);
	watcher->set_ticket(RenderManager::instance()->get_cacher()->get_range_of_audio(
		get_connected_node(), TimeRange(audio_playback_queue_time_, queue_end)));

	audio_playback_queue_time_ = queue_end;
}

void ViewerWidget::received_audio_buffer_for_playback()
{
	while (!audio_playback_queue_.empty() &&
		   audio_playback_queue_.front()->has_result()) {
		RenderTicketWatcher *watcher = audio_playback_queue_.front();
		audio_playback_queue_.pop_front();

		if (watcher->has_result()) {
			SampleBuffer samples = watcher->get().value<SampleBuffer>();
			if (samples.is_allocated()) {
				// If the samples must be reversed, reverse them now
				if (playback_speed_ < 0) {
					samples.reverse();
				}

				// Convert to packed data for audio output
				AudioProcessor::Buffer buf;
				int r = audio_processor_.convert(samples.to_raw_ptrs().data(),
															 samples.sample_count(), &buf);

				// TempoProcessor may have emptied the array
				if (r >= 0) {
					if (!buf.empty()) {
						const QByteArray &pack = buf.at(0);
						if (prequeuing_audio_) {
							// Add to prequeued audio buffer
							prequeued_audio_.append(pack);
						} else {
							// Push directly to audio manager
							AudioManager::instance()->push_to_output(
								audio_processor_.to(), pack);
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

		delete watcher;
	}
}

void ViewerWidget::received_audio_buffer_for_scrubbing()
{
	RenderTicketWatcher *watcher = static_cast<RenderTicketWatcher *>(sender());

	while (!audio_scrub_watchers_.empty() &&
		   audio_scrub_watchers_.front() != watcher) {
		audio_scrub_watchers_.pop_front();
	}

	if (!audio_scrub_watchers_.empty()) {
		if (watcher->has_result()) {
			SampleBuffer samples = watcher->get().value<SampleBuffer>();
			if (samples.is_allocated()) {
				if (samples.audio_params().channel_count() > 0) {
					AudioProcessor::Buffer buf;
					int r =
						audio_processor_.convert(samples.to_raw_ptrs().data(),
												 samples.sample_count(), &buf);

					if (r >= 0) {
						if (!buf.empty()) {
							QString error;
							const QByteArray &packed = buf.at(0);
							AudioManager::instance()->clear_buffered_output();
							if (!AudioManager::instance()->push_to_output(
									audio_processor_.to(), packed, &error)) {
								Core::instance()->show_status_bar_message(
									tr("Audio scrubbing failed: %1").arg(error));
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
	}

	delete watcher;
}

void ViewerWidget::queue_starved()
{
	static const int k_maximum_wait_time_ms = 250;
	static const Rational k_maximum_wait_time(k_maximum_wait_time_ms, 1000);
	qint64 now = QDateTime::currentMSecsSinceEpoch();

	if (!queue_starved_start_) {
		queue_starved_start_ = now;
	} else if (now > queue_starved_start_ + k_maximum_wait_time_ms) {
		if (first_requeue_watcher_) {
			if (get_connected_node()->get_playhead() + k_maximum_wait_time <
				first_requeue_watcher_->property("time").value<Rational>()) {
				// We still have time
				return;
			}
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
	// watchers can re-enter the same RenderTicket mutex and deadlock.
	QMetaObject::invokeMethod(
		this, [this]() { force_requeue_from_current_time_internal(); },
		Qt::QueuedConnection);
}

void ViewerWidget::force_requeue_from_current_time_internal()
{
	// Allow half a second for requeue to complete
	static const Rational k_requeue_wait_time(1);

	RenderManager::instance()->get_cacher()->clear_single_frame_renders();
	queue_watchers_.clear();
	int queue = determine_playback_queue_size();
	playback_queue_next_frame_ =
		get_timestamp() +
		playback_speed_ * Timecode::time_to_timestamp(
							  k_requeue_wait_time, timebase(), Timecode::k_floor);
	;
	first_requeue_watcher_ = nullptr;
	for (int i = 0; i < queue; i++) {
		RenderTicketWatcher *watcher = request_next_frame_for_queue();
		if (!first_requeue_watcher_) {
			first_requeue_watcher_ = watcher;
			connect(first_requeue_watcher_, &RenderTicketWatcher::destroyed,
					this, &ViewerWidget::handle_first_requeue_destroy);
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
		RenderTicketWatcher *watcher = new RenderTicketWatcher();
		watcher->setProperty("start", QDateTime::currentMSecsSinceEpoch());
		watcher->setProperty("time", QVariant::fromValue(time));
		connect(watcher, &RenderTicketWatcher::finished, this,
				&ViewerWidget::renderer_generated_frame);
		nonqueue_watchers_.append(watcher);

		// Clear queue because we want this frame more than any others
		RenderManager::instance()
			->get_cacher()
			->clear_single_frame_renders_that_arent_running();

		detect_multicam_node(time);

		watcher->set_ticket(get_frame(time));
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
	RenderManager::instance()->get_cacher()->set_thumbnails_paused(true);

	RenderManager::instance()->set_aggressive_garbage_collection(true);

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
				get_connected_node()->set_playhead(0);
			} else {
				get_connected_node()->set_playhead(last_frame);
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

	AudioParams ap = get_connected_node()->get_audio_params();
	qDebug() << "ViewerWidget::PlayInternal: audio params valid=" << ap.is_valid()
			 << "channel_count=" << ap.channel_count();
	if (ap.is_valid() && ap.channel_count() != 0) {
		update_audio_processor();

		// Verify audio processor output params are valid before using them
		AudioParams output_params = audio_processor_.to();
		qDebug() << "ViewerWidget::PlayInternal: audio processor output params valid="
				 << output_params.is_valid();
		if (!output_params.is_valid()) {
			qWarning()
				<< "Audio processor output params are invalid, skipping audio playback";
		} else {
			AudioManager::instance()->set_output_notify_interval(
				output_params.time_to_bytes(k_audio_playback_interval));
			connect(AudioManager::instance(), &AudioManager::output_notify, this,
					&ViewerWidget::queue_next_audio_buffer);

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
		AudioManager::instance()->stop_recording();
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
		foreach (RenderTicketWatcher *watcher, queue_watchers_) {
			watcher->cancel();
		}
		qDeleteAll(queue_watchers_);
		queue_watchers_.clear();
		RenderManager::instance()->get_cacher()->clear_single_frame_renders();

		playback_backup_timer_.stop();

		// Handle audio
		AudioManager::instance()->stop_output();
		AudioMonitor::stop_on_all();
		prequeued_audio_.clear();
		disconnect(AudioManager::instance(), &AudioManager::output_notify, this,
				   &ViewerWidget::queue_next_audio_buffer);
		qDeleteAll(audio_playback_queue_);
		audio_playback_queue_.clear();
		update_audio_processor();

		RenderManager::instance()->get_cacher()->set_thumbnails_paused(false);

		update_texture_from_node();

		RenderManager::instance()->set_aggressive_garbage_collection(false);
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
			const AudioParams &params = get_connected_node()->get_audio_params();

			if (params.is_valid()) {
				// NOTE: Hardcoded scrubbing interval (20ms)
				Rational interval = Rational(20, 1000);

				RenderTicketWatcher *watcher = new RenderTicketWatcher();
				connect(watcher, &RenderTicketWatcher::finished, this,
						&ViewerWidget::received_audio_buffer_for_scrubbing);
				audio_scrub_watchers_.push_back(watcher);
				watcher->set_ticket(
					RenderManager::instance()->get_cacher()->get_range_of_audio(
						get_connected_node(),
						TimeRange(get_connected_node()->get_playhead(),
								  get_connected_node()->get_playhead() +
									  interval)));
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

void ViewerWidget::set_display_image(RenderTicketPtr ticket)
{
	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		QVariant push;
		if (ticket) {
			if (dynamic_cast<MulticamDisplay *>(dw)) {
				push = ticket->property("multicam_output");
			} else {
				push = ticket->get();
			}
		}
		dw->set_image(push);
	}
}

RenderTicketWatcher *ViewerWidget::request_next_frame_for_queue(bool increment)
{
	RenderTicketWatcher *watcher = nullptr;

	Rational next_time =
		Timecode::timestamp_to_time(playback_queue_next_frame_, timebase());

	if (frame_exists_at_time(next_time) || viewer_might_be_a_still()) {
		if (increment) {
			playback_queue_next_frame_ += playback_speed_;
		}

		watcher = new RenderTicketWatcher();
		watcher->setProperty("start", QDateTime::currentMSecsSinceEpoch());
		watcher->setProperty("time", QVariant::fromValue(next_time));
		detect_multicam_node(next_time);
		connect(watcher, &RenderTicketWatcher::finished, this,
				&ViewerWidget::renderer_generated_frame_for_queue);
		queue_watchers_.append(watcher);
		watcher->set_ticket(get_frame(next_time));
	}

	return watcher;
}

RenderTicketPtr ViewerWidget::get_frame(const Rational &t)
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
		// Frame has been cached, grab the frame
		RenderTicketPtr ticket = std::make_shared<RenderTicket>();
		ticket->setProperty("time", QVariant::fromValue(t));
		QtConcurrent::run(
			static_cast<void (*)(RenderTicketPtr, const QString &,
								 const QUuid &, const int64_t &)>(
				ViewerWidget::decode_cached_image),
			ticket,
			get_connected_node()->video_frame_cache()->get_cache_directory(),
			get_connected_node()->video_frame_cache()->get_uuid(),
			Timecode::time_to_timestamp(t, timebase(), Timecode::k_floor));
		return ticket;
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
	AudioManager::instance()->reset_output_clock();

	// Start audio waveform playback
	if (!prequeued_audio_.isEmpty()) {
		QString error;
		if (!AudioManager::instance()->push_to_output(audio_processor_.to(),
													prequeued_audio_, &error)) {
			QMessageBox::critical(
				this, tr("Audio Error"),
				tr("Failed to start audio: %1\n\n"
				   "Please check your audio preferences and try again.")
					.arg(error));
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

	auto vp = get_connected_node()->get_video_params();
	vp.set_divider(div);

	auto c = new NodeParamSetStandardValueCommand(
		NodeKeyframeTrackReference(
			NodeInput(get_connected_node(), ViewerOutput::k_video_params_input, 0)),
		QVariant::fromValue(vp));
	Core::instance()->undo_stack()->push(c, tr("Changed Playback Resolution"));
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
	RenderTicketWatcher *ticket = static_cast<RenderTicketWatcher *>(sender());

	if (nonqueue_watchers_.contains(ticket)) {
		while (!nonqueue_watchers_.isEmpty()) {
			// Pop frames that are "old"
			if (nonqueue_watchers_.takeFirst() == ticket) {
				break;
			}
		}

		if (ticket->has_result()) {
			set_display_image(ticket->get_ticket());
		}
	}

	delete ticket;
}

void ViewerWidget::renderer_generated_frame_for_queue()
{
	RenderTicketWatcher *watcher = static_cast<RenderTicketWatcher *>(sender());

	if (queue_watchers_.contains(watcher)) {
		queue_watchers_.removeOne(watcher);

		if (watcher->has_result()) {
			QVariant frame = watcher->get();
			bool drop_frame = false;

			// Ignore this signal if we've paused now
			if (is_playing() || prequeuing_video_) {
				const qint64 start_ms = watcher->property("start").toLongLong();
				const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
				const int playback_step = qMax(1, qAbs(playback_speed_));
				const double frame_interval_ms =
					qMax(1.0, timebase().to_double() * 1000.0 /
								  static_cast<double>(playback_step));
				if (start_ms > 0 && (now_ms - start_ms) > frame_interval_ms) {
					// If the queue is nearly empty, keep the frame anyway
					// to prevent the viewer from freezing entirely when
					// rendering can't keep up with playback speed.
					if (display_widget_->queue()->size() >= 2) {
						drop_frame = true;
					}
				}

				Rational ts = watcher->property("time").value<Rational>();

				if (!drop_frame) {
					foreach (ViewerDisplayWidget *dw, playback_devices_) {
						const bool is_multicam =
							dynamic_cast<MulticamDisplay *>(dw);
						QVariant push;
						if (is_multicam) {
							push = watcher->get_ticket()->property(
								"multicam_output");
							if (!push.isValid() || push.isNull()) {
								// Fall back to the primary frame when multicam isn't available.
								push = frame;
							}
						} else {
							push = frame;
						}

						dw->queue()->append_timewise({ ts, push },
													playback_speed_);
					}
				}

				if (prequeuing_video_) {
					prequeue_count_++;

					if (prequeue_count_ == prequeue_length_) {
						prequeuing_video_ = false;
						finish_play_preprocess();
					} else {
						// This call was mostly necessary to keep the threads busy between prequeue and playback.
						// If we only have a single render thread, it's no longer necessary.
						//RequestNextFrameForQueue();
					}
				}
			}
		}
	}

	if (first_requeue_watcher_ == watcher) {
		first_requeue_watcher_ = nullptr;
	}

	delete watcher;
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

			for (int d : VideoParams::k_supported_dividers) {
				playback_res_menu->add_action_with_data(
					VideoParams::get_name_for_divider(d), d,
					get_connected_node()->get_video_params().divider());
			}

			connect(playback_res_menu, &QMenu::triggered, this,
					&ViewerWidget::context_menu_set_playback_res);
		}

		{
			// Deinterlace Option
			if (get_connected_node()->get_video_params().interlacing() !=
				VideoParams::k_interlace_none) {
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
			get_connected_node()->set_playhead(
				get_connected_node()->get_work_area()->in());
		} else {
			in_to_out_only = false;
		}
	} else if (record_armed_) {
		disarm_recording();

		if (get_connected_node()->project()->filename().isEmpty()) {
			QMessageBox::critical(
				this, tr("Audio Recording"),
				tr("Project must be saved before you can record audio."));
			return;
		}

		QDir audio_path(QFileInfo(get_connected_node()->project()->filename())
							.dir()
							.filePath(tr("audio")));
		if (!audio_path.exists()) {
			audio_path.mkpath(QStringLiteral("."));
		}

		recording_filename_ = audio_path.filePath(QStringLiteral("%1.%2").arg(
			QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss"),
			ExportFormat::get_extension(static_cast<ExportFormat::Format>(
				OAK_CONFIG("AudioRecordingFormat").toInt()))));

		AudioParams ap(
			OAK_CONFIG("AudioRecordingSampleRate").toInt(),
			OAK_CONFIG("AudioRecordingChannelLayout").toULongLong(),
			SampleFormat::from_string(OAK_CONFIG("AudioRecordingSampleFormat")
										  .toString()
										  .toStdString()));

		EncodingParams encode_param;
		encode_param.enable_audio(
			ap, static_cast<ExportCodec::Codec>(
					OAK_CONFIG("AudioRecordingCodec").toInt()));
		encode_param.set_filename(recording_filename_);
		encode_param.set_audio_bit_rate(
			OAK_CONFIG("AudioRecordingBitRate").toInt() * 1000);

		QString error;
		if (AudioManager::instance()->start_recording(encode_param, &error)) {
			recording_ = true;
			controls_->set_pause_button_recording_state(true);
			recording_callback_->enable_recording_overlay(
				TimelineCoordinate(recording_range_.in(), recording_track_));
		} else {
			QMessageBox::critical(
				this, tr("Audio Recording"),
				tr("Failed to start audio recording: %1").arg(error));
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
	get_connected_node()->set_playhead(time_to_set);
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

void ViewerWidget::interlacing_changed_slot(VideoParams::Interlacing interlacing)
{
	// Automatically set a "sane" deinterlacing option
	bool deint = interlacing != VideoParams::k_interlace_none;

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_deinterlacing(deint);
	}
}

void ViewerWidget::update_renderer_video_parameters()
{
	VideoParams vp = get_connected_node()->get_video_params();

	foreach (ViewerDisplayWidget *dw, playback_devices_) {
		dw->set_video_params(vp);
	}
}

void ViewerWidget::update_renderer_audio_parameters()
{
	AudioParams ap = get_connected_node()->get_audio_params();

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
	if (event->mimeData()->formats().contains(Project::k_item_mime_type)) {
		event->accept();
	}
}

void ViewerWidget::dropped(QDropEvent *event)
{
	QByteArray mimedata = event->mimeData()->data(Project::k_item_mime_type);
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
