/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 * The Original Code is Copyright (C) 2010 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __DNA_LRT_TYPES_H__
#define __DNA_LRT_TYPES_H__

/** \file DNA_lineart_types.h
 *  \ingroup DNA
 */

#include "DNA_ID.h"
#include "DNA_collection_types.h"
#include "DNA_listBase.h"

struct Object;
struct Material;
struct Collection;

typedef enum eLineartEdgeFlag {
  LRT_EDGE_FLAG_EDGE_MARK = (1 << 0),
  LRT_EDGE_FLAG_CONTOUR = (1 << 1),
  LRT_EDGE_FLAG_CREASE = (1 << 2),
  LRT_EDGE_FLAG_MATERIAL = (1 << 3),
  LRT_EDGE_FLAG_INTERSECTION = (1 << 4),
  /**  floating edge, unimplemented yet */
  LRT_EDGE_FLAG_FLOATING = (1 << 5),
  LRT_EDGE_FLAG_CHAIN_PICKED = (1 << 6),
} eLineartEdgeFlag;

#define LRT_EDGE_FLAG_ALL_TYPE 0x3f

typedef enum eLineartModeFlags {
  LRT_LINE_LAYER_USE_SAME_STYLE = (1 << 0),      /* Share with object lineart flags */
  LRT_LINE_LAYER_USE_MULTIPLE_LEVELS = (1 << 1), /* Share with object lineart flags */
  LRT_LINE_LAYER_NORMAL_ENABLED = (1 << 2),
  LRT_LINE_LAYER_NORMAL_INVERSE = (1 << 3),
  LRT_LINE_LAYER_REPLACE_STROKES = (1 << 4),
  LRT_LINE_LAYER_COLLECTION_FORCE = (1 << 5),
} eLineartModeFlags;

#endif
