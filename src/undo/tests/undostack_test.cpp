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

#include "undo/undostack.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "../src/undostack.h"

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

int64_t stack_index(OakUndoStack stack)
{
	int64_t index = -1;
	EXPECT_EQ(oakundo_undostack_index(stack, &index), OAKUNDO_OK);
	return index;
}

int64_t stack_count(OakUndoStack stack)
{
	int64_t count = -1;
	EXPECT_EQ(oakundo_undostack_count(stack, &count), OAKUNDO_OK);
	return count;
}

int stack_can_undo(OakUndoStack stack)
{
	int value = -1;
	EXPECT_EQ(oakundo_undostack_can_undo(stack, &value), OAKUNDO_OK);
	return value;
}

int stack_can_redo(OakUndoStack stack)
{
	int value = -1;
	EXPECT_EQ(oakundo_undostack_can_redo(stack, &value), OAKUNDO_OK);
	return value;
}

}

TEST(OakUndoStack, InitFree)
{
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	// A fresh stack holds the "New/Open Project" empty command.
	EXPECT_EQ(stack_count(stack), 1);
	EXPECT_EQ(stack_index(stack), 1);
	EXPECT_EQ(stack_can_undo(stack), 0);
	EXPECT_EQ(stack_can_redo(stack), 0);

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, FreeNullIsNoOp)
{
	{ OakUndoStack h = {}; oakundo_undostack_free(&h); }
}

TEST(OakUndoStack, NullHandleFails)
{
	CommandLog log;
	int64_t value64 = 0;
	int value = 0;
	char buf[8];

	EXPECT_EQ(oakundo_undostack_push(OakUndoStack{}, make_command(log), "x"),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_push_pre_executed(OakUndoStack{}, make_command(log),
												  "x"),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_undo(OakUndoStack{}), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_redo(OakUndoStack{}), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_jump(OakUndoStack{}, 0), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_clear(OakUndoStack{}), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_can_undo(OakUndoStack{}, &value), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_can_redo(OakUndoStack{}, &value), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_count(OakUndoStack{}, &value64), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_index(OakUndoStack{}, &value64), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_command_text(OakUndoStack{}, 0, buf, sizeof(buf)),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_command_is_done(OakUndoStack{}, 0, &value),
			  OAKUNDO_E_INVALID);

	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);
	EXPECT_EQ(oakundo_undostack_push(stack, OakUndoCommand{}, "x"), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_push_pre_executed(stack, OakUndoCommand{}, "x"),
			  OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_can_undo(stack, nullptr), OAKUNDO_E_INVALID);
	EXPECT_EQ(oakundo_undostack_count(stack, nullptr), OAKUNDO_E_INVALID);
	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, PushUndoRedoRoundtrip)
{
	CommandLog log;
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	ASSERT_EQ(oakundo_undostack_push(stack, make_command(log), "edit"),
			  OAKUNDO_OK);
	EXPECT_EQ(log.redo_count, 1);
	EXPECT_EQ(stack_count(stack), 2);
	EXPECT_EQ(stack_index(stack), 2);
	EXPECT_EQ(stack_can_undo(stack), 1);
	EXPECT_EQ(stack_can_redo(stack), 0);

	ASSERT_EQ(oakundo_undostack_undo(stack), OAKUNDO_OK);
	EXPECT_EQ(log.undo_count, 1);
	EXPECT_EQ(stack_index(stack), 1);
	EXPECT_EQ(stack_can_undo(stack), 0);
	EXPECT_EQ(stack_can_redo(stack), 1);

	ASSERT_EQ(oakundo_undostack_redo(stack), OAKUNDO_OK);
	EXPECT_EQ(log.redo_count, 2);
	EXPECT_EQ(stack_index(stack), 2);
	EXPECT_EQ(stack_can_undo(stack), 1);
	EXPECT_EQ(stack_can_redo(stack), 0);

	oakundo_undostack_free(&stack);
	EXPECT_EQ(log.free_count, 1);
}

