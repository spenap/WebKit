/*
 * Copyright (C) 2025 Igalia, S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "OpenXRInputSource.h"

#if ENABLE(WEBXR) && USE(OPENXR)

#include "OpenXRExtensions.h"
#include "OpenXRUtils.h"
#include <openxr/openxr.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

constexpr auto s_userHandPath { "/user/hand/"_s };
constexpr auto s_inputGripPath { "/input/grip/pose"_s };
constexpr auto s_inputAimPath { "/input/aim/pose"_s };
constexpr auto s_inputPinchPath { "/input/pinch_ext/pose"_s };
constexpr auto s_inputPokePath { "/input/poke_ext/pose"_s };

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(OpenXRInputSource);

std::unique_ptr<OpenXRInputSource> OpenXRInputSource::create(XrInstance instance, XrSession session, PlatformXR::XRHandedness handedness, PlatformXR::InputSourceHandle handle)
{
    auto input = std::unique_ptr<OpenXRInputSource>(new OpenXRInputSource(instance, session, handedness, handle));
    if (XR_FAILED(input->initialize()))
        return nullptr;
    return input;
}

OpenXRInputSource::OpenXRInputSource(XrInstance instance, XrSession session, PlatformXR::XRHandedness handedness, PlatformXR::InputSourceHandle handle)
    : m_instance(instance)
    , m_session(session)
    , m_handedness(handedness)
    , m_handle(handle)
{
}

OpenXRInputSource::~OpenXRInputSource()
{
    if (m_actionSet != XR_NULL_HANDLE)
        xrDestroyActionSet(m_actionSet);
    if (m_gripSpace != XR_NULL_HANDLE)
        xrDestroySpace(m_gripSpace);
    if (m_pointerSpace != XR_NULL_HANDLE)
        xrDestroySpace(m_pointerSpace);
}

XrResult OpenXRInputSource::initialize()
{
    String handednessName = handednessToString(m_handedness);
    m_subactionPathName = makeString(s_userHandPath, handednessName);
    RETURN_RESULT_IF_FAILED(xrStringToPath(m_instance, m_subactionPathName.utf8().data(), &m_subactionPath));

    auto prefix = makeString("input_"_s, handednessName);
    auto actionSetName = makeString(prefix, "_action_set"_s);
    auto createInfo = createOpenXRStruct<XrActionSetCreateInfo, XR_TYPE_ACTION_SET_CREATE_INFO>();
    std::strncpy(createInfo.actionSetName, actionSetName.utf8().data(), XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(createInfo.localizedActionSetName, actionSetName.utf8().data(), XR_MAX_ACTION_SET_NAME_SIZE - 1);

    RETURN_RESULT_IF_FAILED(xrCreateActionSet(m_instance, &createInfo, &m_actionSet));

    RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_POSE_INPUT, makeString(prefix, "_grip"_s), m_gripAction));
    RETURN_RESULT_IF_FAILED(createActionSpace(m_gripAction, m_gripSpace));
    RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_POSE_INPUT, makeString(prefix, "_pointer"_s), m_pointerAction));
    RETURN_RESULT_IF_FAILED(createActionSpace(m_pointerAction, m_pointerSpace));

#if defined(XR_EXT_hand_interaction)
    if (OpenXRExtensions::singleton().isExtensionSupported(XR_EXT_HAND_INTERACTION_EXTENSION_NAME)) {
        RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_POSE_INPUT, makeString(prefix, "_pinch_ext"_s), m_pinchPoseAction));
        RETURN_RESULT_IF_FAILED(createActionSpace(m_pinchPoseAction, m_pinchSpace), m_instance);
        RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_POSE_INPUT, makeString(prefix, "_poke_ext"_s), m_pokePoseAction));
        RETURN_RESULT_IF_FAILED(createActionSpace(m_pokePoseAction, m_pokeSpace), m_instance);
    }
#endif

    for (auto buttonType : openXRButtonTypes) {
        OpenXRButtonActions actions;
        createButtonActions(buttonType, prefix, actions);
        m_buttonActions.add(buttonType, actions);
    }

    for (auto axisType : openXRAxisTypes) {
        XrAction axisAction = XR_NULL_HANDLE;
        auto name = makeString(prefix, "_axis_"_s, axisTypetoString(axisType));
        RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_VECTOR2F_INPUT, name, axisAction), false);
        m_axisActions.add(axisType, axisAction);
    }

    return XR_SUCCESS;
}

XrResult OpenXRInputSource::suggestBindings(SuggestedBindings& bindings) const
{
    auto isBindingForHand = [hand = m_handedness](OpenXRHandFlags buttonHand) {
        switch (buttonHand) {
        case OpenXRHandFlags::Both:
            return true;
        case OpenXRHandFlags::Left:
            return hand == PlatformXR::XRHandedness::Left;
        case OpenXRHandFlags::Right:
            return hand == PlatformXR::XRHandedness::Right;
        default:
            ASSERT_NOT_REACHED_WITH_MESSAGE("Unknown OpenXRHandFlags");
            return false;
        }
    };

    for (const auto& profile : openXRInteractionProfiles) {
        CHECK_XRCMD(createBinding(profile.path, m_gripAction, makeString(m_subactionPathName, s_inputGripPath), bindings));
        CHECK_XRCMD(createBinding(profile.path, m_pointerAction, makeString(m_subactionPathName, s_inputAimPath), bindings));

#if defined(XR_EXT_hand_interaction)
        if (OpenXRExtensions::singleton().isExtensionSupported(XR_EXT_HAND_INTERACTION_EXTENSION_NAME)) {
            RETURN_RESULT_IF_FAILED(createBinding(profile.path, m_pinchPoseAction, makeString(m_subactionPathName, s_inputPinchPath), bindings));
            RETURN_RESULT_IF_FAILED(createBinding(profile.path, m_pokePoseAction, makeString(m_subactionPathName, s_inputPokePath), bindings));
        }
#endif

        for (const auto& button : profile.buttons) {
            if (!isBindingForHand(button.hand))
                continue;

            const auto& actions = m_buttonActions.get(button.type);
            if (button.flags & OpenXRButtonFlags::Click) {
                ASSERT(actions.press != XR_NULL_HANDLE);
                CHECK_XRCMD(createBinding(profile.path, actions.press, makeString(m_subactionPathName, button.path, s_pathActionClick), bindings));
            }
            if (button.flags & OpenXRButtonFlags::Touch) {
                ASSERT(actions.touch != XR_NULL_HANDLE);
                CHECK_XRCMD(createBinding(profile.path, actions.touch, makeString(m_subactionPathName, button.path, s_pathActionTouch), bindings));
            }
            if (button.flags & OpenXRButtonFlags::Value) {
                ASSERT(actions.value != XR_NULL_HANDLE);
                CHECK_XRCMD(createBinding(profile.path, actions.value, makeString(m_subactionPathName, button.path, s_pathActionValue), bindings));
            }
        }

        for (const auto& axis : profile.axes) {
            auto action = m_axisActions.get(axis.type);
            ASSERT(action != XR_NULL_HANDLE);
            CHECK_XRCMD(createBinding(profile.path, action, makeString(m_subactionPathName, unsafeSpan(axis.path)), bindings));
        }
    }

    return XR_SUCCESS;
}

std::optional<PlatformXR::FrameData::InputSource> OpenXRInputSource::collectInputSource(XrSpace localSpace, const XrFrameState& frameState) const
{
    PlatformXR::FrameData::InputSource data;
    data.handedness = m_handedness;
    data.handle = m_handle;
    data.targetRayMode = PlatformXR::XRTargetRayMode::TrackedPointer;
    data.profiles = m_profiles;

    getPose(m_pointerSpace, localSpace, frameState, data.pointerOrigin);
    PlatformXR::FrameData::InputSourcePose gripPose;
    if (XR_SUCCEEDED(getPose(m_gripSpace, localSpace, frameState, gripPose)))
        data.gripOrigin = gripPose;

    Vector<std::optional<PlatformXR::FrameData::InputSourceButton>, openXRButtonTypes.size()> buttons;
    for (auto type : openXRButtonTypes) {
        if (auto button = collectButton(type); button.has_value())
            buttons.append(button);
    }

    // Trigger is mandatory in xr-standard mapping.
    if (buttons.isEmpty() || !buttons.first().has_value())
        return std::nullopt;

    for (size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i]) {
            data.buttons.append(*buttons[i]);
            continue;
        }
        // Add placeholder if there are more valid buttons in the list.
        for (size_t j = i + 1; j < buttons.size(); ++j) {
            if (buttons[j]) {
                data.buttons.append({ });
                break;
            }
        }
    }

    Vector<std::optional<XrVector2f>, openXRAxisTypes.size()> axes;
    for (auto type : openXRAxisTypes)
        axes.append(collectAxis(type));

    for (size_t i = 0; i < axes.size(); ++i) {
        if (axes[i]) {
            data.axes.append(axes[i]->x);
            data.axes.append(axes[i]->y);
            continue;
        }
        // Add placeholder if there are more valid axes in the list.
        for (size_t j = i + 1; j < buttons.size(); ++j) {
            if (axes[j]) {
                data.axes.append(0.0f);
                data.axes.append(0.0f);
                break;
            }
        }
    }

    return data;
}

XrResult OpenXRInputSource::updateInteractionProfile()
{
    auto state = createOpenXRStruct<XrInteractionProfileState, XR_TYPE_INTERACTION_PROFILE_STATE>();
    RETURN_RESULT_IF_FAILED(xrGetCurrentInteractionProfile(m_session, m_subactionPath, &state));

    constexpr uint32_t bufferSize = 100;
    char buffer[bufferSize];
    uint32_t writtenCount = 0;
    RETURN_RESULT_IF_FAILED(xrPathToString(m_instance, state.interactionProfile, bufferSize, &writtenCount, buffer));

    m_profiles.clear();
    for (auto& profile : openXRInteractionProfiles) {
        if (equalSpans(profile.path.span(), unsafeSpan(buffer))) {
            LOG(XR, "Input source %s using interaction profile %s", m_subactionPathName.utf8().data(), profile.path.span().data());
            for (const auto& id : profile.profileIds)
                m_profiles.append(String::fromUTF8(id));
            break;
        }
    }

    return XR_SUCCESS;
}


XrResult OpenXRInputSource::createActionSpace(XrAction action, XrSpace& space) const
{
    auto createInfo =  createOpenXRStruct<XrActionSpaceCreateInfo, XR_TYPE_ACTION_SPACE_CREATE_INFO>();
    createInfo.action = action;
    createInfo.subactionPath = m_subactionPath;
    createInfo.poseInActionSpace = { };
    createInfo.poseInActionSpace.orientation.w = 1.0f;

    return xrCreateActionSpace(m_session, &createInfo, &space);
}

XrResult OpenXRInputSource::createAction(XrActionType actionType, const String& name, XrAction& action) const
{
    auto createInfo =  createOpenXRStruct<XrActionCreateInfo, XR_TYPE_ACTION_CREATE_INFO>();
    createInfo.actionType = actionType;
    createInfo.countSubactionPaths = 1;
    createInfo.subactionPaths = &m_subactionPath;
    std::strncpy(createInfo.actionName, name.utf8().data(), XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(createInfo.localizedActionName, name.utf8().data(), XR_MAX_ACTION_SET_NAME_SIZE - 1);

    return xrCreateAction(m_actionSet, &createInfo, &action);
}

XrResult OpenXRInputSource::createButtonActions(OpenXRButtonType type, const String& prefix, OpenXRButtonActions& actions) const
{
    auto name = makeString(prefix, "_button_"_s, buttonTypeToString(type));

    RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, makeString(name, "_press"_s), actions.press));
    RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, makeString(name, "_touch"_s), actions.touch));
    RETURN_RESULT_IF_FAILED(createAction(XR_ACTION_TYPE_FLOAT_INPUT, makeString(name, "_value"_s), actions.value));

    return XR_SUCCESS;
}

XrResult OpenXRInputSource::createBinding(const char* profilePath, XrAction action, const String& bindingPath, SuggestedBindings& bindings) const
{
    ASSERT(profilePath != XR_NULL_PATH);
    ASSERT(action != XR_NULL_HANDLE);
    ASSERT(!bindingPath.isEmpty());

    XrPath path = XR_NULL_PATH;
    RETURN_RESULT_IF_FAILED(xrStringToPath(m_instance, bindingPath.utf8().data(), &path));

    XrActionSuggestedBinding binding { action, path };
    if (auto it = bindings.find(profilePath); it != bindings.end())
        it->value.append(binding);
    else
        bindings.add(profilePath, Vector<XrActionSuggestedBinding> { binding });

    return XR_SUCCESS;
}

XrResult OpenXRInputSource::getPose(XrSpace space, XrSpace baseSpace, const XrFrameState& frameState, PlatformXR::FrameData::InputSourcePose& pose) const
{
    auto location = createOpenXRStruct<XrSpaceLocation, XR_TYPE_SPACE_LOCATION>();
    RETURN_RESULT_IF_FAILED(xrLocateSpace(space, baseSpace, frameState.predictedDisplayTime, &location), m_instance);

    if (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
        pose.pose = XrPosefToPose(location.pose);
    pose.isPositionEmulated = !(location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT);

    return XR_SUCCESS;
}

std::optional<PlatformXR::FrameData::InputSourceButton> OpenXRInputSource::collectButton(OpenXRButtonType buttonType) const
{
    auto it = m_buttonActions.find(buttonType);
    if (it == m_buttonActions.end())
        return std::nullopt;

    PlatformXR::FrameData::InputSourceButton result;
    bool hasValue = false;
    auto& actions = it->value;

    auto queryActionState = [this, &hasValue](XrAction action, auto& value, auto defaultValue) {
        if (action != XR_NULL_HANDLE && XR_SUCCEEDED(this->getActionState(action, &value)))
            hasValue = true;
        else
            value = defaultValue;
    };

    queryActionState(actions.press, result.pressed, false);
    queryActionState(actions.touch, result.touched, result.pressed);
    queryActionState(actions.value, result.pressedValue, result.pressed ? 1.0 : 0.0);
    // When using hand interaction profiles, press and touch are not valid paths, so use the value for everything.
    if (result.pressedValue > 0.0f)
        result.pressed = result.touched = true;

    return hasValue ?  std::make_optional(result) : std::nullopt;
}

std::optional<XrVector2f> OpenXRInputSource::collectAxis(OpenXRAxisType axisType) const
{
    auto it = m_axisActions.find(axisType);
    if (it == m_axisActions.end())
        return std::nullopt;

    XrVector2f axis;
    if (XR_FAILED(getActionState(it->value, &axis)))
        return std::nullopt;

    return axis;
}

XrResult OpenXRInputSource::getActionState(XrAction action, bool* value) const
{
    ASSERT(value);
    ASSERT(action != XR_NULL_HANDLE);

    auto state = createOpenXRStruct<XrActionStateBoolean, XR_TYPE_ACTION_STATE_BOOLEAN>();
    auto info = createOpenXRStruct<XrActionStateGetInfo, XR_TYPE_ACTION_STATE_GET_INFO>();
    info.action = action;

    RETURN_RESULT_IF_FAILED(xrGetActionStateBoolean(m_session, &info, &state));
    *value = state.currentState;

    return XR_SUCCESS;
}

XrResult OpenXRInputSource::getActionState(XrAction action, float* value) const
{
    ASSERT(value);
    ASSERT(action != XR_NULL_HANDLE);

    auto state = createOpenXRStruct<XrActionStateFloat, XR_TYPE_ACTION_STATE_FLOAT>();
    auto info = createOpenXRStruct<XrActionStateGetInfo, XR_TYPE_ACTION_STATE_GET_INFO>();
    info.action = action;

    RETURN_RESULT_IF_FAILED(xrGetActionStateFloat(m_session, &info, &state));
    *value = state.currentState;

    return XR_SUCCESS;
}

XrResult OpenXRInputSource::getActionState(XrAction action, XrVector2f* value) const
{
    ASSERT(value);
    ASSERT(action != XR_NULL_HANDLE);

    auto state = createOpenXRStruct<XrActionStateVector2f, XR_TYPE_ACTION_STATE_VECTOR2F>();
    auto info = createOpenXRStruct<XrActionStateGetInfo, XR_TYPE_ACTION_STATE_GET_INFO>();
    info.action = action;

    RETURN_RESULT_IF_FAILED(xrGetActionStateVector2f(m_session, &info, &state));
    *value = state.currentState;

    return XR_SUCCESS;
}

} // namespace WebKit

#endif // ENABLE(WEBXR) && USE(OPENXR)
