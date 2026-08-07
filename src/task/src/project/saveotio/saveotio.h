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

#ifndef OAK_SAVEOTIOTASK_H
#define OAK_SAVEOTIOTASK_H

#include <string>

#include <opentimelineio/timeline.h>
#include <opentimelineio/version.h>

#include <olive/core/util/rational.h>

#include "node/project.h"
#include "node/sequence.h"
#include "node/track.h"
#include "task.h"

namespace OTIO = opentimelineio::OPENTIMELINEIO_VERSION;

namespace olive
{

using core::Rational;

class SaveOTIOTask : public Task {
public:
	SaveOTIOTask(OakNodeProject *project, const std::string &filename);

protected:
	virtual bool run() override;

private:
	OTIO::Timeline *serialize_timeline(OakNodeSequence *sequence);

	OTIO::Track *serialize_track(OakNodeTrack *track, double sequence_rate,
								 Rational max_track_length);

	bool serialize_track_list(OakNodeTrackList *list,
							  OTIO::Timeline *otio_timeline,
							  double sequence_rate);

	OakNodeProject *project_;

	std::string filename_;
};

}

#endif // OAK_SAVEOTIOTASK_H
