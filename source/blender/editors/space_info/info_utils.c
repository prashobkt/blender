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

#include <string.h>

#include "BLI_blenlib.h"
#include "BLI_fnmatch.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"

#include "WM_api.h"
#include "WM_types.h"

#include "CLG_log.h"
#include "ED_screen.h"
#include "info_intern.h"

/** Redraw every possible space info */
void info_area_tag_redraw(const bContext *C)
{
  struct wmWindowManager *wm = CTX_wm_manager(C);
  LISTBASE_FOREACH (wmWindow *, win, &wm->windows) {
    bScreen *screen = WM_window_get_active_screen(win);
    LISTBASE_FOREACH (ScrArea *, area, &screen->areabase) {
      if (area->spacetype == SPACE_INFO) {
        ED_area_tag_redraw(area);
      }
    }
  }
}

bool info_match_string_filter(const char *search_pattern,
                              const char *string,
                              const bool use_match_case,
                              const bool use_match_glob,
const bool use_reverse_match)
{
if (STREQ(search_pattern, "")) {
    return true;
  }
  if (!use_match_glob) {
    char *(*compare_func)(const char *, const char *) = use_match_case ? strstr : BLI_strcasestr ;
    bool result = compare_func(string, search_pattern) != NULL;
    if (use_reverse_match) {
      return !result;
    }
    return result;
  }
  bool result = fnmatch(search_pattern, string, use_match_case ? 0 : FNM_CASEFOLD) == 0;
  if (use_reverse_match) {
    return !result;
  }
  return result;
}
