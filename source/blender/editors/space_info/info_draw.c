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
 * The Original Code is Copyright (C) 2010 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup spinfo
 */

#include <BLI_blenlib.h>
#include <DNA_text_types.h>
#include <MEM_guardedalloc.h>
#include <CLG_log.h>
#include <limits.h>
#include <string.h>

#include "BLI_utildefines.h"

#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "BKE_report.h"

#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "../space_text/text_format.h"
#include "GPU_framebuffer.h"
#include "info_intern.h"
#include "textview.h"


static void info_textview_draw_rect_calc(const ARegion *region,
                                         rcti *r_draw_rect,
                                         rcti *r_draw_rect_outer)
{
  const int margin = 0.45f * U.widget_unit;
  r_draw_rect->xmin = margin + UI_UNIT_X;
  r_draw_rect->xmax = region->winx - V2D_SCROLL_WIDTH;
  r_draw_rect->ymin = margin;
  r_draw_rect->ymax = region->winy;
  /* No margin at the top (allow text to scroll off the window). */

  r_draw_rect_outer->xmin = 0;
  r_draw_rect_outer->xmax = region->winx;
  r_draw_rect_outer->ymin = 0;
  r_draw_rect_outer->ymax = region->winy;
}

static int info_textview_main__internal(const SpaceInfo *sinfo,
                                        const ARegion *region,
                                        const ReportList *reports,
                                        const bool do_draw,
                                        const int mval[2],
                                        void **r_mval_pick_item,
                                        int *r_mval_pick_offset)
{
  int ret = 0;

  const View2D *v2d = &region->v2d;

  TextViewContext tvc = {0};
  tvc.const_colors = NULL;

  tvc.arg1 = sinfo;

  switch (sinfo->view) {
    case INFO_VIEW_CLOG:
      tvc.begin = clog_textview_begin;
      tvc.lines_get = clog_textview_line_get;
      tvc.line_draw_data = clog_line_draw_data;
      tvc.end = clog_textview_end;
      tvc.step = clog_textview_step;
      tvc.arg2 = CLG_log_records_get();
      break;
    case INFO_VIEW_REPORTS:
      tvc.begin = report_textview_begin;
      tvc.lines_get = report_textview_line_get;
      tvc.line_draw_data = report_line_draw_data;
      tvc.end = report_textview_end;
      tvc.step = report_textview_step;
      tvc.arg2 = reports;
      break;
    default:
      BLI_assert(0);
      break;
  }

  /* view */
  tvc.sel_start = 0;
  tvc.sel_end = 0;
  tvc.lheight = 17 * UI_DPI_FAC;
  tvc.row_vpadding = 0.4 * tvc.lheight;
  tvc.scroll_ymin = v2d->cur.ymin;
  tvc.scroll_ymax = v2d->cur.ymax;

  info_textview_draw_rect_calc(region, &tvc.draw_rect, &tvc.draw_rect_outer);

  ret = textview_draw(&tvc, do_draw, mval, r_mval_pick_item, r_mval_pick_offset);

  return ret;
}

void *info_text_pick(const SpaceInfo *sinfo,
                     const ARegion *region,
                     const ReportList *reports,
                     int mval_y)
{
  void *mval_pick_item = NULL;
  const int mval[2] = {0, mval_y};

  info_textview_main__internal(sinfo, region, reports, false, mval, &mval_pick_item, NULL);
  return (void *)mval_pick_item;
}

int info_textview_height(const SpaceInfo *sinfo, const ARegion *region, const ReportList *reports)
{
  int mval[2] = {INT_MAX, INT_MAX};
  return info_textview_main__internal(sinfo, region, reports, false, mval, NULL, NULL);
}

void info_textview_main(const SpaceInfo *sinfo, const ARegion *region, const ReportList *reports)
{
  int mval[2] = {INT_MAX, INT_MAX};
  info_textview_main__internal(sinfo, region, reports, true, mval, NULL, NULL);
}
