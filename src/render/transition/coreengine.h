#pragma once
// Transitional: engine/coreengine.h belongs to the facade layer and is not
// available to the de-Qt render/node libraries. Union of the surfaces
// oakrender (warn_cache_full, diskmanager.cpp) and oaknode (undo_stack,
// timecode display, clipboard) use; the real implementation lives on the
// facade side. Superset of the former src/node/transition stub.
#include <string>

#include "olive/core/util/timecodefunctions.h"
#include "undostack.h"

namespace olive
{

class EngineCore {
public:
	static EngineCore *instance()
	{
		static EngineCore e;
		return &e;
	}

	void warn_cache_full()
	{
	}

	core::Timecode::Display get_timecode_display() const
	{
		return core::Timecode::k_timecode_seconds;
	}

	UndoStack *undo_stack()
	{
		return &undo_stack_;
	}

	static void copy_string_to_clipboard(const std::string &) {}
	static std::string paste_string_from_clipboard() { return std::string(); }

private:
	UndoStack undo_stack_;
};

}
