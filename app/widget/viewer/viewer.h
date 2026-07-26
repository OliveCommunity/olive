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

#ifndef OAK_VIEWER_WIDGET_H
#define OAK_VIEWER_WIDGET_H

#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include "oakengine/audio.h"
#include "audiowaveformview.h"
#include "node/output/viewer/viewer.h"
#include "render/previewaudiodevice.h"
#include "oakengine/preview.h"
#include "viewerdisplay.h"
#include "viewersizer.h"
#include "viewerwindow.h"
#include "widget/playbackcontrols/playbackcontrols.h"
#include "widget/timebased/timebasedwidget.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

class EngineEventBridge;
class MulticamWidget;

/**
 * @brief An OpenGL-based viewer widget with playback controls (a PlaybackControls widget).
 */
class ViewerWidget : public TimeBasedWidget {
	Q_OBJECT
public:
	enum WaveformMode {
		k_wf_automatic,
		k_wf_viewer_only,
		k_wf_waveform_only,
		k_wf_viewer_and_waveform
	};

	ViewerWidget(QWidget *parent = nullptr)
		: ViewerWidget(new ViewerDisplayWidget(), parent)
	{
	}

	virtual ~ViewerWidget() override;

	void set_playback_controls_enabled(bool enabled);

	void set_time_ruler_enabled(bool enabled);

	void toggle_play_pause();

	bool is_playing() const;

	/**
   * @brief Enable or disable the color management menu
   *
   * While the Viewer is _always_ color managed, In some contexts, the color management may be controlled from an
   * external UI making the menu unnecessary.
   */
	void set_color_menu_enabled(bool enabled);

	void set_matrix(const QMatrix4x4 &mat);

	/**
   * @brief Creates a ViewerWindow widget and places it full screen on another screen
   *
   * If `screen` is nullptr, the screen will be automatically selected as whichever one contains the mouse cursor.
   */
	void set_full_screen(QScreen *screen = nullptr);

	OakEngineColorManager *color_manager() const
	{
		return display_widget_->color_manager();
	}

	void set_gizmos(Node *node);

	void start_capture(TimelineWidget *source, const TimeRange &time,
					  const Track::Reference &track);

	void set_audio_scrubbing_enabled(bool e)
	{
		enable_audio_scrubbing_ = e;
	}

	void add_playback_device(ViewerDisplayWidget *vw)
	{
		playback_devices_.push_back(vw);
	}

	void set_timeline_selected_blocks(const QVector<Block *> &b)
	{
		timeline_selected_blocks_ = b;

		if (!is_playing()) {
			// If is playing, this will happen by the next frame automatically
			detect_multicam_node_now();
			update_texture_from_node();
		}
	}

	void set_node_view_selections(const QVector<OakEngineNode *> &n)
	{
		node_view_selected_.clear();
		node_view_selected_.reserve(n.size());
		foreach (OakEngineNode *handle, n) {
			node_view_selected_.append(reinterpret_cast<Node *>(handle));
		}

		if (!is_playing()) {
			// If is playing, this will happen by the next frame automatically
			detect_multicam_node_now();
			update_texture_from_node();
		}
	}

	void connect_multicam_widget(MulticamWidget *p);

public slots:
	void play(bool in_to_out_only);

	void play();

	void pause();

	void shuttle_left();

	void shuttle_stop();

	void shuttle_right();

	void set_color_transform(const ColorTransform &transform);

	/**
   * @brief Wrapper for ViewerGLWidget::SetSignalCursorColorEnabled()
   */
	void set_signal_cursor_color_enabled(bool e);

	void cache_entire_sequence();

	void cache_sequence_in_out();

	void set_viewer_resolution(int width, int height);

	void set_viewer_pixel_aspect(const Rational &ratio);

	void update_texture_from_node();

	void request_start_editing_text()
	{
		display_widget_->request_start_editing_text();
	}

signals:
	/**
   * @brief Wrapper for ViewerGLWidget::CursorColor()
   */
	void cursor_color(const Color &reference, const Color &display);

	/**
   * @brief Signal emitted when a new frame is loaded
   */
	void texture_changed(void *t);

	/**
   * @brief Wrapper for ViewerGLWidget::ColorProcessorChanged()
   */
	void color_processor_changed(ColorProcessorHandlePtr processor);

	/**
   * @brief Wrapper for ViewerGLWidget::ColorManagerChanged()
   */
	void color_manager_changed(OakEngineColorManager *color_manager);

protected:
	ViewerWidget(ViewerDisplayWidget *display, QWidget *parent = nullptr);

	virtual void TimebaseChangedEvent(const Rational &) override;
	virtual void TimeChangedEvent(const Rational &time) override;

	virtual void ConnectNodeEvent(ViewerOutput *) override;
	virtual void DisconnectNodeEvent(ViewerOutput *) override;
	virtual void ConnectedNodeChangeEvent(ViewerOutput *) override;
	virtual void ConnectedWorkAreaChangeEvent(TimelineWorkArea *) override;
	virtual void ConnectedMarkersChangeEvent(TimelineMarkerList *) override;

	virtual void ScaleChangedEvent(const double &s) override;

	virtual void resizeEvent(QResizeEvent *event) override;

	PlaybackControls *controls_;

	ViewerDisplayWidget *display_widget() const
	{
		return display_widget_;
	}

