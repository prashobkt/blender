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

#ifndef __BKE_ASSET_H__
#define __BKE_ASSET_H__

/** \file
 * \ingroup bke
 */

#include "BLI_utildefines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AssetData *BKE_asset_data_create(void);
void BKE_asset_data_free(struct AssetData *asset_data);

struct CustomTagEnsureResult {
  struct CustomTag *tag;
  /* Set to false if a tag of this name was already present. */
  bool is_new;
};

struct CustomTagEnsureResult BKE_assetdata_tag_ensure(struct AssetData *asset_data,
                                                      const char *name);
void BKE_assetdata_tag_remove(struct AssetData *asset_data, struct CustomTag *tag);

#ifdef __cplusplus
}
#endif

#endif /* __BKE_ASSET_H__ */
