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

/** \file DNA_lanpr_types.h
 *  \ingroup DNA
 */

#include "DNA_ID.h"
#include "DNA_collection_types.h"
#include "DNA_listBase.h"

struct Object;
struct Material;
struct Collection;

typedef enum eLineartTaperSettings {
  LRT_USE_DIFFERENT_TAPER = 0,
  LRT_USE_SAME_TAPER = 1,
} eLineartTaperSettings;

typedef enum eLineartNomalEffect {
  /* Shouldn't have access to zero value. */
  /* Enable/disable is another flag. */
  LRT_NORMAL_DIRECTIONAL = 1,
  LRT_NORMAL_POINT = 2,
} eLineartNomalEffect;

typedef enum eLineartComponentMode {
  LRT_COMPONENT_MODE_ALL = 0,
  LRT_COMPONENT_MODE_OBJECT = 1,
  LRT_COMPONENT_MODE_MATERIAL = 2,
  LRT_COMPONENT_MODE_COLLECTION = 3,
} eLineartComponentMode;

typedef enum eLineartComponentUsage {
  LRT_COMPONENT_INCLUSIVE = 0,
  LRT_COMPONENT_EXCLUSIVE = 1,
} eLineartComponentUsage;

typedef enum eLineartComponentLogic {
  LRT_COMPONENT_LOGIG_OR = 0,
  LRT_COMPONENT_LOGIC_AND = 1,
} eLineartComponentLogic;

struct DRWShadingGroup;

typedef struct LineartLineType {
  int use;
  float thickness;
  float color[4];
} LineartLineType;

typedef enum eLineartLineLayerFlags {
  LRT_LINE_LAYER_USE_SAME_STYLE = (1 << 0),      /* Share with object lineart flags */
  LRT_LINE_LAYER_USE_MULTIPLE_LEVELS = (1 << 1), /* Share with object lineart flags */
  LRT_LINE_LAYER_NORMAL_ENABLED = (1 << 2),
  LRT_LINE_LAYER_NORMAL_INVERSE = (1 << 3),
  LRT_LINE_LAYER_REPLACE_STROKES = (1 << 4),
  LRT_LINE_LAYER_COLLECTION_FORCE = (1 << 5),
} eLineartLineLayerFlags;

typedef struct LineartLineLayer {
  struct LineartLineLayer *next, *prev;

  int flags;
  int _pad1;
  int level_start;
  int level_end;

  /** To be displayed on the list */
  char name[64];

  LineartLineType contour;
  LineartLineType crease;
  LineartLineType edge_mark;
  LineartLineType material_separate;
  LineartLineType intersection;

  float thickness;

  float color[4];

  int normal_mode;
  float normal_ramp_begin;
  float normal_ramp_end;
  float normal_thickness_start;
  float normal_thickness_end;
  struct Object *normal_control_object;

  /** For component evaluation */
  int logic_mode;
  int _pad3;

  struct DRWShadingGroup *shgrp;
  struct GPUBatch *batch;

} LineartLineLayer;

#endif
