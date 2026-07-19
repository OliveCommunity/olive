/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_HUMANSTRINGS_H
#define OAK_HUMANSTRINGS_H

#include <olive/core/core.h>
#include <QObject>

namespace olive
{

using namespace core;

class HumanStrings : public QObject {
	Q_OBJECT
public:
	HumanStrings() = default;

	static QString sample_rate_to_string(const int &sample_rate);

	static QString channel_layout_to_string(const uint64_t &layout);

	static QString format_to_string(const SampleFormat &f);
};

}

#endif // OAK_HUMANSTRINGS_H
