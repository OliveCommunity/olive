#ifndef OAK_PLAYBACKCONTROLLER_H
#define OAK_PLAYBACKCONTROLLER_H

#include <QObject>

#include "oakengine/viewer.h"
#include "olive/core/util/rational.h"

namespace olive
{

/**
 * @brief App-internal hub for playhead changes (issue 0c of the
 * EventBridge elimination plan).
 *
 * Every app-side `oakengine_viewer_set_playhead` call site goes through
 * set_playhead(), which forwards to the engine and re-broadcasts
 * playhead_changed as a plain Qt signal. Widgets subscribe to that signal
 * instead of OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED via EngineEventBridge
 * or raw oakengine_event_subscribe callbacks.
 */
class PlaybackController : public QObject {
	Q_OBJECT
public:
	/**
	 * @brief Lazily created process-wide instance (leaked at exit, like
	 * Core). Widget tests construct widgets without Core::Start(), so the
	 * instance must not depend on explicit startup.
	 */
	static PlaybackController *instance();

	/**
	 * @brief Move the playhead and notify app subscribers.
	 */
	void set_playhead(OakEngineNode *viewer, const core::Rational &time);
	void set_playhead(OakEngineNode *viewer, int64_t num, int64_t den);

signals:
	void playhead_changed(OakEngineNode *viewer,
						  const olive::core::Rational &time);

private:
	explicit PlaybackController(QObject *parent = nullptr);

	static PlaybackController *instance_;
};

}

#endif // OAK_PLAYBACKCONTROLLER_H
