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

#include "BLI_listbase.h"
#include "BLI_string.h"
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
  BLI_freelistN(&asset->tags);
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

struct CustomTagEnsureResult BKE_asset_tag_ensure(Asset *asset, const char *name)
{
  struct CustomTagEnsureResult result = {.tag = NULL};
  if (!name[0]) {
    return result;
  }

  CustomTag *tag = BLI_findstring(&asset->tags, name, offsetof(CustomTag, name));

  if (tag) {
    result.tag = tag;
    result.is_new = false;
    return result;
  }

  tag = MEM_mallocN(sizeof(*tag), __func__);
  BLI_strncpy(tag->name, name, sizeof(tag->name));

  BLI_addtail(&asset->tags, tag);

  result.tag = tag;
  result.is_new = true;
  return result;
}

void BKE_asset_tag_remove(Asset *asset, CustomTag *tag)
{
  BLI_freelinkN(&asset->tags, tag);
}

AssetData *BKE_asset_data_create(void)
{
  return MEM_callocN(sizeof(AssetData), __func__);
}

void BKE_asset_data_free(AssetData *asset_data)
{
  MEM_SAFE_FREE(asset_data);
}
