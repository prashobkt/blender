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

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <list>
#include <sstream>

#include "GHOST_C-api.h"

#include "GHOST_IXrGraphicsBinding.h"
#include "GHOST_XrContext.h"
#include "GHOST_XrException.h"
#include "GHOST_XrSwapchain.h"
#include "GHOST_Xr_intern.h"

#include "GHOST_XrSession.h"

/** Oculus Touch OpenXR profile data */
struct OculusTouchProfile {
  bool valid;

  XrActionSet actionSet;
  XrPath handPaths[2];
  XrSpace handSpaces[2];

  /* Common actions for each hands */
  XrAction squeezeValueAction;
  XrAction triggerValueAction;
  XrAction triggerTouchAction;
  XrAction thumbstickXAction;
  XrAction thumbstickYAction;
  XrAction thumbstickClickAction;
  XrAction thumbstickTouchAction;
  XrAction thumbrestTouchAction;
  XrAction gripPoseAction;
  XrAction aimPoseAction;
  XrAction hapticAction;

  /* Specific hand actions */
  XrAction leftXClickAction;
  XrAction leftXTouchAction;
  XrAction leftYClickAction;
  XrAction leftYTouchAction;
  XrAction leftMenuClickAction;

  XrAction rightAClickAction;
  XrAction rightATouchAction;
  XrAction rightBClickAction;
  XrAction rightBTouchAction;
  XrAction rightSystemClickAction;
};

enum class OpenXrProfile {
    UNKNOWN,
    OCULUS_TOUCH
};

struct OpenXRSessionData {
  XrSystemId system_id = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;

  /* Only stereo rendering supported now. */
  const XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  XrSpace reference_space;
  XrSpace view_space;
  std::vector<XrView> views;
  std::vector<GHOST_XrSwapchain> swapchains;

  OpenXrProfile detectedProfile = OpenXrProfile::UNKNOWN;
  OculusTouchProfile oculusTouchProfile;
};

struct GHOST_XrDrawInfo {
  XrFrameState frame_state;

  /** Time at frame start to benchmark frame render durations. */
  std::chrono::high_resolution_clock::time_point frame_begin_time;
  /* Time previous frames took for rendering (in ms). */
  std::list<double> last_frame_times;
};

/* One structure for all devices */
struct GHOST_XrControllersData {
  XrPosef left_pose;
  float left_trigger_value;
  bool left_trigger_touch;
  float left_grip_value;
  bool left_primary_click;
  bool left_primary_touch;
  bool left_secondary_click;
  bool left_secondary_touch;

  XrPosef right_pose;
  float right_trigger_value;
  bool right_trigger_touch;
  float right_grip_value;
  bool right_primary_click;
  bool right_primary_touch;
  bool right_secondary_click;
  bool right_secondary_touch;

  float left_thumbstick_x;
  float left_thumbstick_y;
  bool left_thumbstick_click;
  bool left_thumbstick_touch;
  float right_thumbstick_x;
  float right_thumbstick_y;
  bool right_thumbstick_click;
  bool right_thumbstick_touch;
};

/* -------------------------------------------------------------------- */
/** \name Create, Initialize and Destruct
 *
 * \{ */

GHOST_XrSession::GHOST_XrSession(GHOST_XrContext *xr_context)
    : m_context(xr_context), m_oxr(new OpenXRSessionData())
{
}

GHOST_XrSession::~GHOST_XrSession()
{
  unbindGraphicsContext();

  /* Destroy action set (which also destroy all handles of actions in that action set) */
  switch (m_oxr->detectedProfile) {
    case OpenXrProfile::OCULUS_TOUCH:
      xrDestroyActionSet(m_oxr->oculusTouchProfile.actionSet);
      break;
  }

  m_oxr->swapchains.clear();

  if (m_oxr->reference_space != XR_NULL_HANDLE) {
    CHECK_XR_ASSERT(xrDestroySpace(m_oxr->reference_space));
  }
  if (m_oxr->view_space != XR_NULL_HANDLE) {
    CHECK_XR_ASSERT(xrDestroySpace(m_oxr->view_space));
  }
  if (m_oxr->session != XR_NULL_HANDLE) {
    CHECK_XR_ASSERT(xrDestroySession(m_oxr->session));
  }

  m_oxr->session = XR_NULL_HANDLE;
  m_oxr->session_state = XR_SESSION_STATE_UNKNOWN;

  m_context->getCustomFuncs().session_exit_fn(m_context->getCustomFuncs().session_exit_customdata);
}

/**
 * A system in OpenXR the combination of some sort of HMD plus controllers and whatever other
 * devices are managed through OpenXR. So this attempts to init the HMD and the other devices.
 */
void GHOST_XrSession::initSystem()
{
  assert(m_context->getInstance() != XR_NULL_HANDLE);
  assert(m_oxr->system_id == XR_NULL_SYSTEM_ID);

  XrSystemGetInfo system_info = {};
  system_info.type = XR_TYPE_SYSTEM_GET_INFO;
  system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

  CHECK_XR(xrGetSystem(m_context->getInstance(), &system_info, &m_oxr->system_id),
           "Failed to get device information. Is a device plugged in?");

  /* Get detected device */
  XrSystemProperties xrSystemProperties = {XR_TYPE_SYSTEM_PROPERTIES};
  xrSystemProperties.next = NULL;
  xrSystemProperties.graphicsProperties = {0};
  xrSystemProperties.trackingProperties = {0};
  CHECK_XR(xrGetSystemProperties(m_context->getInstance(), m_oxr->system_id, &xrSystemProperties),
      "Failed to get system properties.");

  if (strcmp(xrSystemProperties.systemName, "Quest") == 0 || strcmp(xrSystemProperties.systemName, "Oculus Rift S") == 0) {
    m_oxr->detectedProfile = OpenXrProfile::OCULUS_TOUCH;
  }
}

