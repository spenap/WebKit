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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <wpe/drm/WPEDRMDamageHistory.h>

namespace TestWebKitAPI {

using WPE::DRM::DamageHistory;

static constexpr int width = 100;
static constexpr int height = 100;

static const Vector<WPERectangle> fullFrame { { 0, 0, width, height } };

TEST(WPEDRMDamageHistory, NewBufferGetsFullFrame)
{
    DamageHistory history;
    history.recordFrame(Vector<WPERectangle> { { 10, 10, 5, 5 } });
    EXPECT_EQ(history.regionSince(0, width, height), fullFrame);
}

TEST(WPEDRMDamageHistory, DamageSinceLastCopy)
{
    DamageHistory history;
    history.recordFrame(Vector<WPERectangle> { { 0, 0, 10, 10 } });
    unsigned copiedFrame = history.frameNumber();
    history.recordFrame(Vector<WPERectangle> { { 10, 10, 5, 5 } });
    history.recordFrame(Vector<WPERectangle> { { 20, 20, 5, 5 } });

    Vector<WPERectangle> expected { { 10, 10, 5, 5 }, { 20, 20, 5, 5 } };
    EXPECT_EQ(history.regionSince(copiedFrame, width, height), expected);
    expected = { { 20, 20, 5, 5 } };
    EXPECT_EQ(history.regionSince(history.frameNumber() - 1, width, height), expected);
}

TEST(WPEDRMDamageHistory, UnknownDamageGivesFullFrame)
{
    DamageHistory history;
    history.recordFrame(Vector<WPERectangle> { { 0, 0, 10, 10 } });
    unsigned copiedFrame = history.frameNumber();
    history.recordFrame(std::nullopt);
    history.recordFrame(Vector<WPERectangle> { { 20, 20, 5, 5 } });
    EXPECT_EQ(history.regionSince(copiedFrame, width, height), fullFrame);
}

TEST(WPEDRMDamageHistory, TooOldGivesFullFrame)
{
    DamageHistory history;
    history.recordFrame(Vector<WPERectangle> { { 0, 0, 1, 1 } });
    unsigned copiedFrame = history.frameNumber();
    for (size_t i = 0; i < DamageHistory::maximumFrames + 1; ++i)
        history.recordFrame(Vector<WPERectangle> { { 0, 0, 1, 1 } });
    EXPECT_EQ(history.regionSince(copiedFrame, width, height), fullFrame);
}

TEST(WPEDRMDamageHistory, HalfFrameOrMoreGivesFullFrame)
{
    DamageHistory history;
    history.recordFrame(Vector<WPERectangle> { { 0, 0, 1, 1 } });
    unsigned copiedFrame = history.frameNumber();
    history.recordFrame(Vector<WPERectangle> { { 0, 0, width, height / 2 } });
    EXPECT_EQ(history.regionSince(copiedFrame, width, height), fullFrame);
}

TEST(WPEDRMDamageHistory, DamageIsClippedToTheBuffer)
{
    DamageHistory history;
    history.recordFrame(Vector<WPERectangle> { { 0, 0, 1, 1 } });
    unsigned copiedFrame = history.frameNumber();
    history.recordFrame(Vector<WPERectangle> { { -5, -5, 10, 10 }, { 95, 90, 20, 5 }, { 200, 200, 5, 5 }, { 30, 30, 0, 10 } });

    Vector<WPERectangle> expected { { 0, 0, 5, 5 }, { 95, 90, 5, 5 } };
    EXPECT_EQ(history.regionSince(copiedFrame, width, height), expected);
}

} // namespace TestWebKitAPI
