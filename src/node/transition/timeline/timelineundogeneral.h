#pragma once
// Transitional stub for engine/timeline/timelineundogeneral.h (still
// Qt-based). Only the surface oaknode uses. M4/M7 replace this with the real
// oaktimeline boundary.
#include "undocommand.h"
namespace olive {
class TrackList;
class TimelineAddTrackCommand : public UndoCommand {
public:
	explicit TimelineAddTrackCommand(TrackList *) {}
protected:
	void redo() override {}
	void undo() override {}
};
}
