#pragma once
#include "core/model/ParkingSpot.h"
#include <cstddef>
#include <cstdint>
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
    // allowDiagonal = false 时只允许上下左右四向移动：路线折线因此严格水平/垂直，
    // 不会出现斜穿车道的对角线（行人/寻车路线要求「沿着车道走」时用这个）。
    // 车行路线（进场/出场评估）保持默认的八向移动，弧长与评分口径都不变。
    GridPlanner(double siteWidth, double siteHeight,
                const std::vector<ParkingSpot> &spots,
                const std::vector<Rectangle> &obstacles = {},
                double cellSize = 0.5,
                bool allowDiagonal = true);
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
    // 搜索状态 =（格子, 到达方向 0..7）。转弯惩罚取决于到达方向，同一个格子
    // 以不同方向到达时后续代价不同，所以不能只保留"每格一个最优值"；
    // 另外起点没有到达方向，单独占一个无方向状态。
    //
    // SearchResult 只是指向"当前线程搜索缓冲区"的只读视图：bestCost/parent/
    // firstSettled* 都存在 thread_local 缓冲区里，用 generation 标记区分本次
    // 搜索写过的值和上一次搜索的残留值（见 GridPlanner.cpp），不再每次搜索都
    // 新建并清零约 1.3MB 的数组。视图在下一次同一线程发起搜索前一直有效；
    // plan()/planFromToTargets() 都是"先搜索、紧接着重建路线"，满足该约束。
    struct SearchResult{
        const double *bestCost{nullptr};                 // 按状态索引：格子 * 8 + 到达方向
        const int *parent{nullptr};                      // 按状态索引的父状态
        const int *firstSettledState{nullptr};           // 每个格子首次结算（即最优）的状态
        const std::uint32_t *firstSettledStamp{nullptr}; // firstSettledState 的 generation 标记
        std::uint32_t generation{0};
        std::size_t cellCount{0};
        int startIndex{-1};
        int startState{-1};
    };
    int columns() const noexcept;
    int rows() const noexcept;
    int cellIndex(int column, int row) const noexcept;
    Point cellCenter(int column, int row) const noexcept;
    int nearestFreeCell(Point point) const;
    bool isFree(int index) const noexcept;
    double occupancyMultiplier(int cellIndex, const OccupancyField *occupancy) const noexcept;
    // goals 为空：全图 Dijkstra（保留给需要全场距离场的调用）；
    // goals 非空：多目标 A*（启发值 = 到最近目标格子中心的 octile 距离，
    // 忽略障碍/占用/转弯所以不会高估），所有目标都结算后提前结束。
    SearchResult searchFromGoals(Point from, const OccupancyField *occupancy,
                                 const std::vector<int> &goals) const;
    SearchResult searchFrom(Point from, const OccupancyField *occupancy, int goalIndex) const;
    Route reconstruct(const SearchResult &search, Point to) const;
    double siteWidth_;
    double siteHeight_;
    double cellSize_;
    bool allowDiagonal_{true};
    std::vector<bool> blocked_;
};
} // namespace smartpark