/** \} */ /* Create, Initialize and Destruct */

/* -------------------------------------------------------------------- */
/** \name State Management
 *
 * \{ */

static void create_reference_spaces(OpenXRSessionData *oxr, const GHOST_XrPose *base_pose)
{
  XrReferenceSpaceCreateInfo create_info = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  create_info.poseInReferenceSpace.orientation.w = 1.0f;

  create_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
#if 0
/* TODO
 *
 * Proper reference space set up is not supported yet. We simply hand OpenXR
 * the global space as reference space and apply its pose onto the active
 * camera matrix to get a basic viewing experience going. If there's no active
 * camera with stick to the world origin.
 *
 * Once we have proper reference space set up (i.e. a way to define origin, up-
 * direction and an initial view rotation perpendicular to the up-direction),
 * we can hand OpenXR a proper reference pose/space.
 */
  create_info.poseInReferenceSpace.position.x = base_pose->position[0];
  create_info.poseInReferenceSpace.position.y = base_pose->position[1];
  create_info.poseInReferenceSpace.position.z = base_pose->position[2];
  create_info.poseInReferenceSpace.orientation.x = base_pose->orientation_quat[1];
  create_info.poseInReferenceSpace.orientation.y = base_pose->orientation_quat[2];
  create_info.poseInReferenceSpace.orientation.z = base_pose->orientation_quat[3];
  create_info.poseInReferenceSpace.orientation.w = base_pose->orientation_quat[0];
#else
  (void)base_pose;
#endif

  CHECK_XR(xrCreateReferenceSpace(oxr->session, &create_info, &oxr->reference_space),
           "Failed to create reference space.");

  create_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  CHECK_XR(xrCreateReferenceSpace(oxr->session, &create_info, &oxr->view_space),
           "Failed to create view reference space.");
}

/* Helper function to create and bind an OpenXR action */
static void create_and_bind_xr_action(XrInstance xrInstance,
                             XrActionSet actionSet,
                             const XrActionCreateInfo *actionInfo,
                             XrAction *action,
                             std::vector<XrActionSuggestedBinding> &bindings,
                             const std::vector<std::string> &paths)
{
  std::string error_msg = "failed to create \"";
  error_msg += actionInfo->actionName;
  error_msg += "\" action";
  CHECK_XR(xrCreateAction(actionSet, actionInfo, action), error_msg.c_str());
  for (int i = 0; i < paths.size(); ++i) {
    XrPath xrPaths;
    xrStringToPath(xrInstance, paths[i].c_str(), &xrPaths);
    bindings.push_back(XrActionSuggestedBinding{*action, xrPaths});
  }
}

