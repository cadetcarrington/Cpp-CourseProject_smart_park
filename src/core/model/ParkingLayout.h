#pragma once

#include "core/model/ParkingSpot.h"

#include <string>
#include <vector>

namespace smartpark {

enum class AisleSide
{
    Left,
    Right
};

class ParkingLayout
{
public:
    static ParkingLayout defaultLayout();
    static ParkingLayout fromDescription(const std::string &description);

    const std::vector<ParkingSpot> &spots() const noexcept;
    const std::vector<Rectangle> &regions() const noexcept;
    double siteWidth() const noexcept;
    double siteHeight() const noexcept;
    const Point &entrance() const noexcept;
    const Point &exit() const noexcept;

private:
    ParkingLayout(double siteWidth, double siteHeight, Point entrance, Point exit);
    void addRegion(const std::string &name, Point origin, int rows, int columns,
                   double spotWidth, double spotLength, double aisleWidth,
                   AisleSide aisleSide);

    std::vector<ParkingSpot> spots_;
    std::vector<Rectangle> regions_;
    double siteWidth_;
    double siteHeight_;
    Point entrance_;
    Point exit_;
};

} // namespace smartpark
