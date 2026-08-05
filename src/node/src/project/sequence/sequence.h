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

#include "output/track/tracklist.h"
#include "output/viewer/viewer.h"

namespace olive
{

/**
 * @brief The main timeline object, an graph of edited clips that forms a complete edit
 */
class Sequence : public ViewerOutput {
public:
	Sequence();

	// Deletes the owned TrackLists (raw pointers; they were QObject children
	// in the Qt build). Custom destructor, so NODE_DEFAULT_FUNCTIONS is
	// expanded manually.
	virtual ~Sequence() override;

	virtual Node *copy() const override
	{
		return new Sequence();
	}

	virtual std::string name() const override
	{
		return "Sequence";
	}

	virtual std::string id() const override
	{
		return "org.olivevideoeditor.Olive.sequence";
	}

	virtual std::vector<CategoryID> category() const override
	{
		return { k_category_project };
	}

	virtual std::string description() const override
	{
		return "A series of cuts that result in an edited video. Also called "
			   "a timeline.";
	}

	void add_default_nodes(MultiUndoCommand *command = nullptr);

	virtual Variant data(const DataType &d) const override;

	const std::vector<Track *> &get_tracks() const
	{
		return track_cache_;
	}

	Track *get_track_from_reference(const Track::Reference &track_ref) const
	{
		if (track_ref.type() < 0 ||
			track_ref.type() >= int(track_lists_.size())) {
			return nullptr;
		}
		return track_lists_.at(track_ref.type())->get_track_at(track_ref.index());
	}

	/**
   * @brief Same as GetTracks() but omits tracks that are locked.
   */
	std::vector<Track *> get_unlocked_tracks() const;

	TrackList *track_list(Track::Type type) const
	{
		return track_lists_.at(type);
	}

	virtual void retranslate() override;

	virtual void invalidate_cache(const TimeRange &range,
								 const std::string &from, int element,
								 InvalidateCacheOptions options) override;

	/**
	 * @brief Rebuild the flat track cache from the track lists
	 *
	 * Formerly a private slot connected to TrackList::track_list_changed;
	 * the caller (facade / TrackList de-Qt wave) must now call this
	 * explicitly whenever a track list changes.
	 */
	void update_track_cache();

	static const std::string k_track_input_format;

protected:
	virtual void InputConnectedEvent(const std::string &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const std::string &input, int element,
										Node *output) override;

	virtual Rational verify_length_internal(Track::Type type) const override;

private:
	std::vector<TrackList *> track_lists_;

	std::vector<Track *> track_cache_;
};

}

#endif // OAK_SEQUENCE_H