static void init_xr_oculus_touch_profile(OpenXRSessionData *oxr, XrInstance xrInstance)
{
  OculusTouchProfile *profile = &oxr->oculusTouchProfile;

  /* Create action set */
  XrActionSetCreateInfo actionSetInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
  actionSetInfo.next = NULL;
  actionSetInfo.priority = 0;
  strcpy(actionSetInfo.actionSetName, "actionset");
  strcpy(actionSetInfo.localizedActionSetName, "ActionSet");
  CHECK_XR(xrCreateActionSet(xrInstance, &actionSetInfo, &profile->actionSet),
           "Failed to create action set.");

  /* Create common actions for each hand */
  int const handsCount = 2;
  xrStringToPath(xrInstance, "/user/hand/left", &profile->handPaths[0]);
  xrStringToPath(xrInstance, "/user/hand/right", &profile->handPaths[1]);

  std::vector<XrActionSuggestedBinding> bindings;

  XrActionCreateInfo actionInfo = {XR_TYPE_ACTION_CREATE_INFO};
  actionInfo.next = NULL;
  actionInfo.countSubactionPaths = handsCount;
  actionInfo.subactionPaths = profile->handPaths;

  /* ...of type float */
  actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;

  strcpy(actionInfo.actionName, "trigger");
  strcpy(actionInfo.localizedActionName, "Trigger Value");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->triggerValueAction,
                            bindings,
                            {"/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value"});

  strcpy(actionInfo.actionName, "squeeze");
  strcpy(actionInfo.localizedActionName, "Squeeze Value");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->squeezeValueAction,
                            bindings,
                            {"/user/hand/left/input/squeeze/value", "/user/hand/right/input/squeeze/value"});

  strcpy(actionInfo.actionName, "thumbstick_x");
  strcpy(actionInfo.localizedActionName, "Thumbstick X Value");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->thumbstickXAction,
                            bindings,
                            {"/user/hand/left/input/thumbstick/x", "/user/hand/right/input/thumbstick/x"});

  strcpy(actionInfo.actionName, "thumbstick_y");
  strcpy(actionInfo.localizedActionName, "Thumbstick Y Value");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->thumbstickYAction,
                            bindings,
                            {"/user/hand/left/input/thumbstick/y", "/user/hand/right/input/thumbstick/y"});

  /* ...of type bool */
  actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;

  strcpy(actionInfo.actionName, "thumbstickclick");
  strcpy(actionInfo.localizedActionName, "Thumbstick Click");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->thumbstickClickAction,
                            bindings,
                            {"/user/hand/left/input/thumbstick/click", "/user/hand/right/input/thumbstick/click"});

  strcpy(actionInfo.actionName, "thumbsticktouch");
  strcpy(actionInfo.localizedActionName, "Thumbstick Touch");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->thumbstickTouchAction,
                            bindings,
                            {"/user/hand/left/input/thumbstick/touch", "/user/hand/right/input/thumbstick/touch"});

  strcpy(actionInfo.actionName, "triggertouch");
  strcpy(actionInfo.localizedActionName, "Trigger Touch");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->triggerTouchAction,
                            bindings,
                            {"/user/hand/left/input/trigger/touch", "/user/hand/right/input/trigger/touch"});

  /* ...of type haptic */
  actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;

  strcpy(actionInfo.actionName, "haptic");
  strcpy(actionInfo.localizedActionName, "Haptic");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->hapticAction,
                            bindings,
                            {"/user/hand/left/output/haptic", "/user/hand/right/output/haptic"});

  /* ...of type pose */
  actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;

  strcpy(actionInfo.actionName, "handpose");
  strcpy(actionInfo.localizedActionName, "Hand Pose");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->gripPoseAction,
                            bindings,
                            {"/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose"});

  /* Create spaces for poses */
  XrActionSpaceCreateInfo actionSpaceInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
  actionSpaceInfo.next = NULL;
  actionSpaceInfo.action = profile->gripPoseAction;
  actionSpaceInfo.poseInActionSpace.orientation.w = 1.f;
  actionSpaceInfo.subactionPath = profile->handPaths[0];
  CHECK_XR(xrCreateActionSpace(oxr->session, &actionSpaceInfo, &profile->handSpaces[0]),
           "failed to create left hand pose space");

  actionSpaceInfo.subactionPath = profile->handPaths[1];
  CHECK_XR(xrCreateActionSpace(oxr->session, &actionSpaceInfo, &profile->handSpaces[1]),
           "failed to create right hand pose space");

  /* Create unique actions of each hand */
  actionInfo.countSubactionPaths = 0;
  actionInfo.subactionPaths = NULL;

  actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;

  strcpy(actionInfo.actionName, "leftxclick");
  strcpy(actionInfo.localizedActionName, "Left X Click");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->leftXClickAction,
                            bindings,
                            {"/user/hand/left/input/x/click"});

  strcpy(actionInfo.actionName, "leftxtouch");
  strcpy(actionInfo.localizedActionName, "Left X Touch");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->leftXTouchAction,
                            bindings,
                            {"/user/hand/left/input/x/touch"});

  strcpy(actionInfo.actionName, "leftyclick");
  strcpy(actionInfo.localizedActionName, "Left Y Click");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->leftYClickAction,
                            bindings,
                            {"/user/hand/left/input/y/click"});

  strcpy(actionInfo.actionName, "leftytouch");
  strcpy(actionInfo.localizedActionName, "Left Y Touch");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->leftYTouchAction,
                            bindings,
                            {"/user/hand/left/input/y/touch"});

  strcpy(actionInfo.actionName, "rightaclick");
  strcpy(actionInfo.localizedActionName, "Right A Click");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->rightAClickAction,
                            bindings,
                            {"/user/hand/right/input/a/click"});

  strcpy(actionInfo.actionName, "rightatouch");
  strcpy(actionInfo.localizedActionName, "Right A Touch");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->rightATouchAction,
                            bindings,
                            {"/user/hand/right/input/a/touch"});

  strcpy(actionInfo.actionName, "rightbclick");
  strcpy(actionInfo.localizedActionName, "Right B Click");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->rightBClickAction,
                            bindings,
                            {"/user/hand/right/input/b/click"});

  strcpy(actionInfo.actionName, "rightbtouch");
  strcpy(actionInfo.localizedActionName, "Right B Touch");
  create_and_bind_xr_action(xrInstance,
                            profile->actionSet,
                            &actionInfo,
                            &profile->rightBTouchAction,
                            bindings,
                            {"/user/hand/right/input/b/touch"});

  /* Create interaction profile */
  XrPath oculusInteractionProfilePath;
  CHECK_XR(xrStringToPath(xrInstance,
                          "/interaction_profiles/oculus/touch_controller",
                          &oculusInteractionProfilePath),
           "failed to get oculus interaction profile");

  /* Suggest bindings */
  XrInteractionProfileSuggestedBinding suggestedBindings = {
      XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  suggestedBindings.interactionProfile = oculusInteractionProfilePath;
  suggestedBindings.countSuggestedBindings = bindings.size();
  suggestedBindings.suggestedBindings = bindings.data();
  CHECK_XR(xrSuggestInteractionProfileBindings(xrInstance, &suggestedBindings),
           "failed to suggest bindings");

  /* Attach action set to session */
  XrSessionActionSetsAttachInfo attachInfo = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attachInfo.next = NULL;
  attachInfo.countActionSets = 1;
  attachInfo.actionSets = &profile->actionSet;
  CHECK_XR(xrAttachSessionActionSets(oxr->session, &attachInfo), "failed to attach action set");

  profile->valid = true;
}

