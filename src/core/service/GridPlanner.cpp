#include "core/service/GridPlanner.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace smartpark{
    namespace{
        struct SearchNode{
            double priority;
            double cost;   // 到达该状态的实际代价，用于惰性删除过期的堆节点
            int state;
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
        constexpr int directionCount = 8;
        double octileDistance(Point from, Point to){
            const double dx = std::abs(to.x - from.x);
            const double dy = std::abs(to.y - from.y);
            return std::max(dx, dy) + (std::sqrt(2.0) - 1.0) * std::min(dx, dy);
        }
        // 四向移动时的启发值：只能上下左右走，剩余代价至少是曼哈顿距离。
        double manhattanDistance(Point from, Point to){
            return std::abs(to.x - from.x) + std::abs(to.y - from.y);
        }

        // 搜索缓冲区按线程复用：真实车库（116×85 格 × 8 方向）一次搜索的
        // bestCost/parent/firstSettled/closed 等数组合计约 1.3MB，原来每次
        // 搜索都要新建并整体清零。现在每个线程保留一份只增不减的缓冲区，
        // 用 generation 标记表示"本次搜索写过这个槽位"：
        //   bestCostStamp[state]     == generation 时 bestCost/parent 有效
        //   firstSettledStamp[cell]  == generation 时 firstSettledState 有效
        //   goalStamp[cell]          == generation 时该格子是本次搜索的目标
        // 未写过的槽位即使留着上一次搜索的数值也不会被读取，因此不需要清零。
        // 缓冲区是 thread_local 的，GridPlanner 的 const 方法仍可被多线程并发调用。
        struct SearchScratch{
            std::vector<double> bestCost;              // 按状态索引
            std::vector<std::uint32_t> bestCostStamp;  // 按状态索引的 generation 标记
            std::vector<int> parent;                   // 按状态索引的父状态
            std::vector<int> firstSettledState;        // 按格子索引
            std::vector<std::uint32_t> firstSettledStamp;
            std::vector<std::uint32_t> goalStamp;
            std::vector<double> heuristic;
            std::vector<int> goalCells;
            std::vector<Point> goalCenters;
            std::vector<SearchNode> openSet;
            std::uint32_t generation{0};

            void ensureCapacity(std::size_t stateCount, std::size_t cellCount){
                if (bestCost.size() < stateCount){
                    bestCost.resize(stateCount);
                    bestCostStamp.resize(stateCount, 0u);
                    parent.resize(stateCount);
                }
                if (firstSettledState.size() < cellCount){
                    firstSettledState.resize(cellCount);
                    firstSettledStamp.resize(cellCount, 0u);
                    goalStamp.resize(cellCount, 0u);
                    heuristic.resize(cellCount);
                }
            }
            std::uint32_t nextGeneration(){
                if (++generation == 0u){
                    // 约 43 亿次搜索才会回绕一次；回绕时把标记清回 0 再继续。
                    std::fill(bestCostStamp.begin(), bestCostStamp.end(), 0u);
                    std::fill(firstSettledStamp.begin(), firstSettledStamp.end(), 0u);
                    std::fill(goalStamp.begin(), goalStamp.end(), 0u);
                    generation = 1u;
                }
                return generation;
            }
        };
        SearchScratch &searchScratch(){
            thread_local SearchScratch scratch;
            return scratch;
        }
    }
GridPlanner::GridPlanner(double siteWidth, double siteHeight,
                         const std::vector<ParkingSpot> &spots,
                         const std::vector<Rectangle> &obstacles, double cellSize,
                         bool allowDiagonal)
    : siteWidth_(siteWidth)
    , siteHeight_(siteHeight)
    , cellSize_(cellSize)
    , allowDiagonal_(allowDiagonal){

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
    if (goalIndex < 0){
        return {};
    }
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
    // 单目标是"目标集合只有一个"的特例，共用同一套带方向状态的多目标搜索。
    return searchFromGoals(from, occupancy,
                           goalIndex >= 0 ? std::vector<int>{goalIndex} : std::vector<int>{});
}
GridPlanner::SearchResult GridPlanner::searchFromGoals(
    Point from, const OccupancyField *occupancy, const std::vector<int> &goals) const{
    SearchResult result;
    result.startIndex = nearestFreeCell(from);
    const std::size_t nodeCount = blocked_.size();
    if (result.startIndex < 0 || nodeCount == 0){
        return result;
    }
    // 状态 = 格子 * 8 + 到达方向，末尾再补一个"无方向"的起点状态。
    const int stateCount = static_cast<int>(nodeCount * directionCount) + 1;
    const int startState = static_cast<int>(nodeCount * directionCount);

    // 取当前线程的复用缓冲区：数组只在容量不足时扩容，其余情况靠 generation
    // 标记区分"本次搜索写过的槽位"和上一次的残留值，省掉每次搜索约 1.3MB 的
    // 分配和整体清零（Debug 构建里这些清零循环也很可观）。
    SearchScratch &scratch = searchScratch();
    scratch.ensureCapacity(static_cast<std::size_t>(stateCount), nodeCount);
    const std::uint32_t generation = scratch.nextGeneration();
    double *const bestCost = scratch.bestCost.data();
    std::uint32_t *const bestCostStamp = scratch.bestCostStamp.data();
    int *const parent = scratch.parent.data();
    int *const firstSettledState = scratch.firstSettledState.data();
    std::uint32_t *const firstSettledStamp = scratch.firstSettledStamp.data();
    std::uint32_t *const goalStamp = scratch.goalStamp.data();
    double *const heuristic = scratch.heuristic.data();

    result.bestCost = bestCost;
    result.parent = parent;
    result.firstSettledState = firstSettledState;
    result.firstSettledStamp = firstSettledStamp;
    result.generation = generation;
    result.cellCount = nodeCount;
    result.startState = startState;

    const int columnCount = columns();
    const int rowCount = rows();
    // 默认 0.5 米栅格下，每次方向变化增加 1.0 的基础代价，增强少转弯偏好。
    const double turnPenalty = 2.0 * cellSize_;
    // 相邻格中心的距离只取决于方向和栅格尺寸（正交一步 cellSize_，对角一步
    // cellSize_ * sqrt(2)），预先算成表，避免在内层循环里反复调用 hypot。
    double stepDistance[directionCount];
    for (int direction = 0; direction < directionCount; ++direction){
        stepDistance[direction] = direction < 4 ? cellSize_ : cellSize_ * std::sqrt(2.0);
    }

    // 目标集合：去重、丢掉不可达的格子。goalStamp[cell] == generation 同时
    // 表示"这个格子是本次搜索的目标"并顺带完成去重，不再单独分配 isGoal 数组。
    // 传了目标但全不可达时直接返回空结果（与原来"单目标格子不可走就不给路线"一致）。
    std::vector<int> &goalCells = scratch.goalCells;
    goalCells.clear();
    for (int goal : goals){
        if (goal < 0 || static_cast<std::size_t>(goal) >= nodeCount
            || !isFree(goal) || goalStamp[static_cast<std::size_t>(goal)] == generation){
            continue;
        }
        goalStamp[static_cast<std::size_t>(goal)] = generation;
        goalCells.push_back(goal);
    }
    if (!goals.empty() && goalCells.empty()){
        return result;
    }
    std::size_t remainingGoals = goalCells.size();
    // 启发值 = 到最近目标格子中心的 octile 距离：忽略障碍、拥堵和转弯，因此
    // 不会高估剩余代价，A* 的结算顺序仍然保证"每格首个结算的状态就是最优状态"，
    // 多个目标也可以共用一次搜索。目标集合为空时启发值全为 0（退化为 Dijkstra）。
    if (!goalCells.empty()){
        std::vector<Point> &goalCenters = scratch.goalCenters;
        goalCenters.clear();
        for (int goal : goalCells){
            goalCenters.push_back(cellCenter(goal % columnCount, goal / columnCount));
        }
        for (int row = 0; row < rowCount; ++row){
            for (int column = 0; column < columnCount; ++column){
                const std::size_t cell = static_cast<std::size_t>(cellIndex(column, row));
                if (blocked_[cell]){
                    continue;
                }
                const Point center = cellCenter(column, row);
                double bestEstimate = std::numeric_limits<double>::infinity();
                for (const Point &goalCenter : goalCenters){
                    bestEstimate = std::min(bestEstimate,
                        allowDiagonal_ ? octileDistance(center, goalCenter)
                                       : manhattanDistance(center, goalCenter));
                }
                heuristic[cell] = bestEstimate;
            }
        }
    } else {
        std::fill(heuristic, heuristic + nodeCount, 0.0);
    }

    // 注意：最优判定必须按"状态"而不能按"格子"。同一格子以不同方向到达的
    // 代价不同，按格子去重会把更优的后续路线剪掉，这正是旧版规划的缺陷来源；
    // firstSettledStamp 用来记录本次搜索是否已经为该格子结算过最优状态，
    // 不再每次搜索都把 firstSettledState 填成 -1。
    std::vector<SearchNode> &openSet = scratch.openSet;
    openSet.clear();
    if (openSet.capacity() < nodeCount){
        openSet.reserve(nodeCount);
    }
    const std::greater<SearchNode> compareNodes;
    auto pushNode = [&openSet, &compareNodes](SearchNode node){
        openSet.push_back(node);
        std::push_heap(openSet.begin(), openSet.end(), compareNodes);
    };
    bestCostStamp[startState] = generation;
    bestCost[startState] = 0.0;
    pushNode({heuristic[result.startIndex], 0.0, startState});
    while (!openSet.empty()){
        const SearchNode current = openSet.front();
        std::pop_heap(openSet.begin(), openSet.end(), compareNodes);
        openSet.pop_back();
        // 惰性删除：同一状态被更优代价重新入堆后，过期堆节点直接丢弃。
        if (current.cost > bestCost[static_cast<std::size_t>(current.state)]){
            continue;
        }
        const int currentState = current.state;
        const bool atStart = currentState == startState;
        const int currentCell = atStart ? result.startIndex : currentState / directionCount;
        const signed char arrivalDirection =
            atStart ? static_cast<signed char>(-1)
                    : static_cast<signed char>(currentState % directionCount);
        const std::size_t currentCellIndex = static_cast<std::size_t>(currentCell);
        // 每个格子第一次被结算的状态就是它代价最低的状态；多目标提前结束和
        // 路线重建都以这个状态为准，路线代价天然等于"单独搜该目标"的最优值。
        if (goalStamp[currentCellIndex] == generation
            && firstSettledStamp[currentCellIndex] != generation){
            firstSettledStamp[currentCellIndex] = generation;
            firstSettledState[currentCellIndex] = currentState;
            if (--remainingGoals == 0){
                break;   // 目标全部结算，再展开也不会改善任何一条路线
            }
        }
        const int currentColumn = currentCell % columnCount;
        const int currentRow = currentCell / columnCount;
        // 四向模式只展开前 4 个方向（方向表把上下左右排在前面），斜走那四条
        // 分支连同下面的切角检查一起跳过。
        const int directionLimit = allowDiagonal_ ? directionCount : 4;
        for (int direction = 0; direction < directionLimit; ++direction){
            const int nextColumn = currentColumn + directions[direction].dx;
            const int nextRow = currentRow + directions[direction].dy;
            if (nextColumn < 0 || nextColumn >= columnCount || nextRow < 0 || nextRow >= rowCount){
                continue;
            }
            const int next = cellIndex(nextColumn, nextRow);
            if (!isFree(next)){
                continue;
            }
            if (direction >= 4){
                const int firstOrthogonal = cellIndex(currentColumn + directions[direction].dx, currentRow);
                const int secondOrthogonal = cellIndex(currentColumn, currentRow + directions[direction].dy);
                if (!isFree(firstOrthogonal) || !isFree(secondOrthogonal)){
                    continue;
                }
            }
            const signed char nextArrivalDirection = static_cast<signed char>(direction);
            const int nextState = next * directionCount + direction;
            const double turnCost =
                arrivalDirection >= 0 && arrivalDirection != nextArrivalDirection
                ? turnPenalty
                : 0.0;
            const double stepCost = (stepDistance[direction] + turnCost)
                * occupancyMultiplier(next, occupancy);
            const double nextCost = current.cost + stepCost;
            if (bestCostStamp[static_cast<std::size_t>(nextState)] != generation
                || nextCost < bestCost[static_cast<std::size_t>(nextState)]){
                bestCostStamp[static_cast<std::size_t>(nextState)] = generation;
                bestCost[static_cast<std::size_t>(nextState)] = nextCost;
                parent[static_cast<std::size_t>(nextState)] = currentState;
                pushNode({nextCost + heuristic[static_cast<std::size_t>(next)],
                          nextCost, nextState});
            }
        }
    }
    return result;
}
Route GridPlanner::reconstruct(const SearchResult &search, Point to) const{
    const int goalIndex = nearestFreeCell(to);
    if (search.startIndex < 0 || search.startState < 0 || goalIndex < 0
        || search.bestCost == nullptr || search.parent == nullptr
        || search.firstSettledState == nullptr || search.firstSettledStamp == nullptr
        || static_cast<std::size_t>(goalIndex) >= search.cellCount){
        return {};
    }
    const std::size_t goalCellIndex = static_cast<std::size_t>(goalIndex);
    // 只有"本次搜索（同一 generation）结算过"的目标格子才有最优状态；
    // 缓冲区里上一次搜索残留的 firstSettledState 不能拿来重建路线。
    if (search.firstSettledStamp[goalCellIndex] != search.generation){
        return {};
    }
    const int goalState = search.firstSettledState[goalCellIndex];
    if (goalState < 0){
        return {};
    }
    // 沿父状态链回溯到无方向起点状态，再把状态链翻译成（格子, 到达方向）链。
    // 状态图允许重复经过同一个格子（只要到达方向不同），所以这里不假设路径简单。
    std::vector<int> states;
    for (int state = goalState; state >= 0;
         state = search.parent[static_cast<std::size_t>(state)]){
        states.push_back(state);
        if (state == search.startState){
            break;
        }
    }
    if (states.empty() || states.back() != search.startState){
        return {};
    }
    std::reverse(states.begin(), states.end());
    const int columnCount = columns();
    std::vector<int> cells;
    std::vector<signed char> arrivalDirections;
    cells.reserve(states.size());
    arrivalDirections.reserve(states.size());
    for (int state : states){
        if (state == search.startState){
            cells.push_back(search.startIndex);
            arrivalDirections.push_back(-1);
        } else {
            cells.push_back(state / directionCount);
            arrivalDirections.push_back(static_cast<signed char>(state % directionCount));
        }
    }
    const auto centerOf = [this, columnCount](int cell){
        return cellCenter(cell % columnCount, cell / columnCount);
    };
    // 折线只保留起点、到达方向发生变化处和终点，与修复前 Route.points 的语义一致。
    Route route;
    route.cost = search.bestCost[static_cast<std::size_t>(goalState)];
    route.points.push_back(centerOf(cells.front()));
    for (std::size_t index = 1; index + 1 < cells.size(); ++index){
        if (arrivalDirections[index] != arrivalDirections[index + 1]){
            route.points.push_back(centerOf(cells[index]));
        }
    }
    route.points.push_back(centerOf(cells.back()));
    for (std::size_t index = 1; index < cells.size(); ++index){
        route.distance += distance(centerOf(cells[index - 1]), centerOf(cells[index]));
        if (arrivalDirections[index - 1] >= 0
            && arrivalDirections[index - 1] != arrivalDirections[index]){
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