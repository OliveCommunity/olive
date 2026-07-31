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

#ifndef OAK_TRACKREFERENCEHANDLE_H
#define OAK_TRACKREFERENCEHANDLE_H

#include <QCoreApplication>
#include <QDataStream>
#include <QHash>
#include <QString>

#include "oakengine/timeline.h"

namespace olive
{

/**
 * @brief App-side mirror of the engine Track::Reference value class
 * (engine/node/output/track/track.h).
 *
 * Pure value type (track type + index) used throughout timeline UI code.
 * Semantics are identical to the engine version. The nested Type enum
 * mirrors engine Track::Type; ordinals MUST stay in sync with the engine
 * enum — the C ABI transports track types as ints
 * (OAKENGINE_TRACK_TYPE_* in oakengine/timeline.h), pinned by the
 * static_asserts below. Update both sides together.
 */
class TrackReference
{
public:
	enum Type { k_none = -1, k_video, k_audio, k_subtitle, k_count };

	TrackReference()
		: type_(k_none)
		, index_(-1)
	{
	}

	TrackReference(const Type &type, const int &index)
		: type_(type)
		, index_(index)
	{
	}

	const Type &type() const
	{
		return type_;
	}

	const int &index() const
	{
		return index_;
	}

	bool operator==(const TrackReference &ref) const
	{
		return type_ == ref.type_ && index_ == ref.index_;
	}

	bool operator!=(const TrackReference &ref) const
	{
		return !(*this == ref);
	}

	bool operator<(const TrackReference &rhs) const
	{
		if (type_ != rhs.type_) {
			return type_ < rhs.type_;
		}

		return index_ < rhs.index_;
	}

	QString to_string() const
	{
		QString type_string = type_to_string(type_);
		if (type_string.isEmpty()) {
			return QString();
		} else {
			return QStringLiteral("%1:%2").arg(type_string,
											   QString::number(index_));
		}
	}

	/// For IDs that shouldn't change between localizations
	static QString type_to_string(Type type)
	{
		switch (type) {
		case k_video:
			return QStringLiteral("v");
		case k_audio:
			return QStringLiteral("a");
		case k_subtitle:
			return QStringLiteral("s");
		case k_count:
		case k_none:
			break;
		}

		return QString();
	}

	/// For human-facing strings (translation context "Track" kept
	/// identical to the engine version)
	static QString type_to_translated_string(Type type)
	{
		switch (type) {
		case k_video:
			return QCoreApplication::translate("Track", "V");
		case k_audio:
			return QCoreApplication::translate("Track", "A");
		case k_subtitle:
			return QCoreApplication::translate("Track", "S");
		case k_count:
		case k_none:
			break;
		}

		return QString();
	}

	static Type type_from_string(const QString &s)
	{
		if (s.size() >= 3) {
			if (s.at(1) == ':') {
				if (s.at(0) == 'v') {
					// Video stream
					return k_video;
				} else if (s.at(0) == 'a') {
					// Audio stream
					return k_audio;
				} else if (s.at(0) == 's') {
					// Subtitle stream
					return k_subtitle;
				}
			}
		}

		return k_none;
	}

	static TrackReference from_string(const QString &s)
	{
		TrackReference ref;
		Type parse_type = type_from_string(s);

		if (parse_type != k_none) {
			bool ok;
			int parse_index = s.mid(2).toInt(&ok);

			if (ok) {
				ref.type_ = parse_type;
				ref.index_ = parse_index;
			}
		}

		return ref;
	}

	bool is_valid() const
	{
		return type_ > k_none && type_ < k_count && index_ >= 0;
	}

private:
	Type type_;

	int index_;
};

// Ordinal sync guards: C ABI OAKENGINE_TRACK_TYPE_* (oakengine/timeline.h)
// carry the same values as engine Track::Type, and this mirror matches both.
static_assert(TrackReference::k_video == OAKENGINE_TRACK_TYPE_VIDEO,
			  "TrackReference::Type out of sync with C ABI track types");
static_assert(TrackReference::k_audio == OAKENGINE_TRACK_TYPE_AUDIO,
			  "TrackReference::Type out of sync with C ABI track types");
static_assert(TrackReference::k_subtitle == OAKENGINE_TRACK_TYPE_SUBTITLE,
			  "TrackReference::Type out of sync with C ABI track types");

inline uint qHash(const TrackReference &r, uint seed = 0)
{
	return ::qHash(QStringLiteral("%1:%2").arg(QString::number(r.type()),
											   QString::number(r.index())),
				   seed);
}

inline QDataStream &operator<<(QDataStream &out, const TrackReference &ref)
{
	out << static_cast<int>(ref.type()) << ref.index();
	return out;
}

inline QDataStream &operator>>(QDataStream &in, TrackReference &ref)
{
	int type, index;
	in >> type >> index;
	ref = TrackReference(static_cast<TrackReference::Type>(type), index);
	return in;
}

} // namespace olive

#endif // OAK_TRACKREFERENCEHANDLE_H