static void init_xr_controllers(OpenXRSessionData* oxr, XrInstance xrInstance)
{
  switch (oxr->detectedProfile) {
    case OpenXrProfile::OCULUS_TOUCH:
      init_xr_oculus_touch_profile(oxr, xrInstance);
      break;
  }
}

static void fetch_oculus_touch_xr_data(OpenXRSessionData *oxr,
                                       GHOST_XrDrawInfo *drawInfo,
                                       GHOST_XrControllersData &controllers_data)
{
  OculusTouchProfile profile = oxr->oculusTouchProfile;
  if (!profile.valid) {
    throw GHOST_XrException(
        "Unable to fetch Oculus Touch controllers data: profile not initialized");
  }

  /* Retrieve active action set */
  const XrActiveActionSet activeActionSet = {profile.actionSet, XR_NULL_PATH};

  XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO};
  syncInfo.countActiveActionSets = 1;
  syncInfo.activeActionSets = &activeActionSet;
  CHECK_XR(xrSyncActions(oxr->session, &syncInfo), "failed to sync actions");

  XrActionStateFloat floatState = {XR_TYPE_ACTION_STATE_FLOAT};
  floatState.next = NULL;
  XrActionStateBoolean boolState = {XR_TYPE_ACTION_STATE_BOOLEAN};
  boolState.next = NULL;

  /* Retrieve hands common actions */
  const int hands = 2;
  for (int i = 0; i < hands; i++) {
    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.next = NULL;
    getInfo.subactionPath = profile.handPaths[i];

    /* Trigger values & touch */
    getInfo.action = profile.triggerValueAction;
    CHECK_XR(xrGetActionStateFloat(oxr->session, &getInfo, &floatState),
             "failed to get trigger value!");
    if (i == 0)
      controllers_data.left_trigger_value = floatState.currentState;
    else
      controllers_data.right_trigger_value = floatState.currentState;

    getInfo.action = profile.triggerTouchAction;
    CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
             "failed to get trigger touch!");
    if (i == 0)
      controllers_data.left_trigger_touch = boolState.currentState;
    else
      controllers_data.right_trigger_touch = boolState.currentState;

    /* Squeeze values */
    getInfo.action = profile.squeezeValueAction;
    CHECK_XR(xrGetActionStateFloat(oxr->session, &getInfo, &floatState),
             "failed to get squeeze value!");
    if (i == 0)
      controllers_data.left_grip_value = floatState.currentState;
    else
      controllers_data.right_grip_value = floatState.currentState;

    /* Thumbstick X values */
    getInfo.action = profile.thumbstickXAction;
    CHECK_XR(xrGetActionStateFloat(oxr->session, &getInfo, &floatState),
             "failed to get thumb X value!");
    if (i == 0)
      controllers_data.left_thumbstick_x = floatState.currentState;
    else
      controllers_data.right_thumbstick_x = floatState.currentState;

    /* Thumbstick Y values */
    getInfo.action = profile.thumbstickYAction;
    CHECK_XR(xrGetActionStateFloat(oxr->session, &getInfo, &floatState),
             "failed to get thumb Y value!");
    if (i == 0)
      controllers_data.left_thumbstick_y = floatState.currentState;
    else
      controllers_data.right_thumbstick_y = floatState.currentState;

    /* Thumbstick click values & touch */
    getInfo.action = profile.thumbstickClickAction;
    CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
             "failed to get thumb click value!");
    if (i == 0)
      controllers_data.left_thumbstick_click = boolState.currentState;
    else
      controllers_data.right_thumbstick_click = boolState.currentState;

    getInfo.action = profile.thumbstickTouchAction;
    CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
             "failed to get thumb touch value!");
    if (i == 0)
      controllers_data.left_thumbstick_touch = boolState.currentState;
    else
      controllers_data.right_thumbstick_touch = boolState.currentState;

    /* Controller action poses */
    getInfo.action = profile.gripPoseAction;
    XrActionStatePose poseState = {XR_TYPE_ACTION_STATE_POSE};
    poseState.next = NULL;
    CHECK_XR(xrGetActionStatePose(oxr->session, &getInfo, &poseState),
             "failed to get pose value!");
  }

  /* Left hand */
  XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
  getInfo.next = NULL;
  getInfo.subactionPath = profile.handPaths[0];

  getInfo.action = profile.leftXClickAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get left X click value!");
  controllers_data.left_primary_click = boolState.currentState;
  /*controllers_data.left_primary_onpress = boolState.changedSinceLastSync &&
                                           boolState.currentState;
  controllers_data.left_primary_onrelease = boolState.changedSinceLastSync &&
                                           !boolState.currentState;*/

  getInfo.action = profile.leftXTouchAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get left X touch value!");
  controllers_data.left_primary_touch = boolState.currentState;

  getInfo.action = profile.leftYClickAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get left X click value!");
  controllers_data.left_secondary_click = boolState.currentState;

  getInfo.action = profile.leftYTouchAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get left Y touch value!");
  controllers_data.left_secondary_touch = boolState.currentState;

  /* Right hand */
  getInfo.subactionPath = profile.handPaths[1];

  getInfo.action = profile.rightAClickAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get right A click value!");
  controllers_data.right_primary_click = boolState.currentState;

  getInfo.action = profile.rightATouchAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get right A touch value!");
  controllers_data.right_primary_touch = boolState.currentState;

  getInfo.action = profile.rightBClickAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get right B click value!");
  controllers_data.right_secondary_click = boolState.currentState;

  getInfo.action = profile.rightBTouchAction;
  CHECK_XR(xrGetActionStateBoolean(oxr->session, &getInfo, &boolState),
           "failed to get right B touch value!");
  controllers_data.right_secondary_touch = boolState.currentState;

  /* Retrieve controller spaces */
  XrSpaceLocation spaceLocation[hands];
  bool spaceLocationValid[hands];
  for (int i = 0; i < hands; i++) {
    spaceLocation[i].type = XR_TYPE_SPACE_LOCATION;
    spaceLocation[i].next = NULL;

    CHECK_XR(xrLocateSpace(profile.handSpaces[i],
                           oxr->reference_space,
                           drawInfo->frame_state.predictedDisplayTime,
                           &spaceLocation[i]),
             "failed to locate space!");
    spaceLocationValid[i] =
        //(spaceLocation[i].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
        (spaceLocation[i].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;

    if (spaceLocationValid[i]) {
      if (i == 0) {
        memcpy(&controllers_data.left_pose, &spaceLocation[i].pose, sizeof(XrPosef));
      }
      else {
        memcpy(&controllers_data.right_pose, &spaceLocation[i].pose, sizeof(XrPosef));
      }
    }
  }
}

