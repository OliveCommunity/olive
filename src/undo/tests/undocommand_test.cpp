/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "undo/undocommand.h"

#include <gtest/gtest.h>

namespace
{

struct CommandLog {
	int redo_count = 0;
	int undo_count = 0;
	int free_count = 0;
};

OakUndoCommandVtable make_vtable()
{
	OakUndoCommandVtable vtable;
	vtable.redo = [](void *userdata) {
		static_cast<CommandLog *>(userdata)->redo_count++;
	};
	vtable.undo = [](void *userdata) {
		static_cast<CommandLog *>(userdata)->undo_count++;
	};
	vtable.free_fn = [](void *userdata) {
		static_cast<CommandLog *>(userdata)->free_count++;
	};
	return vtable;
}

OakUndoCommand make_command(CommandLog &log)
{
	OakUndoCommandVtable vtable = make_vtable();
	OakUndoCommand command = oakundo_command_init(&vtable, &log);
	EXPECT_NE(command.ctx, nullptr);
	return command;
}

}

TEST(OakUndoCommand, InitFree)
{
	CommandLog log;
	OakUndoCommand command = make_command(log);
	oakundo_command_free(&command);
	EXPECT_EQ(log.free_count, 1);
}

TEST(OakUndoCommand, InitNullVtableFails)
{
	EXPECT_EQ(oakundo_command_init(nullptr, nullptr).ctx, nullptr);
}

TEST(OakUndoCommand, FreeNullIsNoOp)
{
	{ OakUndoCommand h = {}; oakundo_command_free(&h); }
}

TEST(OakUndoCommand, RedoUndoNowRoundtrip)
{
	CommandLog log;
	OakUndoCommand command = make_command(log);

	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(log.redo_count, 1);
	// redo_now on a done command is a no-op
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(log.redo_count, 1);

	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(log.undo_count, 1);
	// undo_now on an undone command is a no-op
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	EXPECT_EQ(log.undo_count, 1);

	oakundo_command_free(&command);
}

TEST(OakUndoCommand, RedoNowNullHandleFails)
{
	EXPECT_EQ(oakundo_command_redo_now(OakUndoCommand{}),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_command_undo_now(OakUndoCommand{}),
			  OAKUNDO_E_INVALID);
}

TEST(OakUndoCommand, NullCallbacksAreNoOp)
{
	OakUndoCommandVtable vtable;
	vtable.redo = nullptr;
	vtable.undo = nullptr;
	vtable.free_fn = nullptr;

	OakUndoCommand command = oakundo_command_init(&vtable, nullptr);
	ASSERT_NE(command.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(command), OAKUNDO_OK);
	EXPECT_EQ(oakundo_command_undo_now(command), OAKUNDO_OK);
	oakundo_command_free(&command);
}

TEST(OakUndoCommand, MultiAddChildAndCount)
{
	CommandLog log_a;
	CommandLog log_b;
	OakUndoCommand multi = oakundo_command_init_multi();
	ASSERT_NE(multi.ctx, nullptr);

	int count = -1;
	EXPECT_EQ(oakundo_command_multi_child_count(multi, &count), OAKUNDO_OK);
	EXPECT_EQ(count, 0);

	EXPECT_EQ(oakundo_command_multi_add_child(multi, make_command(log_a)),
			  OAKUNDO_OK);
	EXPECT_EQ(oakundo_command_multi_add_child(multi, make_command(log_b)),
			  OAKUNDO_OK);

	EXPECT_EQ(oakundo_command_multi_child_count(multi, &count), OAKUNDO_OK);
	EXPECT_EQ(count, 2);

	// Multi redo/undo forward to children.
	EXPECT_EQ(oakundo_command_redo_now(multi), OAKUNDO_OK);
	EXPECT_EQ(log_a.redo_count, 1);
	EXPECT_EQ(log_b.redo_count, 1);
	EXPECT_EQ(oakundo_command_undo_now(multi), OAKUNDO_OK);
	EXPECT_EQ(log_a.undo_count, 1);
	EXPECT_EQ(log_b.undo_count, 1);

	// Freeing the multi frees the children it owns.
	oakundo_command_free(&multi);
	EXPECT_EQ(log_a.free_count, 1);
	EXPECT_EQ(log_b.free_count, 1);
}

TEST(OakUndoCommand, MultiChildBorrowedHandle)
{
	CommandLog log;
	OakUndoCommand multi = oakundo_command_init_multi();
	ASSERT_NE(multi.ctx, nullptr);
	ASSERT_EQ(oakundo_command_multi_add_child(multi, make_command(log)),
			  OAKUNDO_OK);

	OakUndoCommand child = {};
	EXPECT_EQ(oakundo_command_multi_child(multi, 0, &child), OAKUNDO_OK);
	ASSERT_NE(child.ctx, nullptr);
	EXPECT_EQ(oakundo_command_redo_now(child), OAKUNDO_OK);
	EXPECT_EQ(log.redo_count, 1);

	// Freeing the borrowed wrapper must not free the underlying command.
	oakundo_command_free(&child);
	EXPECT_EQ(log.free_count, 0);

	oakundo_command_free(&multi);
	EXPECT_EQ(log.free_count, 1);
}

TEST(OakUndoCommand, MultiErrorPaths)
{
	CommandLog log;
	OakUndoCommand multi = oakundo_command_init_multi();
	ASSERT_NE(multi.ctx, nullptr);
	OakUndoCommand plain = make_command(log);

	int count = 0;
	OakUndoCommand child = {};

	EXPECT_EQ(oakundo_command_multi_add_child(OakUndoCommand{}, plain),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_command_multi_add_child(multi, OakUndoCommand{}),
			  OAKUNDO_E_INVALID);
	// plain is not a multi command
	EXPECT_EQ(oakundo_command_multi_add_child(plain, make_command(log)),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_command_multi_child_count(OakUndoCommand{}, &count),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_command_multi_child_count(multi, nullptr),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_command_multi_child(multi, 0, nullptr),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_command_multi_child(multi, 5, &child),
			  OAKUNDO_E_NOT_FOUND);
	EXPECT_EQ(oakundo_command_multi_child(multi, -1, &child),
			  OAKUNDO_E_NOT_FOUND);

	oakundo_command_free(&multi);
	oakundo_command_free(&plain);
}
