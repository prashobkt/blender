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

#include "MEM_guardedalloc.h"
#include <BKE_global.h>
#include <BLI_blenlib.h>
#include <CLG_log.h>
#include <DNA_text_types.h>
#include <ED_text.h>

#include "BLF_api.h"

#include "BLI_math.h"
#include "BLI_string_utf8.h"
#include "BLI_utildefines.h"

#include "GPU_immediate.h"
#include "GPU_state.h"

#include "DNA_userdef_types.h" /* For 'U.dpi_fac' */

#include "UI_interface.h"
#include "UI_interface_icons.h"

#include "../space_text/text_format.h"
#include "textview.h"

#define SPACES_BUFFER_SIZE 32

static CLG_LogRef LOG = {"space_info.textview"};

static void textview_font_begin(const int font_id, const int lheight)
{
  /* Font size in relation to line height. */
  BLF_size(font_id, 0.8f * lheight, 72);
}

typedef struct TextViewDrawState {
  int font_id;
  int cwidth;
  int lheight;
  /** Text vertical offset per line. */
  int lofs;
  int row_vpadding;
  /** Number of characters that fit into the width of the console (fixed width). */
  int columns;
  /** For drawing text. */
  const rcti *draw_rect;
  /** For drawing backgrounds colors which may extend beyond text. */
  const rcti *draw_rect_outer;
  int scroll_ymin, scroll_ymax;
  int *xy;   // [2]
  int *sel;  // [2]
  /* Bottom of view == 0, top of file == combine chars, end of line is lower then start. */
  int *mval_pick_offset;
  const int *mval;  // [2]
} TextViewDrawState;

BLI_INLINE void textview_step_sel(TextViewDrawState *tds, const int step)
{
  tds->sel[0] += step;
  tds->sel[1] += step;
}

static void textview_draw_sel(const char *str,
                              const int xy[2],
                              const int str_len_draw,
                              TextViewDrawState *tds,
                              const uchar bg_sel[4])
{
  const int sel[2] = {tds->sel[0], tds->sel[1]};
  const int cwidth = tds->cwidth;
  const int lheight = tds->lheight;

  if (sel[0] <= str_len_draw && sel[1] >= 0) {
    const int sta = BLI_str_utf8_offset_to_column(str, max_ii(sel[0], 0));
    const int end = BLI_str_utf8_offset_to_column(str, min_ii(sel[1], str_len_draw));

    GPU_blend(GPU_BLEND_ALPHA);

    GPUVertFormat *format = immVertexFormat();
    uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);

    immUniformColor4ubv(bg_sel);
    immRecti(pos, xy[0] + (cwidth * sta), xy[1] + lheight, xy[0] + (cwidth * end), xy[1]);

    immUnbindProgram();

    GPU_blend(GPU_BLEND_NONE);
  }
}

/**
 * \warning Allocated memory for 'r_offsets' must be freed by caller.
 * \return The length in bytes.
 */
static int textview_wrap_offsets(
    const char *str, int len, int width, int *r_lines, int **r_offsets)
{
  int i, end; /* Offset as unicode code-point. */
  int j;      /* Offset as bytes. */

  *r_lines = 1;

  *r_offsets = MEM_callocN(
      sizeof(**r_offsets) *
          (len * BLI_UTF8_WIDTH_MAX / MAX2(1, width - (BLI_UTF8_WIDTH_MAX - 1)) + 1),
      __func__);
  (*r_offsets)[0] = 0;

  for (i = 0, end = width, j = 0; j < len && str[j]; j += BLI_str_utf8_size_safe(str + j)) {
    int columns = BLI_str_utf8_char_width_safe(str + j);

    if (i + columns > end) {
      (*r_offsets)[*r_lines] = j;
      (*r_lines)++;

      end = i + width;
    }
    i += columns;
  }
  return j;
}