static void set_xr_controllers_data(OpenXRSessionData *oxr,
                             GHOST_XrDrawInfo *drawInfo,
    GHOST_XrControllersData& controllers_data)
{
  switch (oxr->detectedProfile) {
    case OpenXrProfile::OCULUS_TOUCH:
      fetch_oculus_touch_xr_data(oxr, drawInfo, controllers_data);
      break;
  }
}

void GHOST_XrSession::start(const GHOST_XrSessionBeginInfo *begin_info)
{
  assert(m_context->getInstance() != XR_NULL_HANDLE);
  assert(m_oxr->session == XR_NULL_HANDLE);
  if (m_context->getCustomFuncs().gpu_ctx_bind_fn == nullptr) {
    throw GHOST_XrException(
        "Invalid API usage: No way to bind graphics context to the XR session. Call "
        "GHOST_XrGraphicsContextBindFuncs() with valid parameters before starting the "
        "session (through GHOST_XrSessionStart()).");
  }

  initSystem();

  bindGraphicsContext();
  if (m_gpu_ctx == nullptr) {
    throw GHOST_XrException(
        "Invalid API usage: No graphics context returned through the callback set with "
        "GHOST_XrGraphicsContextBindFuncs(). This is required for session starting (through "
        "GHOST_XrSessionStart()).");
  }

  std::string requirement_str;
  m_gpu_binding = GHOST_XrGraphicsBindingCreateFromType(m_context->getGraphicsBindingType(),
                                                        m_gpu_ctx);
  if (!m_gpu_binding->checkVersionRequirements(
          m_gpu_ctx, m_context->getInstance(), m_oxr->system_id, &requirement_str)) {
    std::ostringstream strstream;
    strstream << "Available graphics context version does not meet the following requirements: "
              << requirement_str;
    throw GHOST_XrException(strstream.str().c_str());
  }
  m_gpu_binding->initFromGhostContext(m_gpu_ctx);

  XrSessionCreateInfo create_info = {};
  create_info.type = XR_TYPE_SESSION_CREATE_INFO;
  create_info.systemId = m_oxr->system_id;
  create_info.next = &m_gpu_binding->oxr_binding;

  CHECK_XR(xrCreateSession(m_context->getInstance(), &create_info, &m_oxr->session),
           "Failed to create VR session. The OpenXR runtime may have additional requirements for "
           "the graphics driver that are not met. Other causes are possible too however.\nTip: "
           "The --debug-xr command line option for Blender might allow the runtime to output "
           "detailed error information to the command line.");

  prepareDrawing();

  create_reference_spaces(m_oxr.get(), &begin_info->base_pose);

  init_xr_controllers(m_oxr.get(), m_context->getInstance());
}

void GHOST_XrSession::requestEnd()
{
  CHECK_XR(xrRequestExitSession(m_oxr->session), "Failed to request the end of the session.");
}

void GHOST_XrSession::beginSession()
{
  XrSessionBeginInfo begin_info = {XR_TYPE_SESSION_BEGIN_INFO};
  begin_info.primaryViewConfigurationType = m_oxr->view_type;
  CHECK_XR(xrBeginSession(m_oxr->session, &begin_info), "Failed to cleanly begin the VR session.");
}

void GHOST_XrSession::endSession()
{
  assert(m_oxr->session != XR_NULL_HANDLE);
  CHECK_XR(xrEndSession(m_oxr->session), "Failed to cleanly end the VR session.");
}

