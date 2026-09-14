#include "core/model/ParkingLayout.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace smartpark{
namespace{
std::string toLower(std::string value){
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}
std::string trimCopy(std::string value){
    const auto isNotSpace = [](unsigned char character) {
        return !std::isspace(character);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
    return value;
}
std::string spotIdentifier(const std::string &zone, int serialNumber){
    std::ostringstream stream;
    stream << zone << std::setw(3) << std::setfill('0') << serialNumber;
    return stream.str();
}
double readPositiveDouble(std::istringstream &stream, const std::string &field){
    double value = 0.0;
    if (!(stream >> value) || value <= 0.0){
        throw std::invalid_argument(field + " must be positive");
    }
    return value;
}
double readNonNegativeDouble(std::istringstream &stream, const std::string &field){
    double value = 0.0;
    if (!(stream >> value) || value < 0.0){
        throw std::invalid_argument(field + " cannot be negative");
    }
    return value;
}
int readPositiveInt(std::istringstream &stream, const std::string &field){
    int value = 0;
    if (!(stream >> value) || value <= 0){
        throw std::invalid_argument(field + " must be positive");
    }
    return value;
}
SpotType parseSpotTypeToken(std::string token){
    token = toLower(std::move(token));
    const std::string prefix = "type=";
    if (token.compare(0, prefix.size(), prefix) == 0){
        token = token.substr(prefix.size());
    }
    const auto type = spotTypeFromString(token);
    if (!type){
        throw std::invalid_argument("unknown spot type: " + token);
    }
    return *type;
}
AisleSide parseAisleSide(std::string token){
    token = toLower(std::move(token));
    if (token == "left"){
        return AisleSide::Left;
    }
    if (token == "right"){
        return AisleSide::Right;
    }
    if (token == "up" || token == "top"){
        return AisleSide::Up;
    }
    if (token == "down" || token == "bottom"){
        return AisleSide::Down;
    }
    throw std::invalid_argument("aisle side must be left, right, up or down");
}
bool isVerticalAisle(AisleSide aisleSide) noexcept{
    return aisleSide == AisleSide::Up || aisleSide == AisleSide::Down;
}
} // namespace
ParkingLayout::ParkingLayout(double siteWidth, double siteHeight)
    : siteWidth_(siteWidth)
    , siteHeight_(siteHeight){
}
ParkingLayout ParkingLayout::defaultLayout(){
    return fromDescription(
        "site 100 60\n"
        "entrance 0 30\n"
        "exit 100 30\n"
        "region A 5 8 10 2 1.2 5.5 6 left\n"
        "region B 38 24 10 2 1.4 6.0 6 right\n"
        "region C 71 40 10 2 1.2 5.5 6 left\n");
}
const char *ParkingLayout::garageDescription() noexcept{
    return
        "# SmartPark 6层车库平面图（单位：米）\n"
        "# 原点在左上角：x=0 为轴线 6-1，y=0 为轴线 6-E（北），与图纸/GUI 方向一致。\n"
        "# 场地 58.0 x 42.4；双入口在北墙，单出口在东北。\n"
        "# 电气室 / 设备用房 / 蓄电池室 / 水箱间已改为停车位；仅保留两侧楼梯间。\n"
        "site 58 42.4\n"
        "entrance 22.5 0\n"
        "entrance 31.5 0\n"
        "exit 49.5 0\n"
        "\n"
        "# 北侧 22010x7800 车位带：8 个南北向车位，通道贴北墙\n"
        "region A 27.00 0.00 1 2 2.75 5.30 2.50 up accessible\n"
        "region A2 32.50 0.00 1 6 2.75 5.30 2.50 up normal\n"
        "\n"
        "# 上岛 22060x6810：一排 8 个充电车位，通道在北侧\n"
        "region B 27.00 7.80 1 8 2.75 5.50 3.00 up charging\n"
        "\n"
        "# 下岛 22060x11080：背靠背两排 8+8，通道分别朝北、朝南\n"
        "region C 27.00 16.30 1 8 2.75 5.54 3.00 up normal\n"
        "region D 27.00 24.84 1 8 2.75 5.54 3.00 down normal\n"
        "\n"
        "# 南侧沿 6-A 的 5400 深车位带，MQ2940 宽\n"
        "region E 21.50 33.38 1 12 2.94 5.40 3.00 up normal\n"
        "\n"
        "# 西北 4 个东西向车位，长 5.75，从右侧通道进入\n"
        "region F 12.00 2.50 4 1 2.40 5.75 3.50 right normal\n"
        "\n"
        "# 东北 4 个 VIP 车位，从左侧通道进入\n"
        "region G 49.00 2.50 4 1 2.40 5.50 2.50 left vip\n"
        "\n"
        "# 原设备用房 + 西侧中部：两列 VIP，东列通道接到主通道\n"
        "region H 0.50 24.00 3 2 2.50 5.50 3.00 right vip\n"
        "\n"
        "# 西南 8850x9700 的 4 个车位\n"
        "region I 12.65 32.70 4 1 2.425 5.50 3.35 right normal\n"
        "\n"
        "# 原电气室：北侧一排充电，南侧两席无障碍\n"
        "region J 0.50 0.00 1 4 2.50 5.30 2.50 up charging\n"
        "region J2 0.50 7.80 2 1 2.50 5.50 2.50 right accessible\n"
        "\n"
        "# 原蓄电池室：四席无障碍，通道在东侧与 I 之间\n"
        "region L 0.40 32.70 4 1 2.425 5.50 3.35 right accessible\n"
        "\n"
        "# 原 6-7/6-8 水箱间：三席充电，南侧通道与 D/E 对齐\n"
        "region M 49.00 24.98 1 3 2.50 5.40 3.00 down charging\n"
        "\n"
        "# 仅保留两侧楼梯间\n"
        "obstacle 0.40 12.80 11.20 11.20 楼梯间\n"
        "obstacle 50.20 13.50 7.40 10.90 楼梯间\n";
}
ParkingLayout ParkingLayout::garageLayout(){
    return fromDescription(garageDescription());
}
ParkingLayout ParkingLayout::fromDescription(const std::string &description){
    std::istringstream input(description);
    std::string line;
    bool hasSite = false;
    ParkingLayout layout(1.0, 1.0);
    while (std::getline(input, line)){
        std::istringstream stream(line);
        std::string command;
        if (!(stream >> command) || command.front() == '#'){
            continue;
        }
        command = toLower(command);
        if (command == "site"){
            if (hasSite){
                throw std::invalid_argument("duplicate site line");
            }
            const double width = readPositiveDouble(stream, "site width");
            const double height = readPositiveDouble(stream, "site height");
            if (width < 10.0 || height < 10.0){
                throw std::invalid_argument("site must be at least 10m x 10m");
            }
            layout = ParkingLayout(width, height);
            hasSite = true;
            continue;
        }
        if (!hasSite){
            throw std::invalid_argument("the first layout command must be site");
        }
        if (command == "entrance" || command == "exit"){
            const double x = readNonNegativeDouble(stream, command + " x");
            const double y = readNonNegativeDouble(stream, command + " y");
            const Point point{x, y};
            if (!Rectangle{{0.0, 0.0}, layout.siteWidth_, layout.siteHeight_}.contains(point)){
                throw std::invalid_argument(command + " must be inside the site");
            }
            if (command == "entrance"){
                layout.entrances_.push_back(point);
            } else{
                layout.exits_.push_back(point);
            }
            continue;
        }
        if (command == "obstacle"){
            const double x = readNonNegativeDouble(stream, "obstacle x");
            const double y = readNonNegativeDouble(stream, "obstacle y");
            const double width = readPositiveDouble(stream, "obstacle width");
            const double height = readPositiveDouble(stream, "obstacle height");
            std::string name;
            std::getline(stream, name);
            layout.addObstacle(trimCopy(std::move(name)), {x, y}, width, height);
            continue;
        }
        if (command == "region"){
            std::string name;
            if (!(stream >> name) || name.empty()){
                throw std::invalid_argument("region name cannot be empty");
            }
            const double x = readNonNegativeDouble(stream, "region x");
            const double y = readNonNegativeDouble(stream, "region y");
            const int rows = readPositiveInt(stream, "rows");
            const int columns = readPositiveInt(stream, "columns");
            const double spotWidth = readPositiveDouble(stream, "spot width");
            const double spotLength = readPositiveDouble(stream, "spot length");
            const double aisleWidth = readPositiveDouble(stream, "aisle width");
            std::string sideText;
            if (!(stream >> sideText)){
                throw std::invalid_argument("aisle side is required");
            }
            const AisleSide aisleSide = parseAisleSide(sideText);
            SpotType type = SpotType::Normal;
            std::string typeToken;
            if (stream >> typeToken){
                type = parseSpotTypeToken(typeToken);
                std::string extra;
                if (stream >> extra){
                    throw std::invalid_argument("unexpected region token: " + extra);
                }
            }
            layout.addRegion(name, {x, y}, rows, columns, spotWidth, spotLength,
                             aisleWidth, aisleSide, type);
            continue;
        }
        throw std::invalid_argument("unknown layout command: " + command);
    }
    if (!hasSite || layout.entrances_.empty() || layout.exits_.empty()){
        throw std::invalid_argument("layout requires site, entrance, and exit lines");
    }
    if (layout.spots_.empty()){
        throw std::invalid_argument("layout requires at least one region");
    }
    return layout;
}
void ParkingLayout::addRegion(const std::string &name, Point origin, int rows, int columns,
                              double spotWidth, double spotLength, double aisleWidth,
                              AisleSide aisleSide, SpotType type){
    if (regions_.empty()){
        spotPrefix_ = name;
    }
    const bool vertical = isVerticalAisle(aisleSide);
    const double bayDepth = spotLength + aisleWidth;
    const double regionWidth = vertical ? columns * spotWidth : columns * bayDepth;
    const double regionHeight = vertical ? rows * bayDepth : rows * spotWidth;
    const Rectangle region{origin, regionWidth, regionHeight};
    if (origin.x < -1e-9 || origin.y < -1e-9
        || origin.x + regionWidth > siteWidth_ + 1e-9
        || origin.y + regionHeight > siteHeight_ + 1e-9){
        throw std::invalid_argument("region " + name + " is outside the site");
    }
    if (std::any_of(regions_.begin(), regions_.end(),
                    [&region](const Rectangle &other) { return rectanglesOverlap(region, other); })){
        throw std::invalid_argument("region " + name + " overlaps another region");
    }
    if (std::any_of(obstacles_.begin(), obstacles_.end(),
                    [&region](const LayoutObstacle &other) {
                        return rectanglesOverlap(region, other.bounds);
                    })){
        throw std::invalid_argument("region " + name + " overlaps an obstacle");
    }
    if (aisleWidth < 2.5){
        throw std::invalid_argument("region " + name + " aisle must be at least 2.5m wide");
    }
    const double laneOffset = 0.25;
    for (int row = 0; row < rows; ++row){
        for (int column = 0; column < columns; ++column){
            Rectangle bounds;
            Point accessPoint;
            if (vertical){
                const double spotX = origin.x + column * spotWidth;
                const double spotY = origin.y + row * bayDepth
                    + (aisleSide == AisleSide::Up ? aisleWidth : 0.0);
                bounds = Rectangle{{spotX, spotY}, spotWidth, spotLength};
                const double accessY = aisleSide == AisleSide::Up
                    ? bounds.origin.y - laneOffset
                    : bounds.origin.y + bounds.height + laneOffset;
                accessPoint = Point{bounds.center().x, accessY};
            } else{
                const double spotX = origin.x + column * bayDepth
                    + (aisleSide == AisleSide::Left ? aisleWidth : 0.0);
                const double spotY = origin.y + row * spotWidth;
                bounds = Rectangle{{spotX, spotY}, spotLength, spotWidth};
                const double accessX = aisleSide == AisleSide::Left
                    ? bounds.origin.x - laneOffset
                    : bounds.origin.x + bounds.width + laneOffset;
                accessPoint = Point{accessX, bounds.center().y};
            }
            spots_.emplace_back(
                spotIdentifier(spotPrefix_, static_cast<int>(spots_.size()) + 1),
                ParkingSpot::Geometry{name, row, column, bounds, accessPoint, type});
        }
    }
    regions_.push_back(region);
}
void ParkingLayout::addObstacle(std::string name, Point origin, double width, double height){
    if (name.empty()){
        name = "room";
    }
    const Rectangle bounds{origin, width, height};
    if (origin.x < -1e-9 || origin.y < -1e-9
        || origin.x + width > siteWidth_ + 1e-9
        || origin.y + height > siteHeight_ + 1e-9){
        throw std::invalid_argument("obstacle " + name + " is outside the site");
    }
    if (std::any_of(regions_.begin(), regions_.end(),
                    [&bounds](const Rectangle &other) { return rectanglesOverlap(bounds, other); })){
        throw std::invalid_argument("obstacle " + name + " overlaps a parking region");
    }
    if (std::any_of(obstacles_.begin(), obstacles_.end(),
                    [&bounds](const LayoutObstacle &other) {
                        return rectanglesOverlap(bounds, other.bounds);
                    })){
        throw std::invalid_argument("obstacle " + name + " overlaps another obstacle");
    }
    obstacles_.push_back(LayoutObstacle{std::move(name), bounds});
}
const std::vector<ParkingSpot> &ParkingLayout::spots() const noexcept{
    return spots_;
}
const std::vector<Rectangle> &ParkingLayout::regions() const noexcept{
    return regions_;
}
const std::vector<LayoutObstacle> &ParkingLayout::obstacles() const noexcept{
    return obstacles_;
}
double ParkingLayout::siteWidth() const noexcept{
    return siteWidth_;
}
double ParkingLayout::siteHeight() const noexcept{
    return siteHeight_;
}
const std::vector<Point> &ParkingLayout::entrances() const noexcept{
    return entrances_;
}
const std::vector<Point> &ParkingLayout::exits() const noexcept{
    return exits_;
}
const Point &ParkingLayout::entrance() const{
    if (entrances_.empty()){
        throw std::logic_error("layout has no entrance");
    }
    return entrances_.front();
}
const Point &ParkingLayout::exit() const{
    if (exits_.empty()){
        throw std::logic_error("layout has no exit");
    }
    return exits_.front();
}
} // namespace smartpark
