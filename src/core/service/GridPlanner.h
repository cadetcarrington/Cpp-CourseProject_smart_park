#pragma once

#include "core/model/ParkingSpot.h"

#include <vector>

namespace smartpark {

struct Route
{
    std::vector<Point> points;
    double distance{0.0};
    double cost{0.0};
    int turnCount{0};
};

struct OccupancyField
{
    std::vector<int> occupancy;
    double weight{0.35};
};

class GridPlanner
{
public:
    GridPlanner(double siteWidth, double siteHeight,
                const std::vector<ParkingSpot> &spots, double cellSize = 0.5);

    OccupancyField buildOccupancy(const std::vector<ParkingSpot> &spots,
                                  double radius = 12.0,
                                  double weight = 0.35) const;

    Route plan(Point from, Point to, const OccupancyField *occupancy = nullptr) const;
    std::vector<Route> planFromToTargets(Point from,
                                         const std::vector<Point> &targets,
                                         const OccupancyField *occupancy = nullptr) const;

private:
    struct SearchResult
    {
        std::vector<double> bestCost;
        std::vector<int> parent;
        std::vector<signed char> arrivalDirection;
        int startIndex{-1};
    };

    int columns() const noexcept;
    int rows() const noexcept;
    int cellIndex(int column, int row) const noexcept;
    Point cellCenter(int column, int row) const noexcept;
    int nearestFreeCell(Point point) const;
    bool isFree(int index) const noexcept;
    double occupancyMultiplier(int cellIndex, const OccupancyField *occupancy) const noexcept;
    SearchResult searchFrom(Point from, const OccupancyField *occupancy, int goalIndex) const;
    Route reconstruct(const SearchResult &search, Point to) const;

    double siteWidth_;
    double siteHeight_;
    double cellSize_;
    std::vector<bool> blocked_;
};

} // namespace smartpark
