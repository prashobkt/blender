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

#define REPORT_INDEX_INVALID -1

/* return true if substring is found */
bool info_filter_text(const Report *report, const char *search_string)
{
  return strstr(report->message, search_string) != NULL;
}

static void reports_select_all(ReportList *reports,
                               int report_mask,
                               const char *search_string,
                               int action)
{
  if (action == SEL_TOGGLE) {
    action = SEL_SELECT;
    for (Report *report = reports->list.last; report; report = report->prev) {
      if (IS_REPORT_VISIBLE(report, report_mask, search_string) && (report->flag & SELECT)) {
        action = SEL_DESELECT;
        break;
      }
    }
  }

  for (Report *report = reports->list.last; report; report = report->prev) {
    if (IS_REPORT_VISIBLE(report, report_mask, search_string)) {
      switch (action) {
        case SEL_SELECT:
          report->flag |= RPT_SELECT;
          break;
        case SEL_DESELECT:
          report->flag &= ~RPT_SELECT;
          break;
        case SEL_INVERT:
          report->flag ^= RPT_SELECT;
          break;
        default:
          BLI_assert(0);
      }
    }
  }
}

int info_report_mask(const SpaceInfo *sinfo)
{
  return sinfo->rpt_mask;
}

// TODO, get this working again!
static int report_replay_exec(bContext *C, wmOperator *UNUSED(op))
{
  //  SpaceInfo *sc = CTX_wm_space_info(C);
  //  ReportList *reports = sinfo->active_reports;
  //  int report_mask = info_report_mask(sc);
  //  Report *report;

#if 0
  sc->type = CONSOLE_TYPE_PYTHON;

  for (report = reports->list.last; report; report = report->prev) {
    if ((report->type & report_mask) && (report->type & RPT_OPERATOR_ALL | RPT_PROPERTY_ALL) &&
        (report->flag & RPT_SELECT)) {
      console_history_add_str(sc, report->message, 0);
      WM_operator_name_call(C, "CONSOLE_OT_execute", WM_OP_EXEC_DEFAULT, NULL);

      ED_area_tag_redraw(CTX_wm_area(C));
    }
  }

  sc->type = CONSOLE_TYPE_REPORT;
#endif
  ED_area_tag_redraw(CTX_wm_area(C));

  return OPERATOR_FINISHED;
}

void INFO_OT_report_replay(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Replay Operators";
  ot->description = "Replay selected reports";
  ot->idname = "INFO_OT_report_replay";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->exec = report_replay_exec;

  /* flags */
  /* ot->flag = OPTYPE_REGISTER; */

  /* properties */
}

static int select_report_pick_exec(bContext *C, wmOperator *op)
{
  const int report_index = RNA_int_get(op->ptr, "report_index");
  const bool extend = RNA_boolean_get(op->ptr, "extend");
  const bool use_range = RNA_boolean_get(op->ptr, "extend_range");
  const bool deselect_all = RNA_boolean_get(op->ptr, "deselect_all");

  SpaceInfo *sinfo = CTX_wm_space_info(C);

  ReportList *reports = sinfo->active_reports;
  Report *report = BLI_findlink(&reports->list, report_index);

  const int report_mask = info_report_mask(sinfo);

  if (report_index == REPORT_INDEX_INVALID) {  // click in empty area
    reports_select_all(reports, report_mask, sinfo->search_string, SEL_DESELECT);
    ED_area_tag_redraw(CTX_wm_area(C));
    return OPERATOR_FINISHED;
  }

  if (!report) {
    return OPERATOR_CANCELLED;
  }

  const Report *active_report = BLI_findlink((const struct ListBase *)reports,
                                             sinfo->active_report_index);
  const bool is_active_report_selected = active_report ? active_report->flag & RPT_SELECT : false;

  if (deselect_all) {
    reports_select_all(reports, report_mask, sinfo->search_string, SEL_DESELECT);
  }

  if (active_report == NULL) {
    report->flag |= RPT_SELECT;
    sinfo->active_report_index = report_index;

    ED_area_tag_redraw(CTX_wm_area(C));
    return OPERATOR_FINISHED;
  }

  if (use_range) {
    if (is_active_report_selected) {
      if (report_index < sinfo->active_report_index) {
        for (Report *i = report; i && i->prev != active_report; i = i->next) {
          i->flag |= RPT_SELECT;
        }
      }
      else {
        for (Report *report_iter = report; report_iter && report_iter->next != active_report;
             report_iter = report_iter->prev) {
          report_iter->flag |= RPT_SELECT;
        }
      }

      ED_area_tag_redraw(CTX_wm_area(C));
      return OPERATOR_FINISHED;
    }
    else {
      reports_select_all(reports, report_mask, sinfo->search_string, SEL_DESELECT);
      report->flag |= RPT_SELECT;
      sinfo->active_report_index = report_index;

      ED_area_tag_redraw(CTX_wm_area(C));
      return OPERATOR_FINISHED;
    }
  }

  if (extend && (report->flag & RPT_SELECT) && report_index == sinfo->active_report_index) {
    report->flag &= ~RPT_SELECT;
  }
  else {
    report->flag |= RPT_SELECT;
    sinfo->active_report_index = BLI_findindex(&reports->list, report);
  }

  ED_area_tag_redraw(CTX_wm_area(C));
  return OPERATOR_FINISHED;
}

