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

// App-side implementations of qHash overloads and stream operators
// used by QHash containers and QDataStream serialization in app code.
// Provides local definitions so the app doesn't import these from liboakengine.

#include "node/param.h"
#include "node/output/track/track.h"

namespace olive
{

uint qHash(const NodeInput &i)
{
	return qHash(i.node()) ^ qHash(i.input()) ^ ::qHash(i.element());
}

uint qHash(const NodeInputPair &i)
{
	return qHash(i.node) ^ qHash(i.input);
}

uint qHash(const NodeKeyframeTrackReference &i)
{
	return qHash(i.input()) ^ ::qHash(i.track());
}

uint qHash(const Track::Reference &r, uint seed)
{
	return ::qHash(QStringLiteral("%1:%2").arg(QString::number(r.type()),
											   QString::number(r.index())),
				   seed);
}

QDataStream &operator<<(QDataStream &out, const Track::Reference &ref)
{
	out << static_cast<int>(ref.type()) << ref.index();
	return out;
}

QDataStream &operator>>(QDataStream &in, Track::Reference &ref)
{
	int type, index;
	in >> type >> index;
	ref = Track::Reference(static_cast<Track::Type>(type), index);
	return in;
}

}