	// Overlay (info chip + zoom/safe-frame buttons) per the UI design reference
	void create_overlay();
	void position_overlay();
	void update_info_chip();

	void IgnoreNextScrubEvent()
	{
		ignore_scrub_++;
	}

	OakEnginePreviewRequest *get_single_frame(const Rational &t, bool dry = false);

	void set_waveform_mode(WaveformMode wf);

private slots:
	void overlay_zoom_in();
	void overlay_zoom_out();
	void overlay_zoom_fit();
	void overlay_toggle_safe_frame();

private:
	int64_t get_timestamp() const
	{
		return Timecode::time_to_timestamp(get_connected_node()->get_playhead(),
										   timebase(), Timecode::k_floor);
	}

	void update_time_internal(int64_t i);

	void play_internal(int speed, bool in_to_out_only);

	void pause_internal();

	void push_scrubbed_audio();

	void update_minimum_scale();

	void set_color_transform(const ColorTransform &transform,
						   ViewerDisplayWidget *sender);

	QString get_cached_filename_from_time(const Rational &time);

	bool frame_exists_at_time(const Rational &time);

	bool viewer_might_be_a_still();

	void set_display_image(RenderTicketPtr ticket);
	void set_display_image(OakEnginePreviewRequest *req);

	OakEnginePreviewRequest *request_next_frame_for_queue(bool increment = true);

	OakEnginePreviewRequest *get_frame(const Rational &t);

	void finish_play_preprocess();

	int determine_playback_queue_size();

	bool should_force_waveform() const;

	void set_empty_image();

	void update_auto_cacher();

	void decrement_prequeued_audio();

	void arm_for_recording();

	void disarm_recording();

	void close_audio_processor();

	void detect_multicam_node(const Rational &time);

	bool is_video_visible() const;

	ViewerSizer *sizer_;

	QWidget *overlay_;
	QLabel *info_chip_;
	QToolButton *safe_frame_btn_;
	int overlay_zoom_index_;

	int playback_speed_;

	Rational last_time_;

	bool color_menu_enabled_;

	bool time_changed_from_timer_;

	bool play_in_to_out_only_;

	AudioWaveformView *waveform_view_;

	QHash<QScreen *, ViewerWindow *> windows_;

	ViewerDisplayWidget *display_widget_;

	ViewerDisplayWidget *context_menu_widget_;

	QTimer playback_backup_timer_;

	int64_t playback_queue_next_frame_;
	int64_t dry_run_next_frame_;
	QVector<ViewerDisplayWidget *> playback_devices_;

	bool prequeuing_video_;
	int prequeuing_audio_;

	QList<OakEnginePreviewRequest *> nonqueue_watchers_;

	Rational last_length_;

	int prequeue_length_;
	int prequeue_count_;

	QVector<OakEnginePreviewRequest *> queue_watchers_;

	std::list<OakEnginePreviewRequest *> audio_playback_queue_;
	Rational audio_playback_queue_time_;
	OakEngineAudioProcessor *audio_processor_;
	QByteArray prequeued_audio_;
	static const Rational k_audio_playback_interval;

	static QVector<ViewerWidget *> instances;

	std::list<OakEnginePreviewRequest *> audio_scrub_watchers_;

	bool record_armed_;
	bool recording_;
	TimelineWidget *recording_callback_;
	TimeRange recording_range_;
	Track::Reference recording_track_;
	QString recording_filename_;

	qint64 queue_starved_start_;
	OakEnginePreviewRequest *first_requeue_watcher_;

	bool enable_audio_scrubbing_;

	WaveformMode waveform_mode_;

	QVector<OakEnginePreviewRequest *> dry_run_watchers_;

	int ignore_scrub_;

	QVector<Block *> timeline_selected_blocks_;
	QVector<Node *> node_view_selected_;

	MulticamWidget *multicam_panel_;

	EngineEventBridge *bridge_;

	int64_t audio_notify_sub_ = 0;

private slots:
	void playback_timer_update();

	void length_changed_slot(const Rational &length);

	void interlacing_changed_slot(int interlacing);

	void update_renderer_video_parameters();

	void update_renderer_audio_parameters();

	void show_context_menu(const QPoint &pos);

	void set_zoom_from_menu(QAction *action);

	void update_waveform_view_from_mode();

	void context_menu_set_full_screen(QAction *action);

	void context_menu_set_playback_res(QAction *action);

	void context_menu_disable_safe_margins();

	void context_menu_set_safe_margins();

	void context_menu_set_custom_safe_margins();

	void window_about_to_close();

	void renderer_generated_frame();

	void renderer_generated_frame_for_queue();

	void viewer_invalidated_video_range(const olive::TimeRange &range);

	void update_waveform_mode_from_menu(QAction *a);

	void drag_entered(QDragEnterEvent *event);

	void dropped(QDropEvent *event);

	void queue_next_audio_buffer();

	void received_audio_buffer_for_playback();

	void received_audio_buffer_for_scrubbing();

	void queue_starved();
	void queue_no_longer_starved();

	void force_requeue_from_current_time();
	void force_requeue_from_current_time_internal();

	void update_audio_processor();

	void create_addable_at(const QRectF &f);

	void handle_first_requeue_destroy();

	void show_subtitle_properties();

	void dry_run_finished();

	void request_next_dry_run();

	void save_frame_as_image();

	void detect_multicam_node_now();
};

}

#endif // OAK_VIEWER_WIDGET_H
