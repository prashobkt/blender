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
 * \ingroup bke
 */

#include <string.h>

#include "BLI_utildefines.h"

#include "BKE_asset.h"
#include "BKE_icons.h"
#include "BKE_idtype.h"
#include "BKE_lib_query.h"

#include "BLT_translation.h"

#include "DNA_ID.h"
#include "DNA_asset_types.h"
#include "DNA_defaults.h"

#include "MEM_guardedalloc.h"

static void asset_init_data(ID *id)
{
  Asset *asset = (Asset *)id;
  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(asset, id));

  MEMCPY_STRUCT_AFTER(asset, DNA_struct_default_get(Asset), id);
}

static void asset_free_data(ID *id)
{
  Asset *asset = (Asset *)id;

  BKE_icon_id_delete((ID *)asset);
  BKE_previewimg_free(&asset->preview);

  MEM_SAFE_FREE(asset->description);
}

static void asset_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Asset *asset = (Asset *)id;

  BKE_LIB_FOREACHID_PROCESS_ID(data, asset->referenced_id, IDWALK_CB_USER);
}

IDTypeInfo IDType_ID_AST = {
    /* id_code */ ID_AST,
    /* id_filter */ FILTER_ID_AST,
    /* main_listbase_index */ INDEX_ID_AST,
    /* struct_size */ sizeof(Asset),
    /* name */ "Asset",
    /* name_plural */ "assets",
    /* translation_context */ BLT_I18NCONTEXT_ID_ASSET,
    /* flags */ 0,

    /* init_data */ asset_init_data,
    /* copy_data */ NULL, /* TODO */
    /* free_data */ asset_free_data,
    /* make_local */ NULL,
    /* foreach_id */ asset_foreach_id,
};

AssetData *BKE_asset_data_create(void)
{
  return MEM_callocN(sizeof(AssetData), __func__);
}

void BKE_asset_data_free(AssetData *asset_data)
{
  MEM_SAFE_FREE(asset_data);
}
