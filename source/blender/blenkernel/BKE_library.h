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
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */
#ifndef __BKE_LIBRARY_H__
#define __BKE_LIBRARY_H__

/** \file
 * \ingroup bke
 *
 * API to manage `Library` data-blocks.
 */

#include "BLI_compiler_attrs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AssetEngineType;
struct AssetUUID;
struct BlendThumbnail;
struct GHash;
struct ID;
struct Library;
struct Main;

void BKE_library_filepath_set(struct Main *bmain, struct Library *lib, const char *filepath);

void BKE_library_asset_repository_init(struct Library *lib,
                                       const struct AssetEngineType *aet,
                                       const char *repo_root);
void BKE_library_asset_repository_clear(struct Library *lib);
void BKE_library_asset_repository_free(struct Library *lib);
struct AssetRef *BKE_library_asset_repository_asset_add(struct Library *lib, const void *idv);
void BKE_library_asset_repository_asset_remove(struct Library *lib, const void *idv);
struct AssetRef *BKE_library_asset_repository_asset_find(struct Library *lib, const void *idv);
void BKE_library_asset_repository_subdata_add(struct AssetRef *aref, const void *idv);
void BKE_library_asset_repository_subdata_remove(struct AssetRef *aref, const void *idv);

void BKE_libraries_asset_subdata_remove(struct Main *bmain, const void *idv);
void BKE_libraries_asset_repositories_clear(struct Main *bmain);
void BKE_libraries_asset_repositories_rebuild(struct Main *bmain);
struct AssetRef *BKE_libraries_asset_repository_uuid_find(struct Main *bmain,
                                                          const struct AssetUUID *uuid);
struct Library *BKE_library_asset_virtual_ensure(struct Main *bmain,
                                                 const struct AssetEngineType *aet);

#define IS_TAGGED(_id) ((_id) && (((ID *)_id)->tag & LIB_TAG_DOIT))

#ifdef __cplusplus
}
#endif

#endif /* __BKE_LIBRARY_H__ */
