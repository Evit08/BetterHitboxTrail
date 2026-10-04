#pragma once

#include <Geode/Geode.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace hitboxtrail::trailgeometry
{
    inline constexpr int kCircleSegments = 24;

    inline cocos2d::CCRect insetRect(cocos2d::CCRect rect, float inset)
    {
        inset = std::min(inset, std::min(rect.size.width, rect.size.height) / 2.f);
        rect.origin.x += inset;
        rect.origin.y += inset;
        rect.size.width -= inset * 2.f;
        rect.size.height -= inset * 2.f;
        return rect;
    }

    inline cocos2d::CCPoint rotatePointAround(cocos2d::CCPoint point, cocos2d::CCPoint centre,
                                              float cosine, float sine)
    {
        auto x = point.x - centre.x;
        auto y = point.y - centre.y;
        return cocos2d::CCPointMake(
            centre.x + x * cosine - y * sine,
            centre.y + x * sine + y * cosine);
    }

    inline std::array<cocos2d::CCPoint, kCircleSegments + 1> const &unitCircle()
    {
        static auto const table = []
        {
            std::array<cocos2d::CCPoint, kCircleSegments + 1> t{};
            constexpr float kTau = 2.f * std::numbers::pi_v<float>;
            for (int i = 0; i <= kCircleSegments; ++i)
            {
                auto angle = kTau * static_cast<float>(i) / kCircleSegments;
                t[i] = cocos2d::CCPointMake(std::cos(angle), std::sin(angle));
            }
            return t;
        }();
        return table;
    }

    template <class Emit>
    void appendRect(Emit &&emit, cocos2d::CCRect rect, float thickness,
                    cocos2d::ccColor4F fill, cocos2d::ccColor4F outline,
                    float rotation = 0.f)
    {
        if (rect.size.width <= 0.f || rect.size.height <= 0.f)
            return;
        if (fill.a <= 0.f && outline.a <= 0.f)
            return;

        thickness = std::clamp(thickness, 0.f,
                               std::min(rect.size.width, rect.size.height) / 2.f);
        cocos2d::CCPoint verts[4] = {
            {rect.getMinX(), rect.getMinY()},
            {rect.getMaxX(), rect.getMinY()},
            {rect.getMaxX(), rect.getMaxY()},
            {rect.getMinX(), rect.getMaxY()},
        };
        auto centre = cocos2d::CCPointMake(rect.getMidX(), rect.getMidY());
        auto rotated = rotation != 0.f;
        float cosine = 1.f, sine = 0.f;
        if (rotated)
        {
            auto radians = -rotation * (std::numbers::pi_v<float> / 180.f);
            cosine = std::cos(radians);
            sine = std::sin(radians);
            for (auto &point : verts)
                point = rotatePointAround(point, centre, cosine, sine);
        }

        if (outline.a <= 0.f)
        {
            emit(verts[0], verts[1], verts[2], fill);
            emit(verts[0], verts[2], verts[3], fill);
            return;
        }

        auto inner = insetRect(rect, thickness);
        cocos2d::CCPoint innerVerts[4] = {
            {inner.getMinX(), inner.getMinY()},
            {inner.getMaxX(), inner.getMinY()},
            {inner.getMaxX(), inner.getMaxY()},
            {inner.getMinX(), inner.getMaxY()},
        };
        if (rotated)
        {
            for (auto &point : innerVerts)
                point = rotatePointAround(point, centre, cosine, sine);
        }

        emit(innerVerts[0], innerVerts[1], innerVerts[2], fill);
        emit(innerVerts[0], innerVerts[2], innerVerts[3], fill);

        for (int i = 0; i < 4; ++i)
        {
            int next = (i + 1) % 4;
            emit(verts[i], verts[next], innerVerts[next], outline);
            emit(verts[i], innerVerts[next], innerVerts[i], outline);
        }
    }

    template <class Emit>
    void appendCircle(Emit &&emit, cocos2d::CCPoint centre, float radius, float thickness,
                      cocos2d::ccColor4F fill, cocos2d::ccColor4F outline)
    {
        if (radius <= 0.f || (fill.a <= 0.f && outline.a <= 0.f))
            return;
        thickness = std::clamp(thickness, 0.f, radius);
        auto const &unit = unitCircle();
        auto at = [&](int index, float distance)
        {
            return cocos2d::CCPointMake(
                centre.x + unit[index].x * distance,
                centre.y + unit[index].y * distance);
        };

        if (outline.a <= 0.f)
        {
            for (int i = 0; i < kCircleSegments; ++i)
                emit(centre, at(i, radius), at(i + 1, radius), fill);
            return;
        }

        auto innerRadius = std::max(radius - thickness, 0.f);
        for (int i = 0; i < kCircleSegments; ++i)
        {
            auto innerPoint0 = at(i, innerRadius);
            auto innerPoint1 = at(i + 1, innerRadius);
            emit(centre, innerPoint0, innerPoint1, fill);

            auto outerPoint0 = at(i, radius);
            auto outerPoint1 = at(i + 1, radius);
            emit(outerPoint0, outerPoint1, innerPoint1, outline);
            emit(outerPoint0, innerPoint1, innerPoint0, outline);
        }
    }
}