GHOST_XrSession::LifeExpectancy GHOST_XrSession::handleStateChangeEvent(
    const XrEventDataSessionStateChanged *lifecycle,
    bool debug)
{
  m_oxr->session_state = lifecycle->state;

  /* Runtime may send events for apparently destroyed session. Our handle should be NULL then. */
  assert((m_oxr->session == XR_NULL_HANDLE) || (m_oxr->session == lifecycle->session));

  switch (lifecycle->state) {
    case XR_SESSION_STATE_IDLE: 
    {
        if (debug) printf("XR_SESSION_STATE_IDLE.\n");
        break;
    }
    case XR_SESSION_STATE_READY: {
      if (debug) printf("XR_SESSION_STATE_READY.\n");
      beginSession();
      break;
    }
    case XR_SESSION_STATE_SYNCHRONIZED:
    {
        if (debug) printf("XR_SESSION_STATE_SYNCHRONIZED.\n");
        break;
    }
    case XR_SESSION_STATE_VISIBLE:
    {
        if (debug) printf("XR_SESSION_STATE_VISIBLE.\n");
        break;
    }
    case XR_SESSION_STATE_FOCUSED:
    {
        if (debug) printf("XR_SESSION_STATE_FOCUSED.\n");
        break;
    }
    case XR_SESSION_STATE_STOPPING: 
    {
        if (debug) printf("XR_SESSION_STATE_STOPPING.\n");
        endSession();
        break;
    }
    case XR_SESSION_STATE_EXITING:
    {
        if (debug) printf("XR_SESSION_STATE_EXITING.\n");
        return SESSION_DESTROY;
    }
    case XR_SESSION_STATE_LOSS_PENDING:
    {
        if (debug) printf("XR_SESSION_STATE_LOSS_PENDING.\n");
        return SESSION_DESTROY;
    }
    default:
      break;
  }

  return SESSION_KEEP_ALIVE;
}
/** \} */ /* State Management */

/* -------------------------------------------------------------------- */
/** \name Drawing
 *
 * \{ */

void GHOST_XrSession::prepareDrawing()
{
  std::vector<XrViewConfigurationView> view_configs;
  uint32_t view_count;

  CHECK_XR(
      xrEnumerateViewConfigurationViews(
          m_context->getInstance(), m_oxr->system_id, m_oxr->view_type, 0, &view_count, nullptr),
      "Failed to get count of view configurations.");
  view_configs.resize(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  CHECK_XR(xrEnumerateViewConfigurationViews(m_context->getInstance(),
                                             m_oxr->system_id,
                                             m_oxr->view_type,
                                             view_configs.size(),
                                             &view_count,
                                             view_configs.data()),
           "Failed to get count of view configurations.");

  for (const XrViewConfigurationView &view_config : view_configs) {
    m_oxr->swapchains.emplace_back(*m_gpu_binding, m_oxr->session, view_config);
  }

  m_oxr->views.resize(view_count, {XR_TYPE_VIEW});

  m_draw_info = std::unique_ptr<GHOST_XrDrawInfo>(new GHOST_XrDrawInfo());
}

void GHOST_XrSession::beginFrameDrawing()
{
  XrFrameWaitInfo wait_info = {XR_TYPE_FRAME_WAIT_INFO};
  XrFrameBeginInfo begin_info = {XR_TYPE_FRAME_BEGIN_INFO};
  XrFrameState frame_state = {XR_TYPE_FRAME_STATE};

  /* TODO Blocking call. Drawing should run on a separate thread to avoid interferences. */
  CHECK_XR(xrWaitFrame(m_oxr->session, &wait_info, &frame_state),
           "Failed to synchronize frame rates between Blender and the device.");

  CHECK_XR(xrBeginFrame(m_oxr->session, &begin_info),
           "Failed to submit frame rendering start state.");

  m_draw_info->frame_state = frame_state;

  if (m_context->isDebugTimeMode()) {
    m_draw_info->frame_begin_time = std::chrono::high_resolution_clock::now();
  }
}

static void print_debug_timings(GHOST_XrDrawInfo *draw_info)
{
  /** Render time of last 8 frames (in ms) to calculate an average. */
  std::chrono::duration<double, std::milli> duration = std::chrono::high_resolution_clock::now() -
                                                       draw_info->frame_begin_time;
  const double duration_ms = duration.count();
  const int avg_frame_count = 8;
  double avg_ms_tot = 0.0;

  if (draw_info->last_frame_times.size() >= avg_frame_count) {
    draw_info->last_frame_times.pop_front();
    assert(draw_info->last_frame_times.size() == avg_frame_count - 1);
  }
  draw_info->last_frame_times.push_back(duration_ms);
  for (double ms_iter : draw_info->last_frame_times) {
    avg_ms_tot += ms_iter;
  }

  printf("VR frame render time: %.0fms - %.2f FPS (%.2f FPS 8 frames average)\n",
         duration_ms,
         1000.0 / duration_ms,
         1000.0 / (avg_ms_tot / draw_info->last_frame_times.size()));
}

void GHOST_XrSession::endFrameDrawing(std::vector<XrCompositionLayerBaseHeader *> *layers)
{
  XrFrameEndInfo end_info = {XR_TYPE_FRAME_END_INFO};

  end_info.displayTime = m_draw_info->frame_state.predictedDisplayTime;
  end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  end_info.layerCount = layers->size();
  end_info.layers = layers->data();

  CHECK_XR(xrEndFrame(m_oxr->session, &end_info), "Failed to submit rendered frame.");

  if (m_context->isDebugTimeMode()) {
    print_debug_timings(m_draw_info.get());
  }
}

void GHOST_XrSession::draw(void *draw_customdata)
{
  std::vector<XrCompositionLayerProjectionView>
      projection_layer_views; /* Keep alive until xrEndFrame() call! */
  XrCompositionLayerProjection proj_layer;
  std::vector<XrCompositionLayerBaseHeader *> layers;

  beginFrameDrawing();

  if (m_draw_info->frame_state.shouldRender) {
    proj_layer = drawLayer(projection_layer_views, draw_customdata);
    layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&proj_layer));
  }

  endFrameDrawing(&layers);
}

