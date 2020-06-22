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

#ifndef __GHOST_XRSESSION_H__
#define __GHOST_XRSESSION_H__

#include <map>
#include <memory>
#include <string>

#include "GHOST_Types.h"
#include "GHOST_Xr_intern.h"

class GHOST_XrContext;
class GHOST_XrSwapchain;
struct GHOST_XrDrawInfo;
struct OpenXRSessionData;
struct GHOST_XrActionCreateInfo;
struct GHOST_XrSubactionInfo;
struct GHOST_XrSubactionsCreateInfo;
struct GHOST_XrSpaceCreateInfo;

struct GHOST_XrSpaceLocation;

class GHOST_XrAction {
 private:
  XrSession xrSession;
  XrInstance xrInstance;

 public:
  XrAction handle;
  XrPath subPath;
  GHOST_XrAction(XrSession xrSession, XrInstance xrInstance, XrAction handle, XrPath subPath);

  GHOST_XrActionStateBoolean getActionStateBoolean();
  GHOST_XrActionStateFloat getActionStateFloat();
  GHOST_XrActionStateVector2f getActionStateVector2f();
  bool isPoseActive();
};

using GHOST_XrActionMap = std::map<std::string, GHOST_XrAction>;

class GHOST_XrActionSet {
 private:
  GHOST_XrActionMap actionMap;
  XrSession xrSession;
  XrInstance xrInstance;

 public:
  XrActionSet handle;

  GHOST_XrAction &getAction(const std::string &actionID);
  GHOST_XrAction &createSubactions(GHOST_XrSubactionsCreateInfo info);
  GHOST_XrAction &createAction(GHOST_XrActionCreateInfo info);

  GHOST_XrActionSet(XrSession xrSession, XrInstance xrInstance, XrActionSet handle);
  ~GHOST_XrActionSet();
};

using GHOST_XrActionSetMap = std::map<std::string, GHOST_XrActionSet>;
using GHOST_XrInteractionMap = std::map<XrPath, std::vector<XrActionSuggestedBinding>>;

class GHOST_XrSession {
 public:
  enum LifeExpectancy {
    SESSION_KEEP_ALIVE,
    SESSION_DESTROY,
  };

  GHOST_XrSession(GHOST_XrContext *xr_context);
  ~GHOST_XrSession();

  GHOST_XrActionSet &createActionSet(const std::string &name, const std::string &localizedName);
  GHOST_XrActionSet &getActionSet(const std::string &setID);

  GHOST_XrPose getSpacePose(GHOST_XrSpace space);

  void initXrActionsDefault();
  void suggestBinding(XrAction handle, const std::string &profile, const std::string &binding);
  void bindAndAttachActions();

  void start(const GHOST_XrSessionBeginInfo *begin_info);
  void requestEnd();

  LifeExpectancy handleStateChangeEvent(const XrEventDataSessionStateChanged *lifecycle);

  bool isRunning() const;
  bool needsUpsideDownDrawing() const;

  void unbindGraphicsContext(); /* Public so context can ensure it's unbound as needed. */

  void draw(void *draw_customdata);

 private:
  /** Pointer back to context managing this session. Would be nice to avoid, but needed to access
   * custom callbacks set before session start. */
  class GHOST_XrContext *m_context;
  std::unique_ptr<OpenXRSessionData> m_oxr; /* Could use stack, but PImpl is preferable. */

  /** Active Ghost graphic context. Owned by Blender, not GHOST. */
  class GHOST_Context *m_gpu_ctx = nullptr;
  std::unique_ptr<class GHOST_IXrGraphicsBinding> m_gpu_binding;

  /** Rendering information. Set when drawing starts. */
  std::unique_ptr<GHOST_XrDrawInfo> m_draw_info;

  void initSystem();
  void beginSession();
  void endSession();

  void init_xr_action_default();

  void bindGraphicsContext();

  void prepareDrawing();
  XrCompositionLayerProjection drawLayer(
      std::vector<XrCompositionLayerProjectionView> &r_proj_layer_views, void *draw_customdata);
  void drawView(GHOST_XrSwapchain &swapchain,
                XrCompositionLayerProjectionView &r_proj_layer_view,
                XrSpaceLocation &view_location,
                XrView &view,
                void *draw_customdata);
  void beginFrameDrawing();
  void endFrameDrawing(std::vector<XrCompositionLayerBaseHeader *> *layers);

  XrPosef locateSpace(GHOST_XrSpace space, GHOST_XrTime time);
  XrSpace createSpace(GHOST_XrAction &action, XrPosef poseInSpace);
  void updateActions(XrTime displayTime);
};

#endif /* GHOST_XRSESSION_H__ */
