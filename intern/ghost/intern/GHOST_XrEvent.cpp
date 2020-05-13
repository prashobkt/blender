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
 * \ingroup GHOST
 */

#include <iostream>

#include "GHOST_C-api.h"
#include "GHOST_XrContext.h"
#include "GHOST_Xr_intern.h"

static bool GHOST_XrEventPollNext(XrInstance instance, XrEventDataBuffer &r_event_data, bool debug)
{
  /* (Re-)initialize as required by specification. */
  r_event_data.type = XR_TYPE_EVENT_DATA_BUFFER;
  r_event_data.next = nullptr;

  XrResult result = xrPollEvent(instance, &r_event_data);
  bool success = (result == XR_SUCCESS);
  if (!success) {
    switch (result) {
      case XR_EVENT_UNAVAILABLE:
        /* printf("FAIL Event received: XR_EVENT_UNAVAILABLE\n"); // spammy, no print here, we have this event every time nothing gets polled. */
        break;
      case XR_ERROR_INSTANCE_LOST:
        if (debug) printf("POLL Fail Event received: XR_ERROR_INSTANCE_LOST\n");
        break;
      case XR_ERROR_RUNTIME_FAILURE:
        if (debug) printf("POLL Fail Event received: XR_ERROR_RUNTIME_FAILURE\n");
        break;
      case XR_ERROR_HANDLE_INVALID:
        if (debug) printf("POLL Fail Event received: XR_ERROR_HANDLE_INVALID\n");
        break;
      case XR_ERROR_VALIDATION_FAILURE:
        if (debug) printf("POLL Fail Event received: XR_ERROR_VALIDATION_FAILURE\n");
        break;
      default:
          if (debug) printf("POLL Fail Event received(should not happen): %i\n", result);
          break;
    }
  }
  return success;
}

GHOST_TSuccess GHOST_XrEventsHandle(GHOST_XrContextHandle xr_contexthandle)
{
  GHOST_XrContext *xr_context = (GHOST_XrContext *)xr_contexthandle;
  XrEventDataBuffer event_buffer; /* Structure big enough to hold all possible events. */

  if (xr_context == NULL) {
    return GHOST_kFailure;
  }

  while (GHOST_XrEventPollNext(xr_context->getInstance(), event_buffer, xr_context->isDebugMode())) {
    XrEventDataBaseHeader *event = (XrEventDataBaseHeader *)&event_buffer;

    switch (event->type) {
        /*
        XR_TYPE_EVENT_DATA_EVENTS_LOST                          (XrEventDataEventsLost *)event                     .lostEventCount = number of removed events because of overflow of events.
        XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING                (XrEventDataInstanceLossPending *)event            .lossTime = application if about to loose the instance (occurs when software updates)
        XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED          (XrEventDataInteractionProfileChanged *)event      .session = bla bla bla
        XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING       (XrEventDataReferenceSpaceChangePending *)event    quand on recentre la vue. la struct contient la pose dans le precedent espace et le temps avant que l'api retourne des poses dans le nouvel espace.
        XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED                (XrEventDataSessionStateChanged *)event            lifecycle change of an XrSession, with a new XrSessionState
        XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT                    (...)
        XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR          (...)
        XR_TYPE_EVENT_DATA_MAIN_SESSION_VISIBILITY_CHANGED_EXTX (...)
        */
      case XR_TYPE_EVENT_DATA_EVENTS_LOST: 
      {
        if (xr_context->isDebugMode())
        {
            XrEventDataEventsLost* e = (XrEventDataEventsLost*)event;
            printf("Event XR_TYPE_EVENT_DATA_EVENTS_LOST received. LostEventCount = %d\n", e->lostEventCount);
        }
        return GHOST_kFailure;
      }
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: 
      {
          if (xr_context->isDebugMode())
          {
              printf("Event XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED => ");
          }
          xr_context->handleSessionStateChange((XrEventDataSessionStateChanged *)event);
          return GHOST_kSuccess;
      }
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: 
      {
        if (xr_context->isDebugMode())
        {
            XrEventDataInteractionProfileChanged *e = (XrEventDataInteractionProfileChanged *)event;
            printf("Event XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED received.\n");
        }
        return GHOST_kFailure;
      }
      case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: 
      {
        if (xr_context->isDebugMode())
        {
            XrEventDataReferenceSpaceChangePending *e = (XrEventDataReferenceSpaceChangePending *)event;
            printf("Event XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING received.\n");
        }
        return GHOST_kFailure;
      }
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: 
      {
        if (xr_context->isDebugMode())
        {
            XrEventDataInstanceLossPending *e = (XrEventDataInstanceLossPending *)event;
            printf("Event XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING received. LossTime: %d\n", e->lossTime);
        }
        GHOST_XrContextDestroy(xr_contexthandle);
        return GHOST_kSuccess;
      }
      case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT:
      {
          if (xr_context->isDebugMode())
          {
              printf("Event XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT received.\n");
          }
          return GHOST_kFailure;
      }
      case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR:
      {
          if (xr_context->isDebugMode())
          {
              printf("Event XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR received.\n");
          }
          return GHOST_kFailure;
      }
      default: 
      {
        if (xr_context->isDebugMode()) 
        {
            printf("Unhandled event: %i\n", event->type);
        }
        return GHOST_kFailure;
      }
    }
  }

  return GHOST_kFailure;
}
