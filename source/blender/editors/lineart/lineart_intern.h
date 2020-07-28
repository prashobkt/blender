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
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editors
 */

#ifndef __LRT_INTERN_H__
#define __LRT_INTERN_H__

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_threads.h"

#include "DNA_lineart_types.h"

#include <math.h>
#include <string.h>

struct LineartStaticMemPoolNode;
struct LineartRenderLine;
struct LineartRenderBuffer;

void *list_append_pointer_static(ListBase *h, struct LineartStaticMemPool *smp, void *p);
void *list_append_pointer_static_sized(ListBase *h,
                                       struct LineartStaticMemPool *smp,
                                       void *p,
                                       int size);
void *list_push_pointer_static(ListBase *h, struct LineartStaticMemPool *smp, void *p);
void *list_push_pointer_static_sized(ListBase *h,
                                     struct LineartStaticMemPool *smp,
                                     void *p,
                                     int size);

void *list_append_pointer_static_pool(struct LineartStaticMemPool *mph, ListBase *h, void *p);
void *list_pop_pointer_no_free(ListBase *h);
void list_remove_pointer_item_no_free(ListBase *h, LinkData *lip);

LineartStaticMemPoolNode *mem_new_static_pool(struct LineartStaticMemPool *smp, int size);
void *mem_static_aquire(struct LineartStaticMemPool *smp, int size);
void *mem_static_aquire_thread(struct LineartStaticMemPool *smp, int size);
void *mem_static_destroy(LineartStaticMemPool *smp);

void tmat_make_ortho_matrix_44d(double (*mProjection)[4],
                                double xMin,
                                double xMax,
                                double yMin,
                                double yMax,
                                double zMin,
                                double zMax);
void tmat_make_perspective_matrix_44d(
    double (*mProjection)[4], double fFov_rad, double fAspect, double zMin, double zMax);

int lineart_count_intersection_segment_count(struct LineartRenderBuffer *rb);

#endif
