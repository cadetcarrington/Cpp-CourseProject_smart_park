#include "core/service/GridPlanner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <stdexcept>

namespace smartpark {
namespace {

struct SearchNode
{
    double priority;
    int index;
};

bool operator>(const SearchNode &first, const SearchNode &second) noexcept
{
    return first.priority > second.priority;
}

struct Direction
{
    int dx;
    int dy;
};

const Direction directions[] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
};

double octileDistance(Point from, Point to, double cellSize)
{
    const double dx = std::abs(to.x - from.x);
    const double dy = std::abs(to.y - from.y);
    (void)cellSize;
    return std::max(dx, dy) + (std::sqrt(2.0) - 1.0) * std::min(dx, dy);
}

} // namespace

GridPlanner::GridPlanner(double siteWidth, double siteHeight,
                         const std::vector<ParkingSpot> &spots, double cellSize)
    : siteWidth_(siteWidth)
    , siteHeight_(siteHeight)
    , cellSize_(cellSize)
{
    if (siteWidth <= 0.0 || siteHeight <= 0.0 || cellSize <= 0.0) {
        throw std::invalid_argument("invalid grid planner dimensions");
    }

    const int columnCount = columns();
    const int rowCount = rows();
    blocked_.assign(static_cast<std::size_t>(columnCount) * rowCount, false);

    for (int row = 0; row < rowCount; ++row) {
        for (int column = 0; column < columnCount; ++column) {
            const Point center = cellCenter(column, row);
            const bool spotBlocked = std::any_of(
                spots.begin(), spots.end(),
                [&center](const ParkingSpot &spot) { return spot.bounds().contains(center); });
            if (spotBlocked) {
                blocked_[cellIndex(column, row)] = true;
            }
        }
    }
}

Route GridPlanner::plan(Point from, Point to) const
{
    const int startIndex = nearestFreeCell(from);
    const int goalIndex = nearestFreeCell(to);
    if (startIndex < 0 || goalIndex < 0) {
        return {};
    }

    const std::size_t nodeCount = blocked_.size();
    const double turnPenalty = 0.35 * cellSize_;
    std::vector<double> bestCost(nodeCount, std::numeric_limits<double>::infinity());
    std::vector<int> parent(nodeCount, -1);
    std::vector<signed char> arrivalDirection(nodeCount, -1);
    std::vector<bool> closed(nodeCount, false);
    std::priority_queue<SearchNode, std::vector<SearchNode>, std::greater<SearchNode>> openSet;

    bestCost[startIndex] = 0.0;
    openSet.push({octileDistance(from, to, cellSize_), startIndex});

    while (!openSet.empty()) {
        const int current = openSet.top().index;
        openSet.pop();
        if (closed[current]) {
            continue;
        }
        closed[current] = true;
        if (current == goalIndex) {
            break;
        }

        const int currentColumn = current % columns();
        const int currentRow = current / columns();
        const Point currentPoint = cellCenter(currentColumn, currentRow);

        for (int direction = 0; direction < 8; ++direction) {
            const int nextColumn = currentColumn + directions[direction].dx;
            const int nextRow = currentRow + directions[direction].dy;
            if (nextColumn < 0 || nextColumn >= columns() || nextRow < 0 || nextRow >= rows()) {
                continue;
            }
            const int next = cellIndex(nextColumn, nextRow);
            if (!isFree(next) || closed[next]) {
                continue;
            }
            if (direction >= 4) {
                const int firstOrthogonal = cellIndex(currentColumn + directions[direction].dx, currentRow);
                const int secondOrthogonal = cellIndex(currentColumn, currentRow + directions[direction].dy);
                if (!isFree(firstOrthogonal) || !isFree(secondOrthogonal)) {
                    continue;
                }
            }

            const Point nextPoint = cellCenter(nextColumn, nextRow);
            const double stepCost = distance(currentPoint, nextPoint)
                + (arrivalDirection[current] >= 0 && arrivalDirection[current] != direction
                       ? turnPenalty : 0.0);
            const double nextCost = bestCost[current] + stepCost;
            if (nextCost < bestCost[next]) {
                bestCost[next] = nextCost;
                parent[next] = current;
                arrivalDirection[next] = static_cast<signed char>(direction);
                openSet.push({nextCost + octileDistance(nextPoint, to, cellSize_), next});
            }
        }
    }

    if (!std::isfinite(bestCost[goalIndex])) {
        return {};
    }

    std::vector<Point> reversedPoints;
    for (int index = goalIndex; index >= 0; index = parent[index]) {
        reversedPoints.push_back(cellCenter(index % columns(), index / columns()));
        if (index == startIndex) {
            break;
        }
    }
    if (reversedPoints.empty()) {
        return {};
    }
    std::reverse(reversedPoints.begin(), reversedPoints.end());

    Route route;
    route.points.push_back(reversedPoints.front());
    for (std::size_t index = 1; index + 1 < reversedPoints.size(); ++index) {
        const Point previous = reversedPoints[index - 1];
        const Point current = reversedPoints[index];
        const Point next = reversedPoints[index + 1];
        const double firstDx = current.x - previous.x;
        const double firstDy = current.y - previous.y;
        const double secondDx = next.x - current.x;
        const double secondDy = next.y - current.y;
        if (std::abs(firstDx * secondDy - firstDy * secondDx) > 1e-9) {
            route.points.push_back(current);
        }
    }
    route.points.push_back(reversedPoints.back());

    for (std::size_t index = 1; index < reversedPoints.size(); ++index) {
        route.distance += distance(reversedPoints[index - 1], reversedPoints[index]);
    }
    return route;
}

int GridPlanner::columns() const noexcept
{
    return static_cast<int>(std::ceil(siteWidth_ / cellSize_));
}

int GridPlanner::rows() const noexcept
{
    return static_cast<int>(std::ceil(siteHeight_ / cellSize_));
}

int GridPlanner::cellIndex(int column, int row) const noexcept
{
    return row * columns() + column;
}

Point GridPlanner::cellCenter(int column, int row) const noexcept
{
    return {(column + 0.5) * cellSize_, (row + 0.5) * cellSize_};
}

int GridPlanner::nearestFreeCell(Point point) const
{
    int firstColumn = static_cast<int>(std::floor(point.x / cellSize_));
    int firstRow = static_cast<int>(std::floor(point.y / cellSize_));
    firstColumn = std::clamp(firstColumn, 0, columns() - 1);
    firstRow = std::clamp(firstRow, 0, rows() - 1);
    if (isFree(cellIndex(firstColumn, firstRow))) {
        return cellIndex(firstColumn, firstRow);
    }

    const int searchRadius = static_cast<int>(std::ceil(2.0 / cellSize_));
    for (int radius = 1; radius <= searchRadius; ++radius) {
        for (int row = firstRow - radius; row <= firstRow + radius; ++row) {
            for (int column = firstColumn - radius; column <= firstColumn + radius; ++column) {
                if (row < 0 || row >= rows() || column < 0 || column >= columns()) {
                    continue;
                }
                const int index = cellIndex(column, row);
                if (isFree(index)) {
                    return index;
                }
            }
        }
    }
    return -1;
}

bool GridPlanner::isFree(int index) const noexcept
{
    return index >= 0 && index < static_cast<int>(blocked_.size()) && !blocked_[index];
}

} // namespace smartpark
