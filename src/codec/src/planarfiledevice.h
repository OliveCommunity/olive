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

#ifndef OAK_PLANARFILEDEVICE_H
#define OAK_PLANARFILEDEVICE_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace olive
{

/**
 * @brief Reads/writes interleaved planar channel files
 *
 * De-Qt replacement for the QFile-based version; now a thin wrapper over
 * std::FILE. Not copyable; closes all files on destruction.
 */
class PlanarFileDevice {
public:
	/**
	 * @brief Open mode (replaces QIODevice::OpenMode)
	 */
	enum OpenMode { k_read_only, k_write_only };

	PlanarFileDevice();

	~PlanarFileDevice();

	PlanarFileDevice(const PlanarFileDevice &) = delete;
	PlanarFileDevice &operator=(const PlanarFileDevice &) = delete;

	bool isOpen() const
	{
		return !files_.empty();
	}

	bool open(const std::vector<std::string> &filenames, OpenMode mode);

	int64_t read(char **data, int64_t bytes_per_channel, int64_t offset = 0);

	int64_t write(const char **data, int64_t bytes_per_channel,
				  int64_t offset = 0);

	int64_t size() const;

	bool seek(int64_t pos);

	void close();

private:
	std::vector<std::FILE *> files_;
};

}

#endif // OAK_PLANARFILEDEVICE_H
