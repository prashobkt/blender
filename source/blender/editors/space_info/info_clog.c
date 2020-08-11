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
 * \ingroup spinfo
 */

#include <BKE_report.h>
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_dynstr.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_screen.h"
#include "ED_select_utils.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "../../../../intern/clog/CLG_log.h"
#include "info_intern.h"

bool is_log_record_visible(const CLG_LogRecord *record, const SpaceInfo *sinfo)
{
  return true;
}

static void log_records_select_all(CLG_LogRecordList *records, const SpaceInfo *sinfo, int action)
{
  if (action == SEL_TOGGLE) {
    action = SEL_SELECT;
    for (CLG_LogRecord *record = records->last; record; record = record->prev) {
      if (is_log_record_visible(record, sinfo) && (record->flag & CLG_SELECT)) {
        action = SEL_DESELECT;
        break;
      }
    }
  }

  for (CLG_LogRecord *record = records->last; record; record = record->prev) {
    if (is_log_record_visible(record, sinfo)) {
      switch (action) {
        case SEL_SELECT:
          record->flag |= CLG_SELECT;
          break;
        case SEL_DESELECT:
          record->flag &= ~CLG_SELECT;
          break;
        case SEL_INVERT:
          record->flag ^= CLG_SELECT;
          break;
        default:
          BLI_assert(0);
      }
    }
  }
}

static int select_clog_pick_exec(bContext *C, wmOperator *op)
{
  const int clog_index = RNA_int_get(op->ptr, "clog_index");
  const bool extend = RNA_boolean_get(op->ptr, "extend");
  const bool use_range = RNA_boolean_get(op->ptr, "extend_range");
  const bool deselect_all = RNA_boolean_get(op->ptr, "deselect_all");

  SpaceInfo *sinfo = CTX_wm_space_info(C);

  CLG_LogRecordList *records = CLG_log_records_get();
  CLG_LogRecord *record = BLI_findlink((const struct ListBase *)records, clog_index);

  if (clog_index == INDEX_INVALID) {  // click in empty area
    log_records_select_all(records, sinfo, SEL_DESELECT);
    info_area_tag_redraw(C);
    return OPERATOR_FINISHED;
  }

  if (!record) {
    return OPERATOR_CANCELLED;
  }

  const CLG_LogRecord *active_item = BLI_findlink((const struct ListBase *)records,
                                                  sinfo->active_index);
  const bool is_active_item_selected = active_item ? active_item->flag & CLG_SELECT : false;

  if (deselect_all) {
    log_records_select_all(records, sinfo, SEL_DESELECT);
  }

  if (active_item == NULL) {
    record->flag |= CLG_SELECT;
    sinfo->active_index = clog_index;
    info_area_tag_redraw(C);
    return OPERATOR_FINISHED;
  }

  if (use_range) {
    if (is_active_item_selected) {
      if (clog_index < sinfo->active_index) {
        for (CLG_LogRecord *i = record; i && i->prev != active_item; i = i->next) {
          i->flag |= CLG_SELECT;
        }
      }
      else {
        for (CLG_LogRecord *record_iter = record; record_iter && record_iter->next != active_item;
             record_iter = record_iter->prev) {
          record_iter->flag |= CLG_SELECT;
        }
      }
      info_area_tag_redraw(C);
      return OPERATOR_FINISHED;
    }
    else {
      log_records_select_all(records, sinfo, SEL_DESELECT);
      record->flag |= CLG_SELECT;
      sinfo->active_index = clog_index;
      info_area_tag_redraw(C);
      return OPERATOR_FINISHED;
    }
  }

  if (extend && (record->flag & CLG_SELECT) && clog_index == sinfo->active_index) {
    record->flag &= ~CLG_SELECT;
  }
  else {
    record->flag |= CLG_SELECT;
    sinfo->active_index = BLI_findindex((const struct ListBase *)records, record);
  }
  info_area_tag_redraw(C);
  return OPERATOR_FINISHED;
}

static int select_clog_pick_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  ARegion *region = CTX_wm_region(C);
  CLG_LogRecordList *records = CLG_log_records_get();
  CLG_LogRecord *record;

  BLI_assert(sinfo->view == INFO_VIEW_CLOG);
  record = info_text_pick(sinfo, region, NULL, event->mval[1]);

  if (record == NULL) {
    RNA_int_set(op->ptr, "clog_index", INDEX_INVALID);
  }
  else {
    RNA_int_set(op->ptr, "clog_index", BLI_findindex((const struct ListBase *)records, record));
  }

  return select_clog_pick_exec(C, op);
}

void INFO_OT_clog_select_pick(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select CLG_LogRecord";
  ot->description = "Select records by index";
  ot->idname = "INFO_OT_clog_select_pick";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->invoke = select_clog_pick_invoke;
  ot->exec = select_clog_pick_exec;

  /* flags */
  /* ot->flag = OPTYPE_REGISTER; */

  /* properties */
  PropertyRNA *prop;
  RNA_def_int(ot->srna,
              "clog_index",
              0,
              INDEX_INVALID,
              INT_MAX,
              "Log Record",
              "Index of the log record",
              0,
              INT_MAX);
  prop = RNA_def_boolean(ot->srna, "extend", false, "Extend", "Extend record selection");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(
      ot->srna, "extend_range", false, "Extend range", "Select a range from active element");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
  prop = RNA_def_boolean(ot->srna,
                         "deselect_all",
                         true,
                         "Deselect On Nothing",
                         "Deselect all when nothing under the cursor");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

static int clog_select_all_exec(bContext *C, wmOperator *op)
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  CLG_LogRecordList *records = CLG_log_records_get();

  int action = RNA_enum_get(op->ptr, "action");
  log_records_select_all(records, sinfo, action);
  info_area_tag_redraw(C);

  return OPERATOR_FINISHED;
}