/** Do not draw, just advance the height. */
static void textview_draw_string_dry_run(TextViewDrawState *tds,
                                         const char *str,
                                         int str_len,
                                         const bool pad_before,
                                         const bool pad_after)
{
  int tot_lines; /* Total number of lines for wrapping. */
  int *offsets;  /* Offsets of line beginnings for wrapping. */

  str_len = textview_wrap_offsets(str, str_len, tds->columns, &tot_lines, &offsets);

  const int line_height = (tot_lines * tds->lheight) + (tds->row_vpadding * pad_after) +
                          (tds->row_vpadding * pad_before);
  const int line_bottom = tds->xy[1];
  const int line_top = line_bottom + line_height;

  const int y_next = line_top;
  if (tds->mval_pick_offset && tds->mval[1] != INT_MAX && line_bottom <= tds->mval[1]) {
    if (y_next >= tds->mval[1]) {
      int ofs = 0;

      /* Wrap. */
      if (tot_lines > 1) {
        const int iofs = (int)((float)(y_next - tds->mval[1]) / tds->lheight);
        ofs += offsets[MIN2(iofs, tot_lines - 1)];
      }

      /* Last part. */
      ofs += BLI_str_utf8_offset_from_column(str + ofs,
                                             (int)floor((float)tds->mval[0] / tds->cwidth));

      CLAMP(ofs, 0, str_len);
      *tds->mval_pick_offset += str_len - ofs;
    }
    else {
      *tds->mval_pick_offset += str_len + 1;
    }
  }
  tds->xy[1] = y_next;
  MEM_freeN(offsets);
}

static void textview_draw_multiline_dry_run(TextViewDrawState *tds, ListBase *text_lines)
{
  TextViewContextLine *iter_line = text_lines->last;
  while (iter_line->prev) {
    textview_draw_string_dry_run(
        tds, iter_line->line, iter_line->len, false, iter_line == text_lines->last);
    iter_line = iter_line->prev;
  }
  textview_draw_string_dry_run(
      tds, iter_line->line, iter_line->len, true, iter_line == text_lines->last);
}

/**
 * return false if the last line is off the screen
 * should be able to use this for any string type.
 *
 * if fg == NULL, then text_line->format will be used
 *
 * keep in sync with textview_draw_string_dry_run
 */