static int select_report_pick_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  ARegion *region = CTX_wm_region(C);
  ReportList *reports = sinfo->active_reports;
  Report *report;

  report = info_text_pick(sinfo, region, reports, event->mval[1]);

  if (report == NULL) {
    RNA_int_set(op->ptr, "report_index", REPORT_INDEX_INVALID);
  }
  else {
    RNA_int_set(op->ptr, "report_index", BLI_findindex(&reports->list, report));
  }

  return select_report_pick_exec(C, op);
}

void INFO_OT_select_pick(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Select Report";
  ot->description = "Select reports by index";
  ot->idname = "INFO_OT_select_pick";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->invoke = select_report_pick_invoke;
  ot->exec = select_report_pick_exec;

  /* flags */
  /* ot->flag = OPTYPE_REGISTER; */

  /* properties */
  PropertyRNA *prop;
  RNA_def_int(ot->srna,
              "report_index",
              0,
              REPORT_INDEX_INVALID,
              INT_MAX,
              "Report",
              "Index of the report",
              0,
              INT_MAX);
  prop = RNA_def_boolean(ot->srna, "extend", false, "Extend", "Extend report selection");
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

static int report_select_all_exec(bContext *C, wmOperator *op)
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  ReportList *reports = sinfo->active_reports;
  const int report_mask = info_report_mask(sinfo);

  int action = RNA_enum_get(op->ptr, "action");
  reports_select_all(reports, report_mask, sinfo->search_string, action);

  ED_area_tag_redraw(CTX_wm_area(C));

  return OPERATOR_FINISHED;
}

void INFO_OT_select_all(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "(De)select All";
  ot->description = "Change selection of all visible reports";
  ot->idname = "INFO_OT_select_all";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->exec = report_select_all_exec;

  /* properties */
  WM_operator_properties_select_action(ot, SEL_SELECT, true);
}

/* box_select operator */
static int box_select_exec(bContext *C, wmOperator *op)
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  ARegion *region = CTX_wm_region(C);
  ReportList *reports = sinfo->active_reports;
  int report_mask = info_report_mask(sinfo);
  Report *report_min, *report_max;
  rcti rect;

  WM_operator_properties_border_to_rcti(op, &rect);

  const eSelectOp sel_op = RNA_enum_get(op->ptr, "mode");
  const int select = (sel_op != SEL_OP_SUB);
  if (SEL_OP_USE_PRE_DESELECT(sel_op)) {
    LISTBASE_FOREACH (Report *, report, &reports->list) {
      if ((report->type & report_mask) == 0) {
        continue;
      }
      report->flag &= ~RPT_SELECT;
    }
  }

  report_min = info_text_pick(sinfo, region, reports, rect.ymax);
  report_max = info_text_pick(sinfo, region, reports, rect.ymin);

  if (report_min == NULL && report_max == NULL) {
    reports_select_all(reports, report_mask, sinfo->search_string, SEL_DESELECT);
  }
  else {
    /* get the first report if none found */
    if (report_min == NULL) {
      // printf("find_min\n");
      LISTBASE_FOREACH (Report *, report, &reports->list) {
        if (report->type & report_mask) {
          report_min = report;
          break;
        }
      }
    }

    if (report_max == NULL) {
      // printf("find_max\n");
      for (Report *report = reports->list.last; report; report = report->prev) {
        if (report->type & report_mask) {
          report_max = report;
          break;
        }
      }
    }

    if (report_min == NULL || report_max == NULL) {
      return OPERATOR_CANCELLED;
    }

    for (Report *report = report_min; (report != report_max->next); report = report->next) {
      if ((report->type & report_mask) == 0) {
        continue;
      }
      SET_FLAG_FROM_TEST(report->flag, select, RPT_SELECT);
    }
  }

  ED_area_tag_redraw(CTX_wm_area(C));

  return OPERATOR_FINISHED;
}

/* ****** Box Select ****** */
void INFO_OT_select_box(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Box Select";
  ot->description = "Toggle box selection";
  ot->idname = "INFO_OT_select_box";

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

static int report_delete_exec(bContext *C, wmOperator *UNUSED(op))
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  ReportList *reports = sinfo->active_reports;
  int report_mask = info_report_mask(sinfo);

  Report *report, *report_next;

  for (report = reports->list.first; report;) {

    report_next = report->next;

    if ((report->type & report_mask) && (report->flag & RPT_SELECT)) {
      BLI_remlink(&reports->list, report);
      MEM_freeN((void *)report->message);
      MEM_freeN(report);
    }

    report = report_next;
  }

  ED_area_tag_redraw(CTX_wm_area(C));

  return OPERATOR_FINISHED;
}

