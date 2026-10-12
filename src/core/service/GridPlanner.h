#pragma once
#include "core/model/ParkingSpot.h"
#include <vector>
namespace smartpark{
struct Route{
    std::vector<Point> points;
    double distance{0.0};
    double cost{0.0};
    int turnCount{0};
};
struct OccupancyField{
    std::vector<int> occupancy;
    double weight{0.35};
};
class GridPlanner{
public:
    GridPlanner(double siteWidth, double siteHeight,
                const std::vector<ParkingSpot> &spots,
                const std::vector<Rectangle> &obstacles = {},
                double cellSize = 0.5);
    OccupancyField buildOccupancy(const std::vector<ParkingSpot> &spots,
                                  double radius = 12.0,
                                  double weight = 0.35) const;
    Route plan(Point from, Point to, const OccupancyField *occupancy = nullptr) const;
    // 多目标搜索：一次搜索同时给出到每个目标的路线。目标是"待评估的一批车位"
    // （分区优先策略下就是同一个分区的候选），全部目标结算即结束——
    // 目标集中在一个分区时只展开"比最远目标更近"的那部分格子，不再扫全图。
    std::vector<Route> planFromToTargets(Point from,
                                         const std::vector<Point> &targets,
                                         const OccupancyField *occupancy = nullptr) const;
private:
    struct SearchResult{
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
    // goals 为空：全图 Dijkstra（保留给需要全场距离场的调用）；
    // goals 非空：多目标 A*，全部目标结算后提前结束。
    SearchResult searchFromGoals(Point from, const OccupancyField *occupancy,
                                 const std::vector<int> &goals) const;
    SearchResult searchFrom(Point from, const OccupancyField *occupancy, int goalIndex) const;
    Route reconstruct(const SearchResult &search, Point to) const;
    double siteWidth_;
    double siteHeight_;
    double cellSize_;
    std::vector<bool> blocked_;
};
} // namespace smartpark
