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

#ifndef OAK_EDITOR_TASK_PROJECT_H
#define OAK_EDITOR_TASK_PROJECT_H

#include "codec/encoder.h"
#include "node/colormanager.h"
#include "node/footage.h"
#include "node/node.h"
#include "node/project.h"
#include "node/sequence.h"
#include "task/task.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Project task factories and result accessors (M8 §2.2).
 */

/** @brief olive::ProjectLoadTask. */
OakTaskTask *oaktask_create_project_load(const char *filename);

/** @brief Take the loaded project (ownership transfer). */
OakNodeProject *oaktask_load_take_project(OakTaskTask *t);

/** @brief olive::ProjectSaveTask. `filename_or_NULL` overrides the
 *        project's own filename. */
OakTaskTask *oaktask_create_project_save(OakNodeProject *project,
										 const char *filename_or_NULL,
										 int use_compression);

/** @brief olive::ProjectImportTask. */
OakTaskTask *oaktask_create_project_import(OakNodeFolder *folder,
										   OakNodeProject *project,
										   const char *const *urls,
										   int url_count);

/** @brief Take the import's undo command (ownership transfer). */
OakUndoCommand *oaktask_import_take_command(OakTaskTask *t);

int oaktask_import_footage_count(OakTaskTask *t);

/** @brief Borrowed footage handle at index, NULL when out of range. */
OakNodeFootage *oaktask_import_footage_at(OakTaskTask *t, int index);

int oaktask_import_invalid_count(OakTaskTask *t);

/** @brief Invalid filename at index (two-stage). */
int oaktask_import_invalid_at(OakTaskTask *t, int index, char *buf,
							  int buf_size);

/** @brief olive::PreCacheTask. */
OakTaskTask *oaktask_create_precache(OakNodeFootage *footage, int index,
									 OakNodeSequence *sequence);

/** @brief olive::ExportTask (params POD from codec/encoder.h). */
OakTaskTask *oaktask_create_export(OakNodeNode *viewer,
								   OakNodeColorManager *color_manager,
								   const oakcodec_encoding_params *params);

/**
 * @brief Image-sequence confirmation callback (facade/UI concern;
 *        olive::ProjectImportTask::set_image_sequence_confirm_callback).
 *        Return non-zero to treat numbered stills as a sequence.
 *        Default (no callback): not a sequence.
 */
typedef int (*oaktask_image_sequence_confirm_fn)(const char *filename,
												 void *userdata);
void oaktask_import_set_image_sequence_confirm_cb(
	oaktask_image_sequence_confirm_fn fn, void *userdata);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_TASK_PROJECT_H
