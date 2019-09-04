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


#include <stdlib.h>
#include <stdio.h>
/* #include <time.h> */
#include "ED_lanpr.h"
#include "MEM_guardedalloc.h"
#include <math.h>

/* ===================================================================[slt] */

void *list_append_pointer_static(ListBase *h, LANPR_StaticMemPool *smp, void *data)
{
  LinkData *lip;
  if (!h) {
    return 0;
  }
  lip = mem_static_aquire(smp, sizeof(LinkData));
  lip->data = data;
  BLI_addtail(h, lip);
  return lip;
}
void *list_append_pointer_static_sized(ListBase *h, LANPR_StaticMemPool *smp, void *data, int size)
{
  LinkData *lip;
  if (!h) {
    return 0;
  }
  lip = mem_static_aquire(smp, size);
  lip->data = data;
  BLI_addtail(h, lip);
  return lip;
}

void *list_append_pointer_static_pool(LANPR_StaticMemPool *mph, ListBase *h, void *data)
{
  LinkData *lip;
  if (!h) {
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
  if (!h) {
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

LANPR_StaticMemPoolNode *mem_new_static_pool(LANPR_StaticMemPool *smp)
{
  LANPR_StaticMemPoolNode *smpn = MEM_callocN(LANPR_MEMORY_POOL_128MB, "mempool");
  smpn->used_byte = sizeof(LANPR_StaticMemPoolNode);
  BLI_addhead(&smp->pools, smpn);
  return smpn;
}
void *mem_static_aquire(LANPR_StaticMemPool *smp, int size)
{
  LANPR_StaticMemPoolNode *smpn = smp->pools.first;
  void *ret;

  if (!smpn || (smpn->used_byte + size) > LANPR_MEMORY_POOL_128MB) {
    smpn = mem_new_static_pool(smp);
  }

  ret = ((unsigned char *)smpn) + smpn->used_byte;

  smpn->used_byte += size;

  return ret;
}
void *mem_static_aquire_thread(LANPR_StaticMemPool *smp, int size)
{
  LANPR_StaticMemPoolNode *smpn = smp->pools.first;
  void *ret;

  BLI_spin_lock(&smp->lock_mem);

  if (!smpn || (smpn->used_byte + size) > LANPR_MEMORY_POOL_128MB) {
    smpn = mem_new_static_pool(smp);
  }

  ret = ((unsigned char *)smpn) + smpn->used_byte;

  smpn->used_byte += size;

  BLI_spin_unlock(&smp->lock_mem);

  return ret;
}
void *mem_static_destroy(LANPR_StaticMemPool *smp)
{
  LANPR_StaticMemPoolNode *smpn;
  void *ret = 0;

  while ((smpn = BLI_pophead(&smp->pools)) != NULL) {
    MEM_freeN(smpn);
  }

  smp->each_size = 0;

  return ret;
}

/* =======================================================================[str] */

real tmat_length_3d(tnsVector3d l)
{
  return (sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]));
}
real tmat_vector_cross_3d(tnsVector3d result, tnsVector3d l, tnsVector3d r)
{
  result[0] = l[1] * r[2] - l[2] * r[1];
  result[1] = l[2] * r[0] - l[0] * r[2];
  result[2] = l[0] * r[1] - l[1] * r[0];
  return tmat_length_3d(result);
}
void tmat_make_perspective_matrix_44d(
    double (*mProjection)[4], real fFov_rad, real fAspect, real zMin, real zMax)
{
  real yMax;
  real yMin;
  real xMin;
  real xMax;

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
void tmat_make_ortho_matrix_44d(
    double (*mProjection)[4], real xMin, real xMax, real yMin, real yMax, real zMin, real zMax)
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