TEST(OakUndoStack, PushClearsRedoable)
{
	CommandLog log_a;
	CommandLog log_b;
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	ASSERT_EQ(oakundo_undostack_push(stack, make_command(log_a), "a"),
			  OAKUNDO_OK);
	ASSERT_EQ(oakundo_undostack_undo(stack), OAKUNDO_OK);
	ASSERT_EQ(stack_can_redo(stack), 1);

	// Pushing a new command deletes the redoable one.
	ASSERT_EQ(oakundo_undostack_push(stack, make_command(log_b), "b"),
			  OAKUNDO_OK);
	EXPECT_EQ(log_a.free_count, 1);
	EXPECT_EQ(stack_can_redo(stack), 0);
	EXPECT_EQ(stack_count(stack), 2);

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, PushPreExecutedSkipsRedo)
{
	CommandLog log;
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	ASSERT_EQ(oakundo_undostack_push_pre_executed(stack, make_command(log),
												  "done elsewhere"),
			  OAKUNDO_OK);
	EXPECT_EQ(log.redo_count, 0);
	EXPECT_EQ(stack_index(stack), 2);

	// Undo and redo still work afterwards.
	ASSERT_EQ(oakundo_undostack_undo(stack), OAKUNDO_OK);
	EXPECT_EQ(log.undo_count, 1);
	ASSERT_EQ(oakundo_undostack_redo(stack), OAKUNDO_OK);
	EXPECT_EQ(log.redo_count, 1);

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, EmptyMultiIsDiscarded)
{
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	OakUndoCommand multi = oakundo_command_init_multi();
	ASSERT_NE(multi.ctx, nullptr);
	ASSERT_EQ(oakundo_undostack_push(stack, multi, "empty"), OAKUNDO_OK);
	EXPECT_EQ(stack_count(stack), 1);

	multi = oakundo_command_init_multi();
	ASSERT_NE(multi.ctx, nullptr);
	ASSERT_EQ(oakundo_undostack_push_pre_executed(stack, multi, "empty"),
			  OAKUNDO_OK);
	EXPECT_EQ(stack_count(stack), 1);

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, Jump)
{
	CommandLog logs[3];
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	for (int i = 0; i < 3; i++) {
		char name[8];
		snprintf(name, sizeof(name), "cmd%d", i);
		ASSERT_EQ(oakundo_undostack_push(stack, make_command(logs[i]), name),
				  OAKUNDO_OK);
	}
	ASSERT_EQ(stack_index(stack), 4);

	// Jump back to 1: undo all three commands.
	ASSERT_EQ(oakundo_undostack_jump(stack, 1), OAKUNDO_OK);
	EXPECT_EQ(stack_index(stack), 1);
	for (int i = 0; i < 3; i++) {
		EXPECT_EQ(logs[i].undo_count, 1);
	}
	EXPECT_EQ(stack_can_redo(stack), 1);

	// Jump forward to 3: redo the first two undone commands.
	ASSERT_EQ(oakundo_undostack_jump(stack, 3), OAKUNDO_OK);
	EXPECT_EQ(stack_index(stack), 3);
	EXPECT_EQ(logs[0].redo_count, 2);
	EXPECT_EQ(logs[1].redo_count, 2);
	EXPECT_EQ(logs[2].redo_count, 1);

	// Negative index clamps to 0; the bottom "New/Open Project" empty
	// command is not undoable, so the jump stops at index 1.
	ASSERT_EQ(oakundo_undostack_jump(stack, -5), OAKUNDO_OK);
	EXPECT_EQ(stack_index(stack), 1);

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, Clear)
{
	CommandLog log;
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	ASSERT_EQ(oakundo_undostack_push(stack, make_command(log), "edit"),
			  OAKUNDO_OK);
	ASSERT_EQ(oakundo_undostack_clear(stack), OAKUNDO_OK);

	EXPECT_EQ(log.free_count, 1);
	EXPECT_EQ(stack_count(stack), 1);
	EXPECT_EQ(stack_can_undo(stack), 0);
	EXPECT_EQ(stack_can_redo(stack), 0);

	char buf[32];
	ASSERT_EQ(oakundo_undostack_command_text(stack, 0, buf, sizeof(buf)),
			  int(strlen("New/Open Project")) + 1);
	EXPECT_STREQ(buf, "New/Open Project");

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, CommandTextTwoStage)
{
	CommandLog log;
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	ASSERT_EQ(oakundo_undostack_push(stack, make_command(log), "hello"),
			  OAKUNDO_OK);

	// Stage one: query the required size.
	int required = oakundo_undostack_command_text(stack, 1, nullptr, 0);
	EXPECT_EQ(required, int(strlen("hello")) + 1);

	// Stage two: copy into an exact buffer.
	std::vector<char> buf(required);
	EXPECT_EQ(oakundo_undostack_command_text(stack, 1, buf.data(), required),
			  required);
	EXPECT_STREQ(buf.data(), "hello");

	// Truncating buffer still reports the full required size.
	char small[3];
	EXPECT_EQ(oakundo_undostack_command_text(stack, 1, small, sizeof(small)),
			  required);
	EXPECT_EQ(small[sizeof(small) - 1], '\0');

	// Invalid rows.
	EXPECT_EQ(oakundo_undostack_command_text(stack, 2, nullptr, 0),
			  OAKUNDO_E_NOT_FOUND);
	EXPECT_EQ(oakundo_undostack_command_text(stack, -1, nullptr, 0),
			  OAKUNDO_E_NOT_FOUND);

	// NULL name behaves like an empty label.
	ASSERT_EQ(oakundo_undostack_push(stack, make_command(log), nullptr),
			  OAKUNDO_OK);
	EXPECT_EQ(oakundo_undostack_command_text(stack, 2, nullptr, 0), 1);

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, CommandIsDone)
{
	CommandLog log;
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	ASSERT_EQ(oakundo_undostack_push(stack, make_command(log), "edit"),
			  OAKUNDO_OK);
	ASSERT_EQ(oakundo_undostack_undo(stack), OAKUNDO_OK);

	int value = -1;
	EXPECT_EQ(oakundo_undostack_command_is_done(stack, 0, &value), OAKUNDO_OK);
	EXPECT_EQ(value, 1);
	EXPECT_EQ(oakundo_undostack_command_is_done(stack, 1, &value), OAKUNDO_OK);
	EXPECT_EQ(value, 0);

	EXPECT_EQ(oakundo_undostack_command_is_done(stack, 2, &value),
			  OAKUNDO_E_NOT_FOUND);
	EXPECT_EQ(oakundo_undostack_command_is_done(stack, -1, &value),
			  OAKUNDO_E_NOT_FOUND);
	EXPECT_EQ(oakundo_undostack_command_is_done(stack, 0, nullptr),
			  OAKUNDO_E_INVALID);

	oakundo_undostack_free(&stack);
}

