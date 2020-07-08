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
 * \ingroup DNA
 */

#ifndef __DNA_ASSET_TYPES_H__
#define __DNA_ASSET_TYPES_H__

#include "DNA_ID.h"

typedef struct Asset {
  ID id;

  /** Thumbnail image of the data-block. Duplicate of the referenced ID preview. */
  struct PreviewImage *preview;

  ID *referenced_id;
} Asset;

/* TODO unused, keeping in case it's useful later. */
typedef struct AssetData {
  int dummy;
} AssetData;

#endif /* __DNA_ASSET_TYPES_H__ */