void INFO_OT_report_delete(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Delete Reports";
  ot->description = "Delete selected reports";
  ot->idname = "INFO_OT_report_delete";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->exec = report_delete_exec;

  /* flags */
  /*ot->flag = OPTYPE_REGISTER;*/

  /* properties */
}

static int report_copy_exec(bContext *C, wmOperator *UNUSED(op))
{
  SpaceInfo *sinfo = CTX_wm_space_info(C);
  ReportList *reports = sinfo->active_reports;
  int report_mask = info_report_mask(sinfo);

  Report *report;

  DynStr *buf_dyn = BLI_dynstr_new();
  char *buf_str;

  for (report = reports->list.first; report; report = report->next) {
    if ((report->type & report_mask) && (report->flag & RPT_SELECT)) {
      BLI_dynstr_append(buf_dyn, report->message);
      BLI_dynstr_append(buf_dyn, "\n");
    }
  }

  buf_str = BLI_dynstr_get_cstring(buf_dyn);
  BLI_dynstr_free(buf_dyn);

  WM_clipboard_text_set(buf_str, 0);

  MEM_freeN(buf_str);
  return OPERATOR_FINISHED;
}

void INFO_OT_report_copy(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Copy Reports to Clipboard";
  ot->description = "Copy selected reports to Clipboard";
  ot->idname = "INFO_OT_report_copy";

  /* api callbacks */
  ot->poll = ED_operator_info_active;
  ot->exec = report_copy_exec;

  /* flags */
  /*ot->flag = OPTYPE_REGISTER;*/

  /* properties */
}

/** Return newly allocated ReportList created from log records */
ReportList *clog_to_report_list(SpaceInfo *sinfo)
{
  ReportList *reports = MEM_mallocN(sizeof(*reports), "ClogConvertedToReportList");
  BKE_reports_init(reports, 0);
  CLG_LogRecordList *records = CLG_log_record_get();

  if (BLI_listbase_is_empty((const struct ListBase *)records)) {
    return reports;
  }

  CLG_LogRecord *log = records->first;

  while (log) {
    DynStr *dynStr = BLI_dynstr_new();
    if (sinfo->log_format & INFO_LOG_SHOW_TIMESTAMP) {
      char timestamp_str[64];
      const uint64_t timestamp = log->timestamp;
      snprintf(timestamp_str,
               sizeof(timestamp_str),
               "%" PRIu64 ".%03u ",
               timestamp / 1000,
               (uint)(timestamp % 1000));
      BLI_dynstr_appendf(dynStr, "%s", timestamp_str);
    }
    if (sinfo->log_format & INFO_LOG_SHOW_LEVEL) {
      if (log->severity <= CLG_SEVERITY_VERBOSE) {
        BLI_dynstr_appendf(dynStr, "%s:%u ", clg_severity_as_text(log->severity), log->verbosity);
      }
      else {
        BLI_dynstr_appendf(dynStr, "%s ", clg_severity_as_text(log->severity));
      }
    }
    if (sinfo->log_format & INFO_LOG_SHOW_LOG_TYPE) {
      BLI_dynstr_appendf(dynStr, "(%s) ", log->type->identifier);
    }
    if (sinfo->log_format & INFO_LOG_SHOW_FILE_LINE) {
      const char *file_line = (sinfo->use_short_file_line) ? BLI_path_basename(log->file_line) :
                                                             log->file_line;
      BLI_dynstr_appendf(dynStr, "%s ", file_line);
    }
    if (sinfo->log_format & INFO_LOG_SHOW_FUNCTION) {
      BLI_dynstr_appendf(dynStr, "%s ", log->function);
    }
    if (sinfo->log_format & sinfo->use_log_message_new_line) {
      BLI_dynstr_append(dynStr, "\n");
    }

    BLI_dynstr_append(dynStr, log->message);
    char *cstr = BLI_dynstr_get_cstring(dynStr);
    Report *report;
    switch (log->severity) {
      case CLG_SEVERITY_DEBUG:
      case CLG_SEVERITY_VERBOSE:
        report = BKE_report_init(RPT_DEBUG, 0, cstr);
        break;
      case CLG_SEVERITY_INFO:
        report = BKE_report_init(RPT_INFO, 0, cstr);
        break;
      case CLG_SEVERITY_WARN:
        report = BKE_report_init(RPT_WARNING, 0, cstr);
        break;
      case CLG_SEVERITY_ERROR:
      case CLG_SEVERITY_FATAL:
        report = BKE_report_init(RPT_ERROR, 0, cstr);
        break;
      default:
        report = BKE_report_init(RPT_INFO, 0, cstr);
        break;
    }
    BLI_addtail(&reports->list, report);
    MEM_freeN(cstr);
    BLI_dynstr_free(dynStr);
    log = log->next;
  }
  return reports;
}
