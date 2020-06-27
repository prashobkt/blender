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

#include <stdio.h>
#include <stdlib.h>
/* #include <time.h> */
#include "ED_lanpr.h"
#include "MEM_guardedalloc.h"
#include <math.h>

#include "lanpr_intern.h"

/* ===================================================================[slt] */

void *list_append_pointer_static(ListBase *h, LineartStaticMemPool *smp, void *data)
{
  LinkData *lip;
  if (h == NULL) {
    return 0;
  }
  lip = mem_static_aquire(smp, sizeof(LinkData));
  lip->data = data;
  BLI_addtail(h, lip);
  return lip;
}
void *list_append_pointer_static_sized(ListBase *h,
                                       LineartStaticMemPool *smp,
                                       void *data,
                                       int size)
{
  LinkData *lip;
  if (h == NULL) {
    return 0;
  }
  lip = mem_static_aquire(smp, size);
  lip->data = data;
  BLI_addtail(h, lip);
  return lip;
}

void *list_append_pointer_static_pool(LineartStaticMemPool *mph, ListBase *h, void *data)
{
  LinkData *lip;
  if (h == NULL) {
    return 0;
  }
  lip = mem_static_aquire(mph, sizeof(LinkData));
  lip->data = data;
  BLI_addtail(h, lip);
  return lip;
}
void *list_pop_pointer_no_free(ListBase *h)
{
  LinkData *lip;
  void *rev = 0;
  if (h == NULL) {
    return 0;
  }
  lip = BLI_pophead(h);
  rev = lip ? lip->data : 0;
  return rev;
}
void list_remove_pointer_item_no_free(ListBase *h, LinkData *lip)
{
  BLI_remlink(h, (void *)lip);
}

LineartStaticMemPoolNode *mem_new_static_pool(LineartStaticMemPool *smp)
{
  LineartStaticMemPoolNode *smpn = MEM_callocN(LRT_MEMORY_POOL_128MB, "mempool");
  smpn->used_byte = sizeof(LineartStaticMemPoolNode);
  BLI_addhead(&smp->pools, smpn);
  return smpn;
}
void *mem_static_aquire(LineartStaticMemPool *smp, int size)
{
  LineartStaticMemPoolNode *smpn = smp->pools.first;
  void *ret;

  if (!smpn || (smpn->used_byte + size) > LRT_MEMORY_POOL_128MB) {
    smpn = mem_new_static_pool(smp);
  }

  ret = ((unsigned char *)smpn) + smpn->used_byte;

  smpn->used_byte += size;

  return ret;
}
void *mem_static_aquire_thread(LineartStaticMemPool *smp, int size)
{
  LineartStaticMemPoolNode *smpn = smp->pools.first;
  void *ret;

  BLI_spin_lock(&smp->lock_mem);

  if (!smpn || (smpn->used_byte + size) > LRT_MEMORY_POOL_128MB) {
    smpn = mem_new_static_pool(smp);
  }

  ret = ((unsigned char *)smpn) + smpn->used_byte;

  smpn->used_byte += size;

  BLI_spin_unlock(&smp->lock_mem);

  return ret;
}
void *mem_static_destroy(LineartStaticMemPool *smp)
{
  LineartStaticMemPoolNode *smpn;
  void *ret = 0;

  while ((smpn = BLI_pophead(&smp->pools)) != NULL) {
    MEM_freeN(smpn);
  }

  smp->each_size = 0;

  return ret;
}

/* =======================================================================[str] */

void tmat_make_perspective_matrix_44d(
    double (*mProjection)[4], double fFov_rad, double fAspect, double zMin, double zMax)
{
  double yMax;
  double yMin;
  double xMin;
  double xMax;

  if (fAspect < 1) {
    yMax = zMin * tan(fFov_rad * 0.5f);
    yMin = -yMax;
    xMin = yMin * fAspect;
    xMax = -xMin;
  }
  else {
    xMax = zMin * tan(fFov_rad * 0.5f);
    xMin = -xMax;
    yMin = xMin / fAspect;
    yMax = -yMin;
  }

  unit_m4_db(mProjection);

  mProjection[0][0] = (2.0f * zMin) / (xMax - xMin);
  mProjection[1][1] = (2.0f * zMin) / (yMax - yMin);
  mProjection[2][0] = (xMax + xMin) / (xMax - xMin);
  mProjection[2][1] = (yMax + yMin) / (yMax - yMin);
  mProjection[2][2] = -((zMax + zMin) / (zMax - zMin));
  mProjection[2][3] = -1.0f;
  mProjection[3][2] = -((2.0f * (zMax * zMin)) / (zMax - zMin));
  mProjection[3][3] = 0.0f;
}
void tmat_make_ortho_matrix_44d(double (*mProjection)[4],
                                double xMin,
                                double xMax,
                                double yMin,
                                double yMax,
                                double zMin,
                                double zMax)
{
  unit_m4_db(mProjection);

  mProjection[0][0] = 2.0f / (xMax - xMin);
  mProjection[1][1] = 2.0f / (yMax - yMin);
  mProjection[2][2] = -2.0f / (zMax - zMin);
  mProjection[3][0] = -((xMax + xMin) / (xMax - xMin));
  mProjection[3][1] = -((yMax + yMin) / (yMax - yMin));
  mProjection[3][2] = -((zMax + zMin) / (zMax - zMin));
  mProjection[3][3] = 1.0f;
}
