#include "core/service/GridPlanner.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace smartpark{
    namespace{
        struct SearchNode{
            double priority;
            int index;
        };
        bool operator > (const SearchNode & first, const SearchNode & second) noexcept{
            return first.priority > second.priority;
        }
        struct Direction{
            int dx;
            int dy;
        };
        const Direction directions[] ={
            {1, 0}, {-1, 0}, {0, 1}, {0, -1},
            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
        };
        double octileDistance(Point from, Point to){
            const double dx = std::abs(to.x - from.x);
            const double dy = std::abs(to.y - from.y);
            return std::max(dx, dy) + (std::sqrt(2.0) - 1.0) * std::min(dx, dy);
        }
    }
GridPlanner::GridPlanner(double siteWidth, double siteHeight,
                         const std::vector<ParkingSpot> &spots,
                         const std::vector<Rectangle> &obstacles, double cellSize)
    : siteWidth_(siteWidth)
    , siteHeight_(siteHeight)
    , cellSize_(cellSize){

    if (siteWidth <= 0.0 || siteHeight <= 0.0 || cellSize <= 0.0) {
        throw std::invalid_argument("invalid grid planner dimensions");
    }

    const int columnCount = columns();
    const int rowCount = rows();
    
    blocked_.assign(static_cast<std::size_t>(columnCount) * rowCount, false);
    for (int row = 0; row < rowCount; ++row){
        for (int column = 0; column < columnCount; ++column){
            const Point center = cellCenter(column, row);
            const bool spotBlocked = std::any_of(
                spots.begin(), spots.end(),
                [&center](const ParkingSpot &spot) { return spot.bounds().contains(center); });
            const bool obstacleBlocked = std::any_of(
                obstacles.begin(), obstacles.end(),
                [&center](const Rectangle &obstacle) { return obstacle.contains(center); });
            if (spotBlocked || obstacleBlocked){
                blocked_[cellIndex(column, row)] = true;
            }
        }
    }
}
OccupancyField GridPlanner::buildOccupancy(const std::vector<ParkingSpot> &spots,
                                           double radius, double weight) const{
    OccupancyField field;
    field.weight = weight;
    field.occupancy.assign(blocked_.size(), 0);
    if (radius <= 0.0){
        return field;
    }
    const int columnCount = columns();
    const int rowCount = rows();
    const int radiusCells = static_cast<int>(std::ceil(radius / cellSize_));
    for (const ParkingSpot &spot : spots){
        if (spot.status() != SpotStatus::Occupied && spot.status() != SpotStatus::Reserved){
            continue;
        }
        const int originColumn = std::clamp(
            static_cast<int>(std::floor(spot.accessPoint().x / cellSize_)), 0, columnCount - 1);
        const int originRow = std::clamp(
            static_cast<int>(std::floor(spot.accessPoint().y / cellSize_)), 0, rowCount - 1);
        for (int row = originRow - radiusCells; row <= originRow + radiusCells; ++row){
            for (int column = originColumn - radiusCells; column <= originColumn + radiusCells; ++column){
                if (row < 0 || row >= rowCount || column < 0 || column >= columnCount){
                    continue;
                }
                const int index = cellIndex(column, row);
                if (!isFree(index)){
                    continue;
                }
                if (distance(cellCenter(column, row), spot.accessPoint()) <= radius){
                    ++field.occupancy[static_cast<std::size_t>(index)];
                }
            }
        }
    }
    return field;
}
Route GridPlanner::plan(Point from, Point to, const OccupancyField *occupancy) const{
    const int goalIndex = nearestFreeCell(to);
    return reconstruct(searchFrom(from, occupancy, goalIndex), to);
}
std::vector<Route> GridPlanner::planFromToTargets(Point from,
                                                  const std::vector<Point> &targets,
                                                  const OccupancyField *occupancy) const{
    if (targets.empty()){
        return {};
    }
    // 目标点 → 格子用 reconstruct 里同一套映射，保证每条路线都能重建。
    std::vector<int> goalCells;
    goalCells.reserve(targets.size());
    for (const Point &target : targets){
        const int cell = nearestFreeCell(target);
        if (cell >= 0){
            goalCells.push_back(cell);
        }
    }
    if (goalCells.empty()){
        return std::vector<Route>(targets.size());   // 一个目标都不可达
    }
    const SearchResult search = searchFromGoals(from, occupancy, goalCells);
    std::vector<Route> routes;
    routes.reserve(targets.size());
    for (const Point &target : targets){
        routes.push_back(reconstruct(search, target));
    }
    return routes;
}
double GridPlanner::occupancyMultiplier(int cellIndex, const OccupancyField *occupancy) const noexcept{
    if (occupancy == nullptr
        || occupancy->occupancy.size() != blocked_.size()
        || cellIndex < 0
        || cellIndex >= static_cast<int>(occupancy->occupancy.size())){
        return 1.0;
    }
    return 1.0 + occupancy->weight * static_cast<double>(occupancy->occupancy[static_cast<std::size_t>(cellIndex)]);
}
GridPlanner::SearchResult GridPlanner::searchFrom(
    Point from, const OccupancyField *occupancy, int goalIndex) const{
    // 单目标是"目标集合只有一个"的特例，共用同一套 A* 实现。
    return searchFromGoals(from, occupancy,
                           goalIndex >= 0 ? std::vector<int>{goalIndex} : std::vector<int>{});
}
GridPlanner::SearchResult GridPlanner::searchFromGoals(
    Point from, const OccupancyField *occupancy, const std::vector<int> &goals) const{
    SearchResult result;
    result.startIndex = nearestFreeCell(from);
    const std::size_t nodeCount = blocked_.size();
    result.bestCost.assign(nodeCount, std::numeric_limits<double>::infinity());
    result.parent.assign(nodeCount, -1);
    result.arrivalDirection.assign(nodeCount, -1);
    if (result.startIndex < 0){
        return result;
    }
    const int columnCount = columns();
    const int rowCount = rows();
    const double turnPenalty = 0.35 * cellSize_;

    // 目标集合：去重、丢掉不可达的格子。传了目标但全不可达时直接返回空结果
    // （与原来"单目标格子不可走就不给路线"的行为一致）。
    std::vector<int> goalCells;
    std::vector<bool> isGoal(nodeCount, false);
    for (int goal : goals){
        if (goal < 0 || static_cast<std::size_t>(goal) >= nodeCount
            || !isFree(goal) || isGoal[static_cast<std::size_t>(goal)]){
            continue;
        }
        isGoal[static_cast<std::size_t>(goal)] = true;
        goalCells.push_back(goal);
    }
    if (!goals.empty() && goalCells.empty()){
        return result;
    }
    std::size_t remainingGoals = goalCells.size();
    // 不加启发值：Dijkstra 按代价递增展开，目标全部结算就停，只展开"比最远目标
    // 更近"的那部分格子（目标集中在一个分区时省得最多）。逐格遍历目标集算启发值
    // 反而更贵——这条路径先被实测否掉过一次（189ms vs 131ms）。

    std::vector<bool> closed(nodeCount, false);
    std::priority_queue<SearchNode, std::vector<SearchNode>, std::greater<SearchNode>> openSet;
    result.bestCost[static_cast<std::size_t>(result.startIndex)] = 0.0;
    openSet.push({0.0, result.startIndex});
    while (!openSet.empty()){
        const int current = openSet.top().index;
        openSet.pop();
        if (closed[static_cast<std::size_t>(current)]){
            continue;
        }
        closed[static_cast<std::size_t>(current)] = true;
        if (isGoal[static_cast<std::size_t>(current)] && --remainingGoals == 0){
            break;   // 目标全部结算，再展开也不会改善任何一条路线
        }
        const int currentColumn = current % columnCount;
        const int currentRow = current / columnCount;
        const Point currentPoint = cellCenter(currentColumn, currentRow);
        for (int direction = 0; direction < 8; ++direction){
            const int nextColumn = currentColumn + directions[direction].dx;
            const int nextRow = currentRow + directions[direction].dy;
            if (nextColumn < 0 || nextColumn >= columnCount || nextRow < 0 || nextRow >= rowCount){
                continue;
            }
            const int next = cellIndex(nextColumn, nextRow);
            if (!isFree(next) || closed[static_cast<std::size_t>(next)]){
                continue;
            }
            if (direction >= 4){
                const int firstOrthogonal = cellIndex(currentColumn + directions[direction].dx, currentRow);
                const int secondOrthogonal = cellIndex(currentColumn, currentRow + directions[direction].dy);
                if (!isFree(firstOrthogonal) || !isFree(secondOrthogonal)){
                    continue;
                }
            }
            const Point nextPoint = cellCenter(nextColumn, nextRow);
            const double turnCost =
                result.arrivalDirection[static_cast<std::size_t>(current)] >= 0
                    && result.arrivalDirection[static_cast<std::size_t>(current)] != static_cast<signed char>(direction)
                ? turnPenalty
                : 0.0;
            const double stepCost = (distance(currentPoint, nextPoint) + turnCost)
                * occupancyMultiplier(next, occupancy);
            const double nextCost = result.bestCost[static_cast<std::size_t>(current)] + stepCost;
            if (nextCost < result.bestCost[static_cast<std::size_t>(next)]){
                result.bestCost[static_cast<std::size_t>(next)] = nextCost;
                result.parent[static_cast<std::size_t>(next)] = current;
                result.arrivalDirection[static_cast<std::size_t>(next)] = static_cast<signed char>(direction);
                openSet.push({nextCost, next});
            }
        }
    }
    return result;
}
Route GridPlanner::reconstruct(const SearchResult &search, Point to) const{
    const int goalIndex = nearestFreeCell(to);
    if (search.startIndex < 0 || goalIndex < 0
        || !std::isfinite(search.bestCost[static_cast<std::size_t>(goalIndex)])){
        return {};
    }
    std::vector<int> cells;
    for (int index = goalIndex; index >= 0; index = search.parent[static_cast<std::size_t>(index)]){
        cells.push_back(index);
        if (index == search.startIndex){
            break;
        }
    }
    if (cells.empty() || cells.back() != search.startIndex){
        return {};
    }
    std::reverse(cells.begin(), cells.end());
    std::vector<Point> reversedPoints;
    reversedPoints.reserve(cells.size());
    for (int index : cells){
        reversedPoints.push_back(cellCenter(index % columns(), index / columns()));
    }
    Route route;
    route.cost = search.bestCost[static_cast<std::size_t>(goalIndex)];
    route.points.push_back(reversedPoints.front());
    for (std::size_t index = 1; index + 1 < reversedPoints.size(); ++index){
        const Point previous = reversedPoints[index - 1];
        const Point current = reversedPoints[index];
        const Point next = reversedPoints[index + 1];
        const double firstDx = current.x - previous.x;
        const double firstDy = current.y - previous.y;
        const double secondDx = next.x - current.x;
        const double secondDy = next.y - current.y;
        if (std::abs(firstDx * secondDy - firstDy * secondDx) > 1e-9){
            route.points.push_back(current);
        }
    }
    route.points.push_back(reversedPoints.back());
    for (std::size_t index = 1; index < reversedPoints.size(); ++index){
        route.distance += distance(reversedPoints[index - 1], reversedPoints[index]);
        const signed char previousDirection =
            search.arrivalDirection[static_cast<std::size_t>(cells[index - 1])];
        const signed char currentDirection =
            search.arrivalDirection[static_cast<std::size_t>(cells[index])];
        if (previousDirection >= 0 && currentDirection >= 0 && previousDirection != currentDirection){
            ++route.turnCount;
        }
    }
    return route;
}
int GridPlanner::columns() const noexcept{
    return static_cast<int>(std::ceil(siteWidth_ / cellSize_));
}
int GridPlanner::rows() const noexcept{
    return static_cast<int>(std::ceil(siteHeight_ / cellSize_));
}
int GridPlanner::cellIndex(int column, int row) const noexcept{
    return row * columns() + column;
}
Point GridPlanner::cellCenter(int column, int row) const noexcept{
    return {(column + 0.5) * cellSize_, (row + 0.5) * cellSize_};
}
int GridPlanner::nearestFreeCell(Point point) const{
    int firstColumn = static_cast<int>(std::floor(point.x / cellSize_));
    int firstRow = static_cast<int>(std::floor(point.y / cellSize_));
    firstColumn = std::clamp(firstColumn, 0, columns() - 1);
    firstRow = std::clamp(firstRow, 0, rows() - 1);
    if (isFree(cellIndex(firstColumn, firstRow))){
        return cellIndex(firstColumn, firstRow);
    }
    const int searchRadius = static_cast<int>(std::ceil(2.0 / cellSize_));
    for (int radius = 1; radius <= searchRadius; ++radius){
        for (int row = firstRow - radius; row <= firstRow + radius; ++row){
            for (int column = firstColumn - radius; column <= firstColumn + radius; ++column){
                if (row < 0 || row >= rows() || column < 0 || column >= columns()){
                    continue;
                }
                const int index = cellIndex(column, row);
                if (isFree(index)){
                    return index;
                }
            }
        }
    }
    return -1;
}
bool GridPlanner::isFree(int index) const noexcept{
    return index >= 0 && index < static_cast<int>(blocked_.size()) && !blocked_[index];
}
} 