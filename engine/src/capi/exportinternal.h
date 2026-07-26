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

#ifndef OAKENGINE_EXPORTINTERNAL_H
#define OAKENGINE_EXPORTINTERNAL_H

// Internal (not installed) shared declaration between the export and
// encoding capi translation units: oakengine_export_render_with_params()
// (declared in oakengine/encoding.h) drives the same synchronous ExportTask
// machinery as oakengine_export_render()/_ex(), which lives in export.cpp.

namespace olive
{
class Sequence;
class Project;
class EncodingParams;
namespace core
{
class AudioParams;
}
using core::AudioParams;
}

// Runs the synchronous export (render_internal in export.cpp): prewarms
// audio conforms when prewarm_audio is set, drives the ExportTask on a
// worker thread while the calling thread pumps events. Returns
// OAKENGINE_OK / OAKENGINE_E_STATE / OAKENGINE_E_FAILED /
// OAKENGINE_E_CANCELLED; failure reason via oakengine_export_last_error().
int oakengine_export_render_internal(olive::Sequence *sequence,
									 olive::Project *project,
									 olive::EncodingParams &params,
									 bool prewarm_audio,
									 const olive::AudioParams &prewarm_params);

// Sets the export family's thread-local failure reason (read back with
// oakengine_export_last_error()). Used by capi TUs outside export.cpp whose
// contracts route errors through the export channel.
class QString;
void oakengine_export_set_error_string(const QString &error);

#endif // OAKENGINE_EXPORTINTERNAL_H