TEST(OakUndoStack, MaxUndoCommands)
{
	OakUndoStack stack = oakundo_undostack_init();
	ASSERT_NE(stack.ctx, nullptr);

	// The fresh stack holds 1 empty command; push well past the 200 cap.
	std::vector<std::unique_ptr<CommandLog>> logs;
	for (int i = 0; i < 250; i++) {
		logs.emplace_back(new CommandLog());
		ASSERT_EQ(oakundo_undostack_push(stack, make_command(*logs.back()),
										 "edit"),
				  OAKUNDO_OK);
	}

	EXPECT_EQ(stack_count(stack), 200);
	EXPECT_EQ(stack_index(stack), 200);
	// The empty "New/Open Project" command was evicted, so everything left
	// is undoable.
	EXPECT_EQ(stack_can_undo(stack), 1);

	// The 51 oldest commands (empty + first 50 pushed) were freed.
	int freed = 0;
	for (int i = 0; i < 50; i++) {
		freed += logs[i]->free_count;
	}
	EXPECT_EQ(freed, 50);

	// Undo all 200 entries: the bottom one is not an EmptyCommand.
	for (int i = 0; i < 200; i++) {
		EXPECT_EQ(stack_can_undo(stack), 1);
		ASSERT_EQ(oakundo_undostack_undo(stack), OAKUNDO_OK);
	}
	EXPECT_EQ(stack_can_undo(stack), 0);
	EXPECT_EQ(stack_count(stack), 200);

	oakundo_undostack_free(&stack);
}

namespace
{

class FakeCommand : public olive::UndoCommand {
public:
	FakeCommand(int *redo_count, int *undo_count)
		: redo_count_(redo_count), undo_count_(undo_count)
	{
	}

protected:
	virtual void redo() override
	{
		(*redo_count_)++;
	}
	virtual void undo() override
	{
		(*undo_count_)++;
	}

private:
	int *redo_count_;
	int *undo_count_;
};

}

TEST(OakUndoStack, CppIndexChangedCallback)
{
	olive::UndoStack stack;

	std::vector<int> notifications;
	stack.set_index_changed_callback(
		[&notifications](int index) { notifications.push_back(index); });

	// The callback only fires on mutations after it is attached.
	int redo_count = 0;
	int undo_count = 0;
	stack.push(new FakeCommand(&redo_count, &undo_count), "fake");
	ASSERT_EQ(notifications.size(), 1u);
	EXPECT_EQ(notifications.back(), 2);

	stack.undo();
	ASSERT_EQ(notifications.size(), 2u);
	EXPECT_EQ(notifications.back(), 1);

	stack.redo();
	EXPECT_EQ(notifications.back(), 2);

	// The bottom EmptyCommand is not undoable: jump(0) stops at index 1.
	stack.jump(0);
	EXPECT_EQ(notifications.back(), 1);
}
