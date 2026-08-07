/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef ASYNCENGINEEVENTS_H
#define ASYNCENGINEEVENTS_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include "oakengine/events.h"
#include "oakengine/task.h"

namespace olive
{

/**
 * @brief POD transport struct queued from the engine C callback to the GUI thread.
 *
 * The C callback runs on the engine's emitting thread (task thread, audio thread,
 * render thread, etc.). It copies the event fields into this struct and queues
 * AsyncEngineEvents::dispatch() on the GUI thread. The string payload is copied
 * out so the original `event->s` buffer remains valid only during the callback.
 */
struct AsyncEngineEventData {
	int32_t id;
	int64_t a;
	int64_t b;
	int64_t c;
	void *source;
	void *handle;
	QString s;
};

/**
 * @brief Single async event dispatcher (issue 0b).
 *
 * The only engine -> app notification channel for truly asynchronous events:
 * TASK family, playback cache validated/invalidated, frame cache invalidated,
 * and audio manager output params/notify. The Core owns the singleton.
 *
 * Engine callbacks are invoked synchronously on the emitting thread. Each callback
 * only packages the event and queues AsyncEngineEvents::dispatch() to the GUI
 * thread with Qt::QueuedConnection; dispatch() then emits typed Qt signals.
 */
class AsyncEngineEvents : public QObject {
	Q_OBJECT
public:
	static AsyncEngineEvents *instance()
	{
		return instance_;
	}

	/**
	 * @brief Create the singleton. Must be called once from the GUI thread
	 * (Core does this).
	 */
	static void create(QObject *parent);

	/**
	 * @brief Destroy the singleton.
	 */
	static void destroy();

	/**
	 * @brief Subscribe to an async event on a specific handle.
	 *
	 * Only async event IDs are accepted; this is the narrow replacement for
	 * raw oakengine_event_subscribe() / EngineEventBridge::subscribe() at the
	 * remaining cache/task per-handle subscription points.
	 */
	int64_t subscribe(void *handle, int32_t event_id);

	/**
	 * @brief Cancel a subscription returned by subscribe().
	 */
	void unsubscribe(int64_t id);

	/**
	 * @brief Subscribe to audio-manager async events.
	 *
	 * The audio manager is created lazily (in GUI mode), so this is called
	 * separately once it exists.
	 */
	void subscribe_audio_manager();

signals:
	/* Task manager family (handle: oakengine_task_manager_handle()). */
	void task_manager_task_added(OakEngineTask *task, const QString &title);
	void task_manager_task_removed(OakEngineTask *task);
	void task_manager_task_failed(OakEngineTask *task);
	void task_manager_list_changed();

	/* Task family (handle: OakEngineTask*). */
	void task_started(OakEngineTask *task, qint64 start_time);
	void task_progress(OakEngineTask *task, double progress);
	void task_finished(OakEngineTask *task, bool succeeded);

	/* Audio manager family (handle: oakengine_audio_manager_handle()). */
	void audio_output_params_changed();
	void audio_output_notify();

	/* Playback cache / frame cache family. */
	void playback_cache_invalidated(void *cache, qint64 a, qint64 b);
	void playback_cache_validated(void *cache, qint64 a, qint64 b);
	void frame_cache_invalidated(void *cache, qint64 a, qint64 b);

private slots:
	void dispatch(const AsyncEngineEventData &data);

private:
	explicit AsyncEngineEvents(QObject *parent = nullptr);
	~AsyncEngineEvents() override;

	static void on_event(const oakengine_event *event, void *userdata);

	void subscribe_task(OakEngineTask *task);
	void unsubscribe_task(OakEngineTask *task);

	static AsyncEngineEvents *instance_;

	// Global async subscriptions (task manager, audio manager) and per-cache
	// subscriptions made through subscribe().
	QVector<int64_t> subscriptions_;

	// Per-task subscriptions managed internally when tasks are added/removed.
	QHash<OakEngineTask *, QVector<int64_t>> task_subscriptions_;
};

}

Q_DECLARE_METATYPE(olive::AsyncEngineEventData)

#endif // ASYNCENGINEEVENTS_H
