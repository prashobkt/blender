/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup bgpencil
 */

#include <stdio.h>

#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_gpencil_types.h"
#include "DNA_space_types.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_main.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "../gpencil_io_exporter.h"
#include "gpencil_io_svg.h"

using blender::io::gpencil::GpencilExporterSVG;

static bool is_keyframe_empty(bGPdata *gpd, int framenum)
{
  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    if (gpl->flag & GP_LAYER_HIDE) {
      continue;
    }
    LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
      if (gpf->framenum == framenum) {
        return false;
      }
    }
  }
  return true;
}

static bool gpencil_io_export_frame(const GpencilExportParams *params)
{

  bool result = false;
  switch (params->mode) {
    case GP_EXPORT_TO_SVG: {
      GpencilExporterSVG writter = GpencilExporterSVG(params);
      result = writter.write(std::string(params->frame));
      break;
    }
    default:
      break;
  }

  return result;
}

/* Main export entry point function. */
bool gpencil_io_export(GpencilExportParams *params)
{
  Main *bmain = CTX_data_main(params->C);
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(params->C);
  Scene *scene = CTX_data_scene(params->C);

  Object *ob = CTX_data_active_object(params->C);
  Object *ob_eval_ = (Object *)DEG_get_evaluated_id(depsgraph, &ob->id);
  bGPdata *gpd_eval = (bGPdata *)ob_eval_->data;
  const bool only_active_frame = ((params->flag & GP_EXPORT_ACTIVE_FRAME) != 0);

  int oldframe = (int)DEG_get_ctime(depsgraph);
  bool done = false;

  if (only_active_frame) {
    done |= gpencil_io_export_frame(params);
  }
  else {
    for (int i = params->frame_start; i < params->frame_end + 1; i++) {
      if (is_keyframe_empty(gpd_eval, i)) {
        continue;
      }

      CFRA = i;
      BKE_scene_graph_update_for_newframe(depsgraph, bmain);
      sprintf(params->frame, "%04d", i);

      done |= gpencil_io_export_frame(params);
    }
  }

  /* Return frame state and DB to original state */
  if (!only_active_frame) {
    CFRA = oldframe;
    BKE_scene_graph_update_for_newframe(depsgraph, bmain);
  }

  return done;
}
