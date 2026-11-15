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

#pragma once

#include <algorithm>
#include <optional>
#include <wpe/wpe-platform.h>
#include <wtf/Vector.h>

namespace WPE {

namespace DRM {

// The damage of a frame is relative to the previous frame. A buffer that last got frame N needs the damage
// of every frame after N; when that is not known, it needs everything.
class DamageHistory {
public:
    static constexpr size_t maximumFrames = 4;

    unsigned frameNumber() const { return m_frameNumber; }

    void recordFrame(std::optional<Vector<WPERectangle>>&& damage)
    {
        m_frameNumber++;
        m_frames.append(WTF::move(damage));
        if (m_frames.size() > maximumFrames)
            m_frames.removeAt(0);
    }

    Vector<WPERectangle> regionSince(unsigned lastFrameNumber, int width, int height) const
    {
        Vector<WPERectangle> fullFrame { { 0, 0, width, height } };
        size_t age = m_frameNumber - lastFrameNumber;
        if (!lastFrameNumber || age > m_frames.size())
            return fullFrame;

        Vector<WPERectangle> region;
        uint64_t damagedArea = 0;
        for (const auto& damage : m_frames.span().last(age)) {
            if (!damage)
                return fullFrame;

            for (const auto& rect : *damage) {
                int x = std::clamp(rect.x, 0, width);
                int y = std::clamp(rect.y, 0, height);
                int right = std::clamp<int64_t>(static_cast<int64_t>(rect.x) + rect.width, x, width);
                int bottom = std::clamp<int64_t>(static_cast<int64_t>(rect.y) + rect.height, y, height);
                if (x == right || y == bottom)
                    continue;
                region.append({ x, y, right - x, bottom - y });
                damagedArea += static_cast<uint64_t>(right - x) * (bottom - y);
            }
        }

        if (damagedArea * 2 >= static_cast<uint64_t>(width) * height)
            return fullFrame;
        return region;
    }

private:
    unsigned m_frameNumber { 0 };
    Vector<std::optional<Vector<WPERectangle>>> m_frames;
};

} // namespace DRM

} // namespace WPE