static void copy_openxr_pose_to_ghost_pose(GHOST_XrPose &r_ghost_pose, const XrPosef &oxr_pose)
{
  /* Set and convert to Blender coodinate space. */
  r_ghost_pose.position[0] = oxr_pose.position.x;
  r_ghost_pose.position[1] = oxr_pose.position.y;
  r_ghost_pose.position[2] = oxr_pose.position.z;
  r_ghost_pose.orientation_quat[0] = oxr_pose.orientation.w;
  r_ghost_pose.orientation_quat[1] = oxr_pose.orientation.x;
  r_ghost_pose.orientation_quat[2] = oxr_pose.orientation.y;
  r_ghost_pose.orientation_quat[3] = oxr_pose.orientation.z;
}

static void ghost_xr_draw_view_info_from_view(const XrView &view, GHOST_XrDrawViewInfo &r_info)
{
  /* Set and convert to Blender coodinate space. */
  copy_openxr_pose_to_ghost_pose(r_info.eye_pose, view.pose);

  r_info.fov.angle_left = view.fov.angleLeft;
  r_info.fov.angle_right = view.fov.angleRight;
  r_info.fov.angle_up = view.fov.angleUp;
  r_info.fov.angle_down = view.fov.angleDown;
}

static bool ghost_xr_draw_view_expects_srgb_buffer(const GHOST_XrContext *context)
{
  /* Monado seems to be faulty and doesn't do OETF transform correctly. So expect a SRGB buffer to
   * compensate. You get way too dark rendering without this, it's pretty obvious (even in the
   * default startup scene). */
  return (context->getOpenXRRuntimeID() == OPENXR_RUNTIME_MONADO);
}

static void copy_controllers_data_to_draw_view_info(GHOST_XrDrawViewInfo *draw_view_info, GHOST_XrControllersData const &controllers_data)
{
  copy_openxr_pose_to_ghost_pose(draw_view_info->controllers_data.left_pose, controllers_data.left_pose);
  copy_openxr_pose_to_ghost_pose(draw_view_info->controllers_data.right_pose, controllers_data.right_pose);
  draw_view_info->controllers_data.left_grip_value = controllers_data.left_grip_value;
  draw_view_info->controllers_data.right_grip_value = controllers_data.right_grip_value;

  draw_view_info->controllers_data.left_trigger_value = controllers_data.left_trigger_value;
  draw_view_info->controllers_data.left_trigger_touch = controllers_data.left_trigger_touch;
  draw_view_info->controllers_data.right_trigger_value = controllers_data.right_trigger_value;
  draw_view_info->controllers_data.right_trigger_touch = controllers_data.right_trigger_touch;

  draw_view_info->controllers_data.left_thumbstick_x = controllers_data.left_thumbstick_x;
  draw_view_info->controllers_data.left_thumbstick_y = controllers_data.left_thumbstick_y;
  draw_view_info->controllers_data.left_thumbstick_click = controllers_data.left_thumbstick_click;
  draw_view_info->controllers_data.left_thumbstick_touch = controllers_data.left_thumbstick_touch;

  draw_view_info->controllers_data.right_thumbstick_x = controllers_data.right_thumbstick_x;
  draw_view_info->controllers_data.right_thumbstick_y = controllers_data.right_thumbstick_y;
  draw_view_info->controllers_data.right_thumbstick_click = controllers_data.right_thumbstick_click;
  draw_view_info->controllers_data.right_thumbstick_touch = controllers_data.right_thumbstick_touch;

  draw_view_info->controllers_data.left_primary_click = controllers_data.left_primary_click;
  draw_view_info->controllers_data.left_primary_touch = controllers_data.left_primary_touch;
  draw_view_info->controllers_data.left_secondary_click = controllers_data.left_secondary_click;
  draw_view_info->controllers_data.left_secondary_touch = controllers_data.left_secondary_touch;

  draw_view_info->controllers_data.right_primary_click = controllers_data.right_primary_click;
  draw_view_info->controllers_data.right_primary_touch = controllers_data.right_primary_touch;
  draw_view_info->controllers_data.right_secondary_click = controllers_data.right_secondary_click;
  draw_view_info->controllers_data.right_secondary_touch = controllers_data.right_secondary_touch;
}