static bool textview_draw_string(TextViewDrawState *tds,
                                 const char *str,
                                 const char *str_format,
                                 int str_len,
                                 const uchar fg[4],
                                 const uchar bg[4],
                                 const int icon,
                                 const uchar icon_fg[4],
                                 const uchar icon_bg[4],
                                 const uchar bg_sel[4],
                                 const bool pad_before,
                                 const bool pad_after)
{
  BLI_assert(str_format != NULL || fg != NULL);
  int tot_lines; /* Total number of lines for wrapping. */
  int *offsets;  /* Offsets of line beginnings for wrapping. */

  str_len = textview_wrap_offsets(str, str_len, tds->columns, &tot_lines, &offsets);

  const int line_height = (tot_lines * tds->lheight) + (tds->row_vpadding * pad_after) +
                          (tds->row_vpadding * pad_before);
  const int line_bottom = tds->xy[1];
  const int line_top = line_bottom + line_height;

  const int y_next = line_top;

  if (y_next < tds->scroll_ymin) {
    /* Have not reached the drawable area so don't break. */
    tds->xy[1] = y_next;

    /* Adjust selection even if not drawing. */
    if (tds->sel[0] != tds->sel[1]) {
      textview_step_sel(tds, -(str_len + 1));
    }

    MEM_freeN(offsets);
    return true;
  }

  size_t len;
  const char *s;
  int i;

  int sel_orig[2];
  copy_v2_v2_int(sel_orig, tds->sel);

  /* Invert and swap for wrapping. */
  tds->sel[0] = str_len - sel_orig[1];
  tds->sel[1] = str_len - sel_orig[0];

  if (bg) {
    GPUVertFormat *format = immVertexFormat();
    uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    immUniformColor4ubv(bg);
    immRecti(pos, tds->draw_rect_outer->xmin, line_bottom, tds->draw_rect_outer->xmax, line_top);
    immUnbindProgram();
  }

  if (icon_bg) {
    float col[4];
    const int bg_size = UI_DPI_ICON_SIZE * 1.2;
    const float vpadding = (tds->lheight + (tds->row_vpadding * pad_after) +
                            (tds->row_vpadding * pad_before) - bg_size) /
                           2;
    const float hpadding = tds->draw_rect->xmin - (bg_size * 1.2f);

    rgba_uchar_to_float(col, icon_bg);
    UI_draw_roundbox_corner_set(UI_CNR_ALL);
    UI_draw_roundbox_aa(true,
                        hpadding,
                        line_top - bg_size - vpadding,
                        bg_size + hpadding,
                        line_top - vpadding,
                        4 * UI_DPI_FAC,
                        col);
  }

  if (icon) {
    const int vpadding = (tds->lheight + (tds->row_vpadding * pad_after) +
                          (tds->row_vpadding * pad_before) - UI_DPI_ICON_SIZE) /
                         2;
    const int hpadding = tds->draw_rect->xmin - (UI_DPI_ICON_SIZE * 1.3f);

    GPU_blend(GPU_BLEND_ALPHA);
    UI_icon_draw_ex(hpadding,
                    line_top - UI_DPI_ICON_SIZE - vpadding,
                    icon,
                    (16 / UI_DPI_ICON_SIZE),
                    1.0f,
                    0.0f,
                    icon_fg,
                    false);
    GPU_blend(GPU_BLEND_NONE);
  }

  if (pad_after) {
    tds->xy[1] += tds->row_vpadding;
  }

  /* Last part needs no clipping. */
  const int final_offset = offsets[tot_lines - 1];
  len = str_len - final_offset;
  s = str + final_offset;
  const float y = tds->lofs + (tds->row_vpadding * pad_after) + line_bottom;
  if (fg) {
    BLF_position(tds->font_id, tds->xy[0], y, 0);
    BLF_color4ubv(tds->font_id, fg);
    BLF_draw_mono(tds->font_id, s, len, tds->cwidth);
  }
  else {
    const char *format_tmp = str_format + final_offset;
    int str_shift = tds->xy[0];
    char fmt_prev = 0xff;
    for (int j = 0; j < len; j++) {
      if (format_tmp[j] != fmt_prev) {
        text_format_draw_font_color(tds->font_id, fmt_prev = format_tmp[j]);
      }
      const size_t draw_len = BLI_str_utf8_size_safe(s + j);
      BLF_position(tds->font_id, str_shift, y, 0);
      const int columns = BLF_draw_mono(tds->font_id, s + j, draw_len, tds->cwidth);
      str_shift += tds->cwidth * columns;
    }
  }

  if (tds->sel[0] != tds->sel[1]) {
    textview_step_sel(tds, -final_offset);
    const int pos[2] = {tds->xy[0], line_bottom};
    textview_draw_sel(s, pos, len, tds, bg_sel);
  }

  tds->xy[1] += tds->lheight;

  if (fg) {
    BLF_color4ubv(tds->font_id, fg);
  }

  for (i = tot_lines - 1; i > 0; i--) {
    len = offsets[i] - offsets[i - 1];
    s = str + offsets[i - 1];

    if (fg) {
      BLF_position(tds->font_id, tds->xy[0], tds->lofs + tds->xy[1], 0);
      BLF_draw_mono(tds->font_id, s, len, tds->cwidth);
    }
    else {
      const char *format_tmp = str_format + offsets[i - 1];
      int str_shift = tds->xy[0];
      char fmt_prev = 0xff;
      for (int j = 0; j < len; j++) {
        if (format_tmp[j] != fmt_prev) {
          text_format_draw_font_color(tds->font_id, fmt_prev = format_tmp[j]);
        }
        const size_t draw_len = BLI_str_utf8_size_safe(s + j);
        BLF_position(tds->font_id, str_shift, tds->lofs + tds->xy[1], 0);
        const int columns = BLF_draw_mono(tds->font_id, s + j, draw_len, tds->cwidth);
        str_shift += tds->cwidth * columns;
      }
    }

    if (tds->sel[0] != tds->sel[1]) {
      textview_step_sel(tds, len);
      textview_draw_sel(s, tds->xy, len, tds, bg_sel);
    }

    tds->xy[1] += tds->lheight;

    /* Check if we're out of view bounds. */
    if (tds->xy[1] > tds->scroll_ymax) {
      MEM_freeN(offsets);
      return false;
    }
  }

  tds->xy[1] = y_next;

  copy_v2_v2_int(tds->sel, sel_orig);
  textview_step_sel(tds, -(str_len + 1));

  MEM_freeN(offsets);
  return true;
}

