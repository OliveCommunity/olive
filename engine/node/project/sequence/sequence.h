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

#ifndef OAK_SEQUENCE_H
#define OAK_SEQUENCE_H

#include "node/output/track/tracklist.h"
#include "node/output/viewer/viewer.h"

namespace olive
{

/**
 * @brief The main timeline object, an graph of edited clips that forms a complete edit
 */
class Sequence : public ViewerOutput {
	Q_OBJECT
public:
	Sequence();

	NODE_DEFAULT_FUNCTIONS(Sequence)

	virtual QString name() const override
	{
		return tr("Sequence");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.Olive.sequence");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_project };
	}

	virtual QString description() const override
	{
		return tr(
			"A series of cuts that result in an edited video. Also called a timeline.");
	}

	void add_default_nodes(MultiUndoCommand *command = nullptr);

	virtual QVariant data(const DataType &d) const override;

	const QVector<Track *> &get_tracks() const
	{
		return track_cache_;
	}

	Track *get_track_from_reference(const Track::Reference &track_ref) const
	{
		if (track_ref.type() < 0 || track_ref.type() >= track_lists_.size()) {
			return nullptr;
		}
		return track_lists_.at(track_ref.type())->get_track_at(track_ref.index());
	}

	/**
   * @brief Same as GetTracks() but omits tracks that are locked.
   */
	QVector<Track *> get_unlocked_tracks() const;

	TrackList *track_list(Track::Type type) const
	{
		return track_lists_.at(type);
	}

	virtual void retranslate() override;

	virtual void invalidate_cache(const TimeRange &range, const QString &from,
								 int element,
								 InvalidateCacheOptions options) override;

	static const QString k_track_input_format;

protected:
	virtual void InputConnectedEvent(const QString &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const QString &input, int element,
										Node *output) override;

	virtual Rational verify_length_internal(Track::Type type) const override;

signals:
	void track_added(Track *track);
	void track_removed(Track *track);

	void subtitles_changed(const TimeRange &range);

private:
	QVector<TrackList *> track_lists_;

	QVector<Track *> track_cache_;

private slots:
	void update_track_cache();
};

}

#endif // OAK_SEQUENCE_H
