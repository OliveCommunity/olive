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

#include "mainwindowundo.h"

#include "core.h"
#include "window/mainwindow/mainwindow.h"

#include "oakengine/undo.h"
namespace olive
{

namespace {

struct OpenCloseSequenceData {
	Sequence *sequence;
};

void open_sequence_redo(void *userdata)
{
	auto *d = static_cast<OpenCloseSequenceData *>(userdata);
	Core::instance()->main_window()->open_sequence(d->sequence);
}

void open_sequence_undo(void *userdata)
{
	auto *d = static_cast<OpenCloseSequenceData *>(userdata);
	Core::instance()->main_window()->close_sequence(d->sequence);
}

void close_sequence_redo(void *userdata)
{
	auto *d = static_cast<OpenCloseSequenceData *>(userdata);
	Core::instance()->main_window()->close_sequence(d->sequence);
}

void close_sequence_undo(void *userdata)
{
	auto *d = static_cast<OpenCloseSequenceData *>(userdata);
	Core::instance()->main_window()->open_sequence(d->sequence);
}

void open_close_sequence_free(void *userdata)
{
	delete static_cast<OpenCloseSequenceData *>(userdata);
}

} // anonymous namespace

void *make_open_sequence_command(Sequence *sequence)
{
	auto *d = new OpenCloseSequenceData;
	d->sequence = sequence;
	return oakengine_undo_command_create(nullptr, open_sequence_redo,
										 open_sequence_undo,
										 open_close_sequence_free, d);
}

void *make_close_sequence_command(Sequence *sequence)
{
	auto *d = new OpenCloseSequenceData;
	d->sequence = sequence;
	return oakengine_undo_command_create(nullptr, close_sequence_redo,
										 close_sequence_undo,
										 open_close_sequence_free, d);
}

}