/** keep in sync with textview_draw_multiline_dry_run */
static bool textview_draw_multiline(const uchar *fg,
                                    const uchar *bg,
                                    const uchar *icon_fg,
                                    const uchar *icon_bg,
                                    const int icon,
                                    const uchar *bg_sel,
                                    ListBase *text_lines,
                                    const int data_flag,
                                    TextViewDrawState *tds)
{
  bool is_out_of_view_y = false;
  TextViewContextLine *iter_line = text_lines->last;
  const uchar *_fg = (data_flag & TVC_LINE_FG_SIMPLE) ? fg : NULL;
  const uchar *_bg = (data_flag & TVC_LINE_BG) ? bg : NULL;
  while (iter_line->prev && !is_out_of_view_y) {
    is_out_of_view_y = !textview_draw_string(tds,
                                             iter_line->line,
                                             (data_flag & TVC_LINE_FG_SYNTAX) ? iter_line->format :
                                                                                NULL,
                                             iter_line->len,
                                             _fg,
                                             _bg,
                                             0,
                                             NULL,
                                             NULL,
                                             bg_sel,
                                             false,
                                             iter_line == text_lines->last);
    iter_line = iter_line->prev;
  }
  /* only first line has icon */
  is_out_of_view_y = !textview_draw_string(tds,
                                           iter_line->line,
                                           (data_flag & TVC_LINE_FG_SYNTAX) ? iter_line->format :
                                                                              NULL,
                                           iter_line->len,
                                           _fg,
                                           _bg,
                                           (data_flag & TVC_LINE_ICON) ? icon : 0,
                                           (data_flag & TVC_LINE_ICON_FG) ? icon_fg : NULL,
                                           (data_flag & TVC_LINE_ICON_BG) ? icon_bg : NULL,
                                           bg_sel,
                                           true,
                                           iter_line == text_lines->last);
  return is_out_of_view_y;
}

static void textview_clear_text_lines(ListBase *text_lines)
{
  if (!BLI_listbase_is_empty(text_lines)) {
    TextViewContextLine *text_line_iter = text_lines->first;
    while (text_line_iter) {
      TextViewContextLine *text_line_next = text_line_iter->next;
      if (text_line_iter->format) {
        MEM_freeN(text_line_iter->format);
      }
      if (text_line_iter->owns_line) {
        MEM_freeN(text_line_iter->line);
      }
      MEM_freeN(text_line_iter);
      text_line_iter = text_line_next;
    }
    BLI_listbase_clear(text_lines);
  }
}

/**
 * \param r_mval_pick_item: The resulting item clicked on using \a mval_init.
 * Set from the void pointer which holds the current iterator.
 * It's type depends on the data being iterated over.
 * \param r_mval_pick_offset: The offset in bytes of the \a mval_init.
 * Use for selection.
 */
