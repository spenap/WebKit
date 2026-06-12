/*
 * Copyright (C) 2026 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "WebViewTest.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <wtf/glib/GUniquePtr.h>

// This test exercises the UIProcess OpenXR instance lifecycle against an in-process
// mock OpenXR runtime (Tools/TestWebKitAPI/openxr-mock), selected via XR_RUNTIME_JSON
// so the production loader/dispatch path is unchanged. It is independent of Monado.
//
// Regression target: an XrInstance created by device enumeration
// (navigator.xr.isSessionSupported()) with no session must be released when the XR
// system is torn down. Before the fix, PlatformXRSystem::invalidate(State) returned
// early in the Idle state and the instance leaked (the coordinator is a NeverDestroyed
// singleton, so its destructor never runs either).

static char* gEventsFilePath;

static void relaxDMABufRequirement(WebKitSettings* settings)
{
    // Lets createGLDisplay() accept a surfaceless EGL display without dma-buf export,
    // so device enumeration (and thus instance creation) succeeds in CI.
    g_autoptr(WebKitFeatureList) featureList = webkit_settings_get_development_features();
    WebKitFeature* feature = webkit_feature_list_find(featureList, "OpenXRDMABufRelaxedForTesting");
    g_assert_nonnull(feature);
    webkit_settings_set_feature_enabled(settings, feature, true);
}

static GUniquePtr<char> readEvents(gsize& length)
{
    char* contents = nullptr;
    length = 0;
    g_file_get_contents(gEventsFilePath, &contents, &length, nullptr);
    return GUniquePtr<char>(contents);
}

static bool eventsContain(const char* needle)
{
    gsize length = 0;
    GUniquePtr<char> events = readEvents(length);
    return events && g_strstr_len(events.get(), length, needle);
}

class WebXRInstanceLifecycleTest : public WebViewTest {
public:
    MAKE_GLIB_TEST_FIXTURE(WebXRInstanceLifecycleTest);

    WebXRInstanceLifecycleTest()
    {
        relaxDMABufRequirement(webkit_web_view_get_settings(m_webView.get()));
    }

    // Tearing down the page (here via web-process termination) runs
    // WebPageProxy::resetState, which calls PlatformXRSystem::invalidate(State). The base
    // WebViewTest fixture asserts on unexpected web-process termination, so flag it.
    void terminateWebProcess()
    {
        m_expectedWebProcessCrash = true;
        webkit_web_view_terminate_web_process(m_webView.get());
    }

    // The instance is destroyed synchronously in the UIProcess during WebPageProxy::resetState;
    // poll briefly to be robust against the ordering of the termination signal.
    bool waitUntilEvent(const char* needle)
    {
        for (int i = 0; i < 200 && !eventsContain(needle); ++i) {
            g_main_context_iteration(nullptr, FALSE);
            g_usleep(5000); // 5 ms; 1 s total budget.
        }
        return eventsContain(needle);
    }
};

static void testOpenXRInstanceReleasedOnTeardown(WebXRInstanceLifecycleTest* test, gconstpointer)
{
    test->showInWindow();

    // Loading from a secure origin so WebXR is exposed.
    test->loadHtml("<html><body></body></html>", "https://foo.com/");
    test->waitUntilLoadFinished();

    // Pure device enumeration: creates an OpenXR instance in the UIProcess but starts no
    // session, leaving PlatformXRSystem in the Idle state.
    test->runJavaScriptAndWaitUntilFinished(
        "navigator.xr.isSessionSupported('immersive-vr')"
        "  .then(s => { document.title = 'done:' + s; })"
        "  .catch(e => { document.title = 'error:' + e; });", nullptr);
    test->waitUntilTitleChanged();

    g_assert_true(eventsContain("instance-created"));
    g_assert_false(eventsContain("instance-destroyed"));

    // With no active session, invalidate(State) must release the lingering instance.
    // waitUntilEvent() pumps the main loop, which processes the termination and runs
    // the UIProcess-side reset synchronously.
    test->terminateWebProcess();

    g_assert_true(test->waitUntilEvent("instance-destroyed"));
    g_assert_false(eventsContain("instance-double-destroy"));
}

void beforeAll()
{
    // Select the in-process mock OpenXR runtime for this process before any OpenXR use,
    // and give the mock a private file to record instance create/destroy events into.
    g_setenv("XR_RUNTIME_JSON", MOCK_OPENXR_RUNTIME_JSON, TRUE);

    GUniquePtr<char> eventsTemplate(g_build_filename(g_get_tmp_dir(), "wk-mock-openxr-events-XXXXXX", nullptr));
    int fd = g_mkstemp(eventsTemplate.get());
    g_assert_cmpint(fd, !=, -1);
    close(fd);
    gEventsFilePath = g_strdup(eventsTemplate.get());
    g_setenv("WEBKIT_MOCK_OPENXR_EVENTS", gEventsFilePath, TRUE);

    WebXRInstanceLifecycleTest::add("OpenXR", "instance-released-on-teardown", testOpenXRInstanceReleasedOnTeardown);
}

void afterAll()
{
    if (gEventsFilePath) {
        g_unlink(gEventsFilePath);
        g_free(gEventsFilePath);
        gEventsFilePath = nullptr;
    }
}
