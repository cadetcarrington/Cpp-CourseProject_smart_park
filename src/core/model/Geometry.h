#pragma once
#include <algorithm>
#include <cmath>
namespace smartpark{
    struct Point{
        double x{0.0};
        double y{0.0};
    };
    inline double distance(Point first, Point second){
        return std::hypot(second.x - first.x, second.y - first.y);
    }
    struct Rectangle{
        Point origin;
        double width{0.0};
        double height{0.0};
        bool contains(Point point) const noexcept{
            return point.x >= origin.x && point.x <= origin.x + width
                && point.y >= origin.y && point.y <= origin.y + height;
        }
        Point center() const noexcept{
            return {origin.x + width / 2.0, origin.y + height / 2.0};
        }
    };
    inline bool rectanglesOverlap(Rectangle first, Rectangle second) noexcept{
        const double overlapX = std::min(first.origin.x + first.width, second.origin.x + second.width)
            - std::max(first.origin.x, second.origin.x);
        const double overlapY = std::min(first.origin.y + first.height, second.origin.y + second.height)
            - std::max(first.origin.y, second.origin.y);
        return overlapX > 1e-9 && overlapY > 1e-9;
    }
} // namespace smartpark