void INFO_OT_clog_select_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select All";
  ot->description = "Change selection of all visible records";
  ot->idname = "INFO_OT_clog_select_all";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->exec = clog_select_all_exec;

  /* properties */
  WM_operator_properties_select_action(ot, SEL_SELECT, true);
}

/* box_select operator */
static int box_select_exec(bContext *C, wmOperator *op)
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  ARegion *region = CTX_wm_region(C);
  CLG_LogRecordList *records = CLG_log_records_get();
  CLG_LogRecord *report_min, *report_max;
  rcti rect;

  WM_operator_properties_border_to_rcti(op, &rect);

  const eSelectOp sel_op = RNA_enum_get(op->ptr, "mode");
  const int select = (sel_op != SEL_OP_SUB);
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    LISTBASE_FOREACH (CLG_LogRecord *, record, records) {
      if (!is_log_record_visible(record, sinfo)) {
        continue;
      }
      record->flag &= ~CLG_SELECT;
    }
  }

  BLI_assert(sinfo->view == INFO_VIEW_CLOG);
  report_min = info_text_pick(sinfo, region, NULL, rect.ymax);
  report_max = info_text_pick(sinfo, region, NULL, rect.ymin);

  if (report_min == NULL && report_max == NULL) {
    log_records_select_all(records, sinfo, SEL_DESELECT);
  }
  else {
    /* get the first record if none found */
    if (report_min == NULL) {
      // printf("find_min\n");
      LISTBASE_FOREACH (CLG_LogRecord *, record, records) {
        if (is_log_record_visible(record, sinfo)) {
          report_min = record;
          break;
        }
      }
    }

    if (report_max == NULL) {
      // printf("find_max\n");
      for (CLG_LogRecord *record = records->last; record; record = record->prev) {
        if (is_log_record_visible(record, sinfo)) {
          report_max = record;
          break;
        }
      }
    }

    if (report_min == NULL || report_max == NULL) {
      return OPERATOR_CANCELLED;
    }

    for (CLG_LogRecord *record = report_min; (record != report_max->next); record = record->next) {
      if (!is_log_record_visible(record, sinfo)) {
        continue;
      }
      SET_FLAG_FROM_TEST(record->flag, select, CLG_SELECT);
    }
  }

  info_area_tag_redraw(C);
  return OPERATOR_FINISHED;
}

/* ****** Box Select ****** */
void INFO_OT_clog_select_box(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Box Select";
  ot->description = "Toggle box selection";
  ot->idname = "INFO_OT_clog_select_box";

  /* api callbacks */
  ot->invoke = WM_gesture_box_invoke;
  ot->exec = box_select_exec;
  ot->modal = WM_gesture_box_modal;
  ot->cancel = WM_gesture_box_cancel;

  ot->poll = ED_operator_info_active;

  /* flags */
  /* ot->flag = OPTYPE_REGISTER; */

  /* properties */
  WM_operator_properties_gesture_box(ot);
  WM_operator_properties_select_operation_simple(ot);
}

static int clog_delete_exec(bContext *C, wmOperator *UNUSED(op))
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  CLG_LogRecordList *records = CLG_log_records_get();
  int clog_mask = info_report_mask(sinfo);

  CLG_LogRecord *record, *report_next;

  for (record = records->first; record;) {

    report_next = record->next;

    if (is_log_record_visible(record, sinfo) && (record->flag & CLG_SELECT)) {
      printf("NOT IMPLEMENTED YET");
      //      BLI_remlink((struct ListBase *)records, record);
      //      MEM_freeN((void *)record->message);
      //      MEM_freeN(record);
    }

    record = report_next;
  }
  info_area_tag_redraw(C);

  return OPERATOR_FINISHED;
}

void INFO_OT_clog_delete(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Delete Reports";
  ot->description = "Delete selected records";
  ot->idname = "INFO_OT_clog_delete";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->exec = clog_delete_exec;

  /* flags */
  /*ot->flag = OPTYPE_REGISTER;*/

  /* properties */
}

static int clog_copy_exec(bContext *C, wmOperator *UNUSED(op))
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  CLG_LogRecordList *records = CLG_log_records_get();
  CLG_LogRecord *record;

  DynStr *buf_dyn = BLI_dynstr_new();
  char *buf_str;

  for (record = records->first; record; record = record->next) {
    if (is_log_record_visible(record, sinfo) && (record->flag & CLG_SELECT)) {
      BLI_dynstr_append(buf_dyn, record->message);
      BLI_dynstr_append(buf_dyn, "\n");
    }
  }

  buf_str = BLI_dynstr_get_cstring(buf_dyn);
  BLI_dynstr_free(buf_dyn);

  WM_clipboard_text_set(buf_str, 0);

  MEM_freeN(buf_str);
  return OPERATOR_FINISHED;
}

void INFO_OT_clog_copy(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Copy Reports to Clipboard";
  ot->description = "Copy selected records to Clipboard";
  ot->idname = "INFO_OT_clog_copy";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->exec = clog_copy_exec;

  /* flags */
  /*ot->flag = OPTYPE_REGISTER;*/

  /* properties */
}
