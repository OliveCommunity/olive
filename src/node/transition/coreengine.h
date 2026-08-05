#pragma once
// Transitional stub for engine/coreengine.h (still Qt-based). Only the
// surface oaknode uses; clipboard access stays a no-op until M9 wires the
// real engine boundary. Clipboard strings are std::string (de-Qt'd at this
// boundary per the ADAPT(M9) notes in serializer.cpp).
#include <string>
#include "olive/core/util/timecodefunctions.h"
#include "undostack.h"
namespace olive {
class EngineCore {
public:
	static EngineCore *instance()
	{
		static EngineCore e;
		return &e;
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
