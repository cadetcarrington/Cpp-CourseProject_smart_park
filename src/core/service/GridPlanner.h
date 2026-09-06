#pragma once

#include "core/model/ParkingSpot.h"

#include <vector>

namespace smartpark {

struct Route
{
    std::vector<Point> points;
    double distance{0.0};
};

class GridPlanner
{
public:
    GridPlanner(double siteWidth, double siteHeight,
                const std::vector<ParkingSpot> &spots, double cellSize = 0.5);

    Route plan(Point from, Point to) const;

private:
    int columns() const noexcept;
    int rows() const noexcept;
    int cellIndex(int column, int row) const noexcept;
    Point cellCenter(int column, int row) const noexcept;
    int nearestFreeCell(Point point) const;
    bool isFree(int index) const noexcept;

    double siteWidth_;
    double siteHeight_;
    double cellSize_;
    std::vector<bool> blocked_;
};

} // namespace smartpark