void GHOST_XrSession::drawView(GHOST_XrSwapchain &swapchain,
                               XrCompositionLayerProjectionView &r_proj_layer_view,
                               XrSpaceLocation &view_location,
                               XrView &view,
                               GHOST_XrControllersData const& controllers_data,
                               void *draw_customdata)
{
  XrSwapchainImageBaseHeader *swapchain_image = swapchain.acquireDrawableSwapchainImage();
  GHOST_XrDrawViewInfo draw_view_info = {};
  copy_controllers_data_to_draw_view_info(&draw_view_info, controllers_data);

  r_proj_layer_view.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
  r_proj_layer_view.pose = view.pose;
  r_proj_layer_view.fov = view.fov;
  swapchain.updateCompositionLayerProjectViewSubImage(r_proj_layer_view.subImage);

  draw_view_info.expects_srgb_buffer = ghost_xr_draw_view_expects_srgb_buffer(m_context);
  draw_view_info.ofsx = r_proj_layer_view.subImage.imageRect.offset.x;
  draw_view_info.ofsy = r_proj_layer_view.subImage.imageRect.offset.y;
  draw_view_info.width = r_proj_layer_view.subImage.imageRect.extent.width;
  draw_view_info.height = r_proj_layer_view.subImage.imageRect.extent.height;
  copy_openxr_pose_to_ghost_pose(draw_view_info.local_pose, view_location.pose);

  ghost_xr_draw_view_info_from_view(view, draw_view_info);

  /* Draw! */
  m_context->getCustomFuncs().draw_view_fn(&draw_view_info, draw_customdata);
  m_gpu_binding->submitToSwapchainImage(swapchain_image, &draw_view_info);

  swapchain.releaseImage();
}

XrCompositionLayerProjection GHOST_XrSession::drawLayer(
    std::vector<XrCompositionLayerProjectionView> &r_proj_layer_views, void *draw_customdata)
{
  GHOST_XrControllersData controllersData = {};
  set_xr_controllers_data(m_oxr.get(), m_draw_info.get(), controllersData);

  XrViewLocateInfo viewloc_info = {XR_TYPE_VIEW_LOCATE_INFO};
  XrViewState view_state = {XR_TYPE_VIEW_STATE};
  XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  XrSpaceLocation view_location{XR_TYPE_SPACE_LOCATION};
  uint32_t view_count;

  viewloc_info.viewConfigurationType = m_oxr->view_type;
  viewloc_info.displayTime = m_draw_info->frame_state.predictedDisplayTime;
  viewloc_info.space = m_oxr->reference_space;

  CHECK_XR(xrLocateViews(m_oxr->session,
                         &viewloc_info,
                         &view_state,
                         m_oxr->views.size(),
                         &view_count,
                         m_oxr->views.data()),
           "Failed to query frame view and projection state.");
  assert(m_oxr->swapchains.size() == view_count);

  CHECK_XR(
      xrLocateSpace(
          m_oxr->view_space, m_oxr->reference_space, viewloc_info.displayTime, &view_location),
      "Failed to query frame view space");

  r_proj_layer_views.resize(view_count);

  for (uint32_t view_idx = 0; view_idx < view_count; view_idx++) {
    drawView(m_oxr->swapchains[view_idx],
             r_proj_layer_views[view_idx],
             view_location,
             m_oxr->views[view_idx],
             controllersData,
             draw_customdata);
  }

  layer.space = m_oxr->reference_space;
  layer.viewCount = r_proj_layer_views.size();
  layer.views = r_proj_layer_views.data();

  return layer;
}

bool GHOST_XrSession::needsUpsideDownDrawing() const
{
  return m_gpu_binding && m_gpu_binding->needsUpsideDownDrawing(*m_gpu_ctx);
}

/** \} */ /* Drawing */

/* -------------------------------------------------------------------- */
/** \name State Queries
 *
 * \{ */

bool GHOST_XrSession::isRunning() const
{
  if (m_oxr->session == XR_NULL_HANDLE) {
    return false;
  }
  switch (m_oxr->session_state) {
    case XR_SESSION_STATE_READY:
    case XR_SESSION_STATE_SYNCHRONIZED:
    case XR_SESSION_STATE_VISIBLE:
    case XR_SESSION_STATE_FOCUSED:
      return true;
    default:
      return false;
  }
}

/** \} */ /* State Queries */

/* -------------------------------------------------------------------- */
/** \name Graphics Context Injection
 *
 * Sessions need access to Ghost graphics context information. Additionally, this API allows
 * creating contexts on the fly (created on start, destructed on end). For this, callbacks to bind
 * (potentially create) and unbind (potentially destruct) a Ghost graphics context have to be set,
 * which will be called on session start and end respectively.
 *
 * \{ */

void GHOST_XrSession::bindGraphicsContext()
{
  const GHOST_XrCustomFuncs &custom_funcs = m_context->getCustomFuncs();
  assert(custom_funcs.gpu_ctx_bind_fn);

  m_gpu_ctx = static_cast<GHOST_Context *>(custom_funcs.gpu_ctx_bind_fn());
}

void GHOST_XrSession::unbindGraphicsContext()
{
  const GHOST_XrCustomFuncs &custom_funcs = m_context->getCustomFuncs();
  if (custom_funcs.gpu_ctx_unbind_fn) {
    custom_funcs.gpu_ctx_unbind_fn((GHOST_ContextHandle)m_gpu_ctx);
  }
  m_gpu_ctx = nullptr;
}

/** \} */ /* Graphics Context Injection */
