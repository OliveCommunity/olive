/***

  Oak - Non-Linear Video Editor
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

#include "oakengine/worker.h"

// The render worker is a thin shell: all runtime logic (Qt application setup,
// render backend initialization, startup handshake and the NDJSON control
// loop) lives inside liboakengine behind the pure C ABI, so this executable
// imports no engine C++ symbols.
int main(int argc, char *argv[])
{
	return oakengine_worker_main(argc, argv);
}
