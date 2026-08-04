#include "playbackcontroller.h"

namespace olive
{

PlaybackController *PlaybackController::instance_ = nullptr;

PlaybackController *PlaybackController::instance()
{
	if (!instance_) {
		instance_ = new PlaybackController();
	}
	return instance_;
}

PlaybackController::PlaybackController(QObject *parent)
	: QObject(parent)
{
}

void PlaybackController::set_playhead(OakEngineNode *viewer,
									  const core::Rational &time)
{
	if (!viewer) {
		return;
	}
	oakengine_viewer_set_playhead(viewer, time.numerator(),
								  time.denominator());
	emit playhead_changed(viewer, time);
}

void PlaybackController::set_playhead(OakEngineNode *viewer, int64_t num,
									  int64_t den)
{
	set_playhead(viewer, core::Rational(num, den));
}

}
