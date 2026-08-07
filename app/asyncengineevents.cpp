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

#include "asyncengineevents.h"

#include <cstring>

#include "oakengine/audio.h"

namespace olive
{

AsyncEngineEvents *AsyncEngineEvents::instance_ = nullptr;

AsyncEngineEvents::AsyncEngineEvents(QObject *parent) : QObject(parent)
{
	qRegisterMetaType<AsyncEngineEventData>();
	qRegisterMetaType<OakEngineTask *>();

	// Global async subscriptions.
	void *task_manager = oakengine_task_manager_handle();
	subscriptions_.append(oakengine_event_subscribe(
		task_manager, OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED, &on_event, this));
	subscriptions_.append(oakengine_event_subscribe(
		task_manager, OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED, &on_event, this));
	subscriptions_.append(oakengine_event_subscribe(
		task_manager, OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED, &on_event, this));
	subscriptions_.append(oakengine_event_subscribe(
		task_manager, OAKENGINE_EVENT_TASK_MANAGER_LIST_CHANGED, &on_event, this));
}

void AsyncEngineEvents::subscribe_audio_manager()
{
	void *audio_manager = oakengine_audio_manager_handle();
	if (!audio_manager) {
		return;
	}

	subscriptions_.append(oakengine_event_subscribe(
		audio_manager, OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED,
		&on_event, this));
	subscriptions_.append(oakengine_event_subscribe(
		audio_manager, OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_NOTIFY, &on_event,
		this));
}

AsyncEngineEvents::~AsyncEngineEvents()
{
	foreach (int64_t id, subscriptions_) {
		oakengine_event_unsubscribe(id);
	}
	for (auto it = task_subscriptions_.cbegin(); it != task_subscriptions_.cend();
		 ++it) {
		foreach (int64_t id, it.value()) {
			oakengine_event_unsubscribe(id);
		}
	}
}

void AsyncEngineEvents::create(QObject *parent)
{
	if (!instance_) {
		instance_ = new AsyncEngineEvents(parent);
	}
}

void AsyncEngineEvents::destroy()
{
	delete instance_;
	instance_ = nullptr;
}

int64_t AsyncEngineEvents::subscribe(void *handle, int32_t event_id)
{
	switch (event_id) {
	case OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED:
	case OAKENGINE_EVENT_PLAYBACK_CACHE_VALIDATED:
	case OAKENGINE_EVENT_FRAME_CACHE_INVALIDATED:
		break;
	default:
		return 0;
	}

	const int64_t id =
		oakengine_event_subscribe(handle, event_id, &on_event, this);
	if (id > 0) {
		subscriptions_.append(id);
	}
	return id;
}

void AsyncEngineEvents::unsubscribe(int64_t id)
{
	if (oakengine_event_unsubscribe(id) == OAKENGINE_OK) {
		subscriptions_.removeAll(id);
	}
}

void AsyncEngineEvents::on_event(const oakengine_event *event, void *userdata)
{
	AsyncEngineEvents *self = static_cast<AsyncEngineEvents *>(userdata);

	AsyncEngineEventData data;
	data.id = event->id;
	data.a = event->a;
	data.b = event->b;
	data.c = event->c;
	data.source = event->source;
	data.handle = event->handle;
	data.s = QString::fromUtf8(event->s ? event->s : "");

	QMetaObject::invokeMethod(self, "dispatch", Qt::QueuedConnection,
							  Q_ARG(olive::AsyncEngineEventData, data));
}

void AsyncEngineEvents::dispatch(const AsyncEngineEventData &data)
{
	switch (data.id) {
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_ADDED: {
		auto *task = static_cast<OakEngineTask *>(data.handle);
		emit task_manager_task_added(task, data.s);
		subscribe_task(task);
		break;
	}
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_REMOVED: {
		auto *task = static_cast<OakEngineTask *>(data.handle);
		emit task_manager_task_removed(task);
		unsubscribe_task(task);
		break;
	}
	case OAKENGINE_EVENT_TASK_MANAGER_TASK_FAILED:
		emit task_manager_task_failed(static_cast<OakEngineTask *>(data.handle));
		break;
	case OAKENGINE_EVENT_TASK_MANAGER_LIST_CHANGED:
		emit task_manager_list_changed();
		break;
	case OAKENGINE_EVENT_TASK_STARTED:
		emit task_started(static_cast<OakEngineTask *>(data.source), data.a);
		break;
	case OAKENGINE_EVENT_TASK_PROGRESS: {
		double d;
		static_assert(sizeof(d) == sizeof(data.a));
		memcpy(&d, &data.a, sizeof(d));
		emit task_progress(static_cast<OakEngineTask *>(data.source), d);
		break;
	}
	case OAKENGINE_EVENT_TASK_FINISHED:
		emit task_finished(static_cast<OakEngineTask *>(data.source), data.a != 0);
		break;
	case OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED:
		emit audio_output_params_changed();
		break;
	case OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_NOTIFY:
		emit audio_output_notify();
		break;
	case OAKENGINE_EVENT_PLAYBACK_CACHE_INVALIDATED:
		emit playback_cache_invalidated(data.source, data.a, data.b);
		break;
	case OAKENGINE_EVENT_PLAYBACK_CACHE_VALIDATED:
		emit playback_cache_validated(data.source, data.a, data.b);
		break;
	case OAKENGINE_EVENT_FRAME_CACHE_INVALIDATED:
		emit frame_cache_invalidated(data.source, data.a, data.b);
		break;
	default:
		break;
	}
}

void AsyncEngineEvents::subscribe_task(OakEngineTask *task)
{
	if (task_subscriptions_.contains(task)) {
		return;
	}

	QVector<int64_t> ids;
	ids.append(oakengine_event_subscribe(task, OAKENGINE_EVENT_TASK_STARTED,
										 &on_event, this));
	ids.append(oakengine_event_subscribe(task, OAKENGINE_EVENT_TASK_PROGRESS,
										 &on_event, this));
	ids.append(oakengine_event_subscribe(task, OAKENGINE_EVENT_TASK_FINISHED,
										 &on_event, this));
	task_subscriptions_.insert(task, ids);
}

void AsyncEngineEvents::unsubscribe_task(OakEngineTask *task)
{
	QVector<int64_t> ids = task_subscriptions_.take(task);
	foreach (int64_t id, ids) {
		oakengine_event_unsubscribe(id);
	}
}

} // namespace olive
