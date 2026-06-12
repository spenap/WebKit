/*
 * Copyright (C) 2026 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

// Minimal mock OpenXR runtime for WebKit API tests.
//
// It is loaded in-process by the real OpenXR loader (selected via XR_RUNTIME_JSON),
// so it exercises the exact production code path (real loader, real dispatch, real
// xr* calls inside libWebKit) and only swaps what sits *behind* the loader.
//
// Scope: just enough of the no-session device-enumeration path
// (getPrimaryDeviceInfo -> initializeDevice -> createInstance/initializeSystem/
// collectViewConfigurations/initializeBlendModes) for navigator.xr.isSessionSupported()
// to resolve, plus instance create/destroy bookkeeping. Session/frame-loop entry
// points are intentionally unimplemented (returned as unsupported) and can be added
// later for a session-capable runtime + teardown-ordering oracle.
//
// Instance lifecycle events are appended to the file named by the
// WEBKIT_MOCK_OPENXR_EVENTS environment variable so the test can observe whether
// xrDestroyInstance was actually called.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

namespace {

// A single fake dispatchable instance handle. XrInstance is a pointer-like handle;
// any stable non-null value works since WebKit only checks against XR_NULL_HANDLE.
int g_mockInstanceStorage = 0;
XrInstance mockInstanceHandle() { return reinterpret_cast<XrInstance>(&g_mockInstanceStorage); }

constexpr XrSystemId kMockSystemId = 1;

int g_liveInstances = 0;

void logEvent(const char* event)
{
    const char* path = getenv("WEBKIT_MOCK_OPENXR_EVENTS");
    if (!path || !*path)
        return;
    if (FILE* file = fopen(path, "a")) {
        fprintf(file, "%s\n", event);
        fclose(file);
    }
}

// Implements the OpenXR two-call enumeration idiom: with capacityInput == 0 the
// runtime reports the required count; otherwise it fills up to capacityInput.
template<typename T>
XrResult enumerate(const T* values, uint32_t count, uint32_t capacityInput, uint32_t* countOutput, T* buffer)
{
    if (countOutput)
        *countOutput = count;
    if (!capacityInput)
        return XR_SUCCESS;
    if (capacityInput < count)
        return XR_ERROR_SIZE_INSUFFICIENT;
    for (uint32_t i = 0; i < count; ++i)
        buffer[i] = values[i];
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrEnumerateInstanceExtensionProperties(const char*, uint32_t capacityInput, uint32_t* propertyCountOutput, XrExtensionProperties* properties)
{
    // WebKit requests XR_KHR_opengl_es_enable unconditionally on the GLES build, and the
    // loader validates requested extensions against this list *before* dispatching
    // xrCreateInstance, so it must be advertised. All other extensions WebKit uses are
    // gated on isExtensionSupported(), so leaving them out keeps them unrequested.
    static const char* const kExtensions[] = { "XR_KHR_opengl_es_enable" };
    const uint32_t count = 1;

    if (propertyCountOutput)
        *propertyCountOutput = count;
    if (!capacityInput)
        return XR_SUCCESS;
    if (capacityInput < count)
        return XR_ERROR_SIZE_INSUFFICIENT;

    for (uint32_t i = 0; i < count; ++i) {
        properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
        properties[i].next = nullptr;
        std::strncpy(properties[i].extensionName, kExtensions[i], XR_MAX_EXTENSION_NAME_SIZE - 1);
        properties[i].extensionName[XR_MAX_EXTENSION_NAME_SIZE - 1] = '\0';
        properties[i].extensionVersion = 1;
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrCreateInstance(const XrInstanceCreateInfo*, XrInstance* instance)
{
    // Accept any requested extensions (e.g. XR_KHR_opengl_es_enable); the mock ignores them.
    if (!instance)
        return XR_ERROR_VALIDATION_FAILURE;
    *instance = mockInstanceHandle();
    ++g_liveInstances;
    logEvent("instance-created");
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrDestroyInstance(XrInstance)
{
    if (g_liveInstances <= 0) {
        // Ordering oracle: destroying with no live instance is a double-destroy.
        logEvent("instance-double-destroy");
        return XR_ERROR_HANDLE_INVALID;
    }
    --g_liveInstances;
    logEvent("instance-destroyed");
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrGetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* systemId)
{
    if (!systemId)
        return XR_ERROR_VALIDATION_FAILURE;
    *systemId = kMockSystemId;
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrGetSystemProperties(XrInstance, XrSystemId, XrSystemProperties* properties)
{
    if (!properties)
        return XR_ERROR_VALIDATION_FAILURE;
    properties->vendorId = 0xCAFE;
    std::strncpy(properties->systemName, "WebKit Mock OpenXR Runtime", XR_MAX_SYSTEM_NAME_SIZE - 1);
    properties->systemName[XR_MAX_SYSTEM_NAME_SIZE - 1] = '\0';
    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;
    properties->graphicsProperties.maxLayerCount = 1;
    properties->graphicsProperties.maxSwapchainImageWidth = 4096;
    properties->graphicsProperties.maxSwapchainImageHeight = 4096;
    // Leave properties->next untouched: WebKit chains XrSystemHandTrackingPropertiesEXT
    // and pre-sets supportsHandTracking = XR_FALSE.
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrEnumerateViewConfigurations(XrInstance, XrSystemId, uint32_t capacityInput, uint32_t* countOutput, XrViewConfigurationType* buffer)
{
    static const XrViewConfigurationType configs[] = { XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO };
    return enumerate(configs, 1, capacityInput, countOutput, buffer);
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrEnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacityInput, uint32_t* countOutput, XrViewConfigurationView* buffer)
{
    // A stereo configuration: two identical views.
    if (countOutput)
        *countOutput = 2;
    if (!capacityInput)
        return XR_SUCCESS;
    if (capacityInput < 2)
        return XR_ERROR_SIZE_INSUFFICIENT;
    for (uint32_t i = 0; i < 2; ++i) {
        buffer[i].recommendedImageRectWidth = 1024;
        buffer[i].maxImageRectWidth = 2048;
        buffer[i].recommendedImageRectHeight = 1024;
        buffer[i].maxImageRectHeight = 2048;
        buffer[i].recommendedSwapchainSampleCount = 1;
        buffer[i].maxSwapchainSampleCount = 1;
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrEnumerateEnvironmentBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacityInput, uint32_t* countOutput, XrEnvironmentBlendMode* buffer)
{
    static const XrEnvironmentBlendMode modes[] = { XR_ENVIRONMENT_BLEND_MODE_OPAQUE };
    return enumerate(modes, 1, capacityInput, countOutput, buffer);
}

// Resolved by loadMethods() under XR_USE_GRAPHICS_API_OPENGL_ES; must be non-null but
// is only invoked during session creation, which the no-session test never reaches.
// Declared with a generic last parameter to avoid pulling in EGL/GLES platform headers.
XRAPI_ATTR XrResult XRAPI_CALL mock_xrGetOpenGLESGraphicsRequirementsKHR(XrInstance, XrSystemId, void*)
{
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL mock_xrGetInstanceProcAddr(XrInstance, const char* name, PFN_xrVoidFunction* function)
{
    if (!name || !function)
        return XR_ERROR_VALIDATION_FAILURE;

    auto bind = [&](PFN_xrVoidFunction fn) {
        *function = fn;
        return XR_SUCCESS;
    };

    if (!std::strcmp(name, "xrGetInstanceProcAddr"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrGetInstanceProcAddr));
    if (!std::strcmp(name, "xrEnumerateInstanceExtensionProperties"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrEnumerateInstanceExtensionProperties));
    if (!std::strcmp(name, "xrCreateInstance"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrCreateInstance));
    if (!std::strcmp(name, "xrDestroyInstance"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrDestroyInstance));
    if (!std::strcmp(name, "xrGetSystem"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrGetSystem));
    if (!std::strcmp(name, "xrGetSystemProperties"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrGetSystemProperties));
    if (!std::strcmp(name, "xrEnumerateViewConfigurations"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrEnumerateViewConfigurations));
    if (!std::strcmp(name, "xrEnumerateViewConfigurationViews"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrEnumerateViewConfigurationViews));
    if (!std::strcmp(name, "xrEnumerateEnvironmentBlendModes"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrEnumerateEnvironmentBlendModes));
    if (!std::strcmp(name, "xrGetOpenGLESGraphicsRequirementsKHR"))
        return bind(reinterpret_cast<PFN_xrVoidFunction>(mock_xrGetOpenGLESGraphicsRequirementsKHR));

    // Everything else (session, frame loop, spaces, input, ...) is unimplemented.
    *function = nullptr;
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

} // namespace

// The OpenXR loader resolves this entry point with dlsym(), so it must have default
// visibility even though WebKit builds with -fvisibility=hidden.
extern "C" __attribute__((visibility("default"))) XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* runtimeRequest)
{
    if (!loaderInfo || !runtimeRequest)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO
        || runtimeRequest->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (XR_CURRENT_LOADER_RUNTIME_VERSION < loaderInfo->minInterfaceVersion
        || XR_CURRENT_LOADER_RUNTIME_VERSION > loaderInfo->maxInterfaceVersion)
        return XR_ERROR_INITIALIZATION_FAILED;

    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtimeRequest->runtimeApiVersion = XR_CURRENT_API_VERSION;
    runtimeRequest->getInstanceProcAddr = mock_xrGetInstanceProcAddr;
    return XR_SUCCESS;
}
