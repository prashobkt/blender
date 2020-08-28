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
 * \ingroup edasset
 */

#include "BKE_asset.h"
#include "BKE_context.h"
#include "BKE_icons.h"
#include "BKE_lib_id.h"
#include "BKE_report.h"

#include "DNA_asset_types.h"

#include "ED_asset.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "UI_interface_icons.h"

#include "WM_api.h"
#include "WM_types.h"

static int asset_make_exec(bContext *C, wmOperator *op)
{
  PointerRNA idptr = RNA_pointer_get(op->ptr, "id");
  ID *id = idptr.data;

  if (!id || !RNA_struct_is_ID(idptr.type)) {
    return OPERATOR_CANCELLED;
  }

  if (id->asset_data) {
    BKE_reportf(op->reports, RPT_ERROR, "Data-block '%s' already is an asset", id->name + 2);
    return OPERATOR_CANCELLED;
  }

  /* TODO this should probably be somewhere in BKE and/or ED. */

  id_fake_user_set(id);

  id->asset_data = BKE_asset_data_create();

  UI_id_icon_render(C, NULL, id, true, false);
  /* Store reference to the ID's preview. */
  id->asset_data->preview = BKE_previewimg_id_get(id);

  BKE_reportf(op->reports, RPT_INFO, "Data-block '%s' is now an asset", id->name + 2);

  WM_event_add_notifier(C, NC_ID | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

static void ASSET_OT_make(wmOperatorType *ot)
{
  ot->name = "Make Asset";
  ot->description = "Enable asset management for a data-block";
  ot->idname = "ASSET_OT_make";

  ot->exec = asset_make_exec;

  RNA_def_pointer_runtime(
      ot->srna, "id", &RNA_ID, "Data-block", "Data-block to enable asset management for");
}

/* -------------------------------------------------------------------- */

void ED_operatortypes_asset(void)
{
  WM_operatortype_append(ASSET_OT_make);
}
