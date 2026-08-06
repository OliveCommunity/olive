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

// Same-dir quoted include would hit the src/node/transition/render/
// bridge header (olive::CancelAtom) first on this build's include path;
// reference the public header relative to this file instead.
#include "../../../include/render/cancelatom.h"

#include <gtest/gtest.h>

#include "render/cache.h" /* oakrender_debug_alive_count */

TEST(OakCancelAtomTest, InitFree)
{
	const int alive_before = oakrender_debug_alive_count();

	OakCancelAtom atom = oakrender_cancelatom_init();
	ASSERT_NE(atom.ctx, nullptr);
	EXPECT_NE(atom.addref, nullptr);
	EXPECT_NE(atom.release, nullptr);
	EXPECT_EQ(atom.abi_version, OAKRENDER_ABI_VERSION);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before + 1);

	oakrender_cancelatom_free(&atom);
	EXPECT_EQ(atom.ctx, nullptr);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);

	// NULL and empty handles are no-ops
	oakrender_cancelatom_free(nullptr);
	oakrender_cancelatom_free(&atom);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);
}

TEST(OakCancelAtomTest, CancelStateMachine)
{
	OakCancelAtom atom = oakrender_cancelatom_init();
	ASSERT_NE(atom.ctx, nullptr);

	int flag = -1;
	EXPECT_EQ(oakrender_cancelatom_is_cancelled(atom, &flag), OAKRENDER_OK);
	EXPECT_EQ(flag, 0);

	EXPECT_EQ(oakrender_cancelatom_cancel(atom), OAKRENDER_OK);

	// Cancel must not be heard until a consumer reads the flag
	int heard = -1;
	EXPECT_EQ(oakrender_cancelatom_heard_cancel(atom, &heard), OAKRENDER_OK);
	EXPECT_EQ(heard, 0);

	EXPECT_EQ(oakrender_cancelatom_is_cancelled(atom, &flag), OAKRENDER_OK);
	EXPECT_EQ(flag, 1);
	EXPECT_EQ(oakrender_cancelatom_heard_cancel(atom, &heard), OAKRENDER_OK);
	EXPECT_EQ(heard, 1);

	oakrender_cancelatom_free(&atom);
}

TEST(OakCancelAtomTest, InvalidArgs)
{
	OakCancelAtom empty = {};

	int flag = 7;
	EXPECT_EQ(oakrender_cancelatom_cancel(empty), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cancelatom_is_cancelled(empty, &flag),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(flag, 7);
	EXPECT_EQ(oakrender_cancelatom_heard_cancel(empty, &flag),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(flag, 7);

	OakCancelAtom atom = oakrender_cancelatom_init();
	ASSERT_NE(atom.ctx, nullptr);
	EXPECT_EQ(oakrender_cancelatom_is_cancelled(atom, nullptr),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_cancelatom_heard_cancel(atom, nullptr),
			  OAKRENDER_E_INVALID);

	oakrender_cancelatom_free(&atom);
}

TEST(OakCancelAtomTest, AddrefReleaseCountSemantics)
{
	const int alive_before = oakrender_debug_alive_count();

	OakCancelAtom atom = oakrender_cancelatom_init();
	ASSERT_NE(atom.ctx, nullptr);

	// Copy the struct and take an extra reference; both copies share the
	// same underlying object and cancel state
	OakCancelAtom copy = atom;
	copy.addref(copy.ctx);

	EXPECT_EQ(oakrender_cancelatom_cancel(atom), OAKRENDER_OK);
	int flag = 0;
	EXPECT_EQ(oakrender_cancelatom_is_cancelled(copy, &flag), OAKRENDER_OK);
	EXPECT_EQ(flag, 1);

	// Releasing one reference keeps the object alive for the other
	atom.release(atom.ctx);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before + 1);
	flag = 0;
	EXPECT_EQ(oakrender_cancelatom_is_cancelled(copy, &flag), OAKRENDER_OK);
	EXPECT_EQ(flag, 1);

	// The final reference destroys the object
	oakrender_cancelatom_free(&copy);
	EXPECT_EQ(copy.ctx, nullptr);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);
}

TEST(OakCancelAtomTest, AddrefReleaseNullCtxIsSafe)
{
	// NULL ctx must not crash the thunks
	OakCancelAtom atom = oakrender_cancelatom_init();
	ASSERT_NE(atom.ctx, nullptr);
	atom.addref(nullptr);
	atom.release(nullptr);
	oakrender_cancelatom_free(&atom);
}