int textview_draw(TextViewContext *tvc,
                  const bool do_draw,
                  const int mval_init[2],
                  void **r_mval_pick_item,
                  int *r_mval_pick_offset)
{
  TextViewDrawState tds = {0};

  const int x_orig = tvc->draw_rect.xmin, y_orig = tvc->draw_rect.ymin;
  int xy[2];
  /* Disable selection by. */
  int sel[2] = {-1, -1};
  uchar fg[4], bg[4], icon_fg[4], icon_bg[4];
  int icon = 0;
  const int font_id = blf_mono_font;

  textview_font_begin(font_id, tvc->lheight);

  xy[0] = x_orig;
  xy[1] = y_orig;

  /* Offset and clamp the results,
   * clamping so moving the cursor out of the bounds doesn't wrap onto the other lines. */
  const int mval[2] = {
      (mval_init[0] == INT_MAX) ?
          INT_MAX :
          CLAMPIS(mval_init[0], tvc->draw_rect.xmin, tvc->draw_rect.xmax) - tvc->draw_rect.xmin,
      (mval_init[1] == INT_MAX) ?
          INT_MAX :
          CLAMPIS(mval_init[1], tvc->draw_rect.ymin, tvc->draw_rect.ymax) + tvc->scroll_ymin,
  };

  if (r_mval_pick_offset != NULL) {
    *r_mval_pick_offset = 0;
  }

  /* Constants for the text-view context. */
  tds.font_id = font_id;
  tds.cwidth = (int)BLF_fixed_width(font_id);
  BLI_assert(tds.cwidth > 0);
  tds.lheight = tvc->lheight;
  tds.row_vpadding = tvc->row_vpadding;
  tds.lofs = -BLF_descender(font_id);
  /* Note, scroll bar must be already subtracted. */
  tds.columns = (tvc->draw_rect.xmax - tvc->draw_rect.xmin) / tds.cwidth;
  /* Avoid divide by zero on small windows. */
  if (tds.columns < 1) {
    tds.columns = 1;
  }
  tds.draw_rect = &tvc->draw_rect;
  tds.draw_rect_outer = &tvc->draw_rect_outer;
  tds.scroll_ymin = tvc->scroll_ymin;
  tds.scroll_ymax = tvc->scroll_ymax;
  tds.xy = xy;
  tds.sel = sel;
  tds.mval_pick_offset = r_mval_pick_offset;
  tds.mval = mval;

  if (tvc->sel_start != tvc->sel_end) {
    sel[0] = tvc->sel_start;
    sel[1] = tvc->sel_end;
  }
  /* TODO (grzelins) add support for defining syntax colors */
  tvc->iter_index = 0;
  if (tvc->begin(tvc)) {
    BLI_assert(IN_RANGE_INCL(tvc->tabnumber, 0, SPACES_BUFFER_SIZE - 1));
    uchar bg_sel[4] = {0};

    if (do_draw && tvc->const_colors) {
      tvc->const_colors(tvc, bg_sel);
    }

    /* Provides context for multiline syntax highlighting. */
    ListBase syntax_lines = {NULL, NULL};
    do {
      ListBase text_lines = {NULL, NULL};
      const int y_prev = xy[1];
      /* get line */
      {
        char *ext_line;
        int ext_len;
        bool free_line;

        tvc->text_get(tvc, &ext_line, &ext_len, (bool *)&free_line);

        /* in future implement here pretty printers/auto formatter */

        /* get rid of tabs */
        if (strchr(ext_line, '\t')) {
          char spaces[SPACES_BUFFER_SIZE];
          for (int i = 0; i < tvc->tabnumber; ++i) {
            spaces[i] = ' ';
          }
          spaces[tvc->tabnumber] = '\0';
          char *replaced_str = BLI_str_replaceN(ext_line, "\t", spaces);
          if (free_line) {
            MEM_freeN(ext_line);
          }
          ext_line = replaced_str;
        }
        else {
          /* we must duplicate before strtok */
          if (!free_line) {
            ext_line = BLI_strdup(ext_line);
            free_line = true;
          }
        }
        /* get rid of newline */
        const char delim[] = "\n";
        char *ptr = strtok(ext_line, delim); /* modifies ext_line! */
        if (ptr == NULL) {
          TextViewContextLine *line = MEM_callocN(sizeof(*line), "tvc->text_get:EmptyLine");
          line->line = BLI_strdup(ext_line);
          line->len = ext_len;
          line->owns_line = true;
          BLI_addtail(&text_lines, line);
        }
        else {
          while (ptr != NULL) {
            TextViewContextLine *line = MEM_callocN(sizeof(*line), "tvc->text_get:NewLine");
            line->line = BLI_strdup(ptr);
            line->len = strlen(ptr);
            line->owns_line = true;
            BLI_addtail(&text_lines, line);
            ptr = strtok(NULL, delim);
          }
        }
        if (free_line) {
          MEM_freeN(ext_line);
        }
        BLI_assert(!BLI_listbase_is_empty(&text_lines));
      }

      if (do_draw) {
        bool is_out_of_view_y;
        const int data_flag = tvc->line_draw_data(tvc, fg, bg, &icon, icon_fg, icon_bg);
        BLI_assert((data_flag & TVC_LINE_FG_SIMPLE) || (data_flag & TVC_LINE_FG_SYNTAX));

        if (data_flag & TVC_LINE_FG_SIMPLE) {
          is_out_of_view_y = textview_draw_multiline(
              fg, bg, icon_fg, icon_bg, icon, bg_sel, &text_lines, data_flag, &tds);
        }
        else {
          /* be careful about direction of iteration (last to first, first to last), it swaps
           * SYNTAX_START and SYNTAX_END */
          if (data_flag & TVC_LINE_FG_SYNTAX_END) {
            textview_clear_text_lines(&syntax_lines);
          }
          if (data_flag & TVC_LINE_FG_SYNTAX) {
            while (!BLI_listbase_is_empty(&text_lines)) {
              BLI_addhead(&syntax_lines, BLI_poptail(&text_lines));
            }
            BLI_listbase_clear(&text_lines);
          }
          if (data_flag & TVC_LINE_FG_SYNTAX_START) {
            if (data_flag & TVC_LINE_FG_SYNTAX_PYTHON) {
              TextFormatType *py_formatter = ED_text_format_get_by_extension("py");
              py_formatter->format_line((TextLine *)(syntax_lines.first), tvc->tabnumber, true);
            }
            else {
              /* unknown syntax */
              BLI_assert(0);
            }

            is_out_of_view_y = textview_draw_multiline(
                fg, bg, icon_fg, icon_bg, icon, bg_sel, &syntax_lines, data_flag, &tds);
            textview_clear_text_lines(&syntax_lines);
          }
          /* pass, do not draw line (waiting for more syntax lines) */
        }

        /* We always want the cursor to draw, but only in first line. */
        if (tvc->draw_cursor && tvc->iter_index == 0) {
          tvc->draw_cursor(tvc, tds.cwidth, tds.columns);
        }

        textview_clear_text_lines(&text_lines);

        /* When drawing, if we pass v2d->cur.ymax, then quit. */
        if (is_out_of_view_y) {
          break;
        }
      }
      else {
        textview_draw_multiline_dry_run(&tds, &text_lines);
        textview_clear_text_lines(&text_lines);
      }

      if ((mval[1] != INT_MAX) && (mval[1] >= y_prev && mval[1] <= xy[1])) {
        *r_mval_pick_item = (void *)tvc->iter;
        break;
      }

      tvc->iter_index++;
    } while (tvc->step(tvc));

    if (G.debug & G_DEBUG && !BLI_listbase_is_empty(&syntax_lines)) {
      CLOG_WARN(&LOG, "There are not printed lines left:%d!!", BLI_listbase_count(&syntax_lines));
    }
  }

  tvc->end(tvc);

  /* Sanity checks (bugs here can be tricky to track down). */
  BLI_assert(tds.lheight == tvc->lheight);
  BLI_assert(tds.row_vpadding == tvc->row_vpadding);

  xy[1] += tvc->lheight * 2;

  return xy[1] - y_orig;
}
