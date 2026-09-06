#include "core/model/ParkingLayout.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace smartpark {
namespace {

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::string spotIdentifier(const std::string &zone, int serialNumber)
{
    std::ostringstream stream;
    stream << zone << std::setw(3) << std::setfill('0') << serialNumber;
    return stream.str();
}

double readPositiveDouble(std::istringstream &stream, const std::string &field)
{
    double value = 0.0;
    if (!(stream >> value) || value <= 0.0) {
        throw std::invalid_argument(field + " must be positive");
    }
    return value;
}

double readNonNegativeDouble(std::istringstream &stream, const std::string &field)
{
    double value = 0.0;
    if (!(stream >> value) || value < 0.0) {
        throw std::invalid_argument(field + " cannot be negative");
    }
    return value;
}

int readPositiveInt(std::istringstream &stream, const std::string &field)
{
    int value = 0;
    if (!(stream >> value) || value <= 0) {
        throw std::invalid_argument(field + " must be positive");
    }
    return value;
}

} // namespace

ParkingLayout::ParkingLayout(double siteWidth, double siteHeight, Point entrance, Point exit)
    : siteWidth_(siteWidth)
    , siteHeight_(siteHeight)
    , entrance_(entrance)
    , exit_(exit)
{
}

ParkingLayout ParkingLayout::defaultLayout()
{
    return fromDescription(
        "site 100 60\n"
        "entrance 0 30\n"
        "exit 100 30\n"
        "region A 5 8 10 2 1.2 5.5 6 left\n"
        "region B 38 24 10 2 1.4 6.0 6 right\n"
        "region C 71 40 10 2 1.2 5.5 6 left\n");
}

ParkingLayout ParkingLayout::fromDescription(const std::string &description)
{
    std::istringstream input(description);
    std::string line;
    bool hasSite = false;
    bool hasEntrance = false;
    bool hasExit = false;
    ParkingLayout layout(1.0, 1.0, {0.0, 0.0}, {1.0, 1.0});

    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string command;
        if (!(stream >> command) || command.front() == '#') {
            continue;
        }

        command = toLower(command);
        if (command == "site") {
            if (hasSite) {
                throw std::invalid_argument("duplicate site line");
            }
            const double width = readPositiveDouble(stream, "site width");
            const double height = readPositiveDouble(stream, "site height");
            if (width < 10.0 || height < 10.0) {
                throw std::invalid_argument("site must be at least 10m x 10m");
            }
            layout = ParkingLayout(width, height, {0.0, 0.0}, {width, height / 2.0});
            hasSite = true;
            continue;
        }

        if (!hasSite) {
            throw std::invalid_argument("the first layout command must be site");
        }

        if (command == "entrance" || command == "exit") {
            const double x = readNonNegativeDouble(stream, command + " x");
            const double y = readNonNegativeDouble(stream, command + " y");
            const Point point{x, y};
            if (!Rectangle{{0.0, 0.0}, layout.siteWidth_, layout.siteHeight_}.contains(point)) {
                throw std::invalid_argument(command + " must be inside the site");
            }
            if (command == "entrance") {
                layout.entrance_ = point;
                hasEntrance = true;
            } else {
                layout.exit_ = point;
                hasExit = true;
            }
            continue;
        }

        if (command == "region") {
            std::string name;
            if (!(stream >> name) || name.empty()) {
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
            if (!(stream >> sideText)) {
                throw std::invalid_argument("aisle side is required");
            }
            const std::string side = toLower(sideText);
            if (side != "left" && side != "right") {
                throw std::invalid_argument("aisle side must be left or right");
            }
            layout.addRegion(name, {x, y}, rows, columns, spotWidth, spotLength,
                             aisleWidth, side == "left" ? AisleSide::Left : AisleSide::Right);
            continue;
        }

        throw std::invalid_argument("unknown layout command: " + command);
    }

    if (!hasSite || !hasEntrance || !hasExit) {
        throw std::invalid_argument("layout requires site, entrance, and exit lines");
    }
    if (layout.spots_.empty()) {
        throw std::invalid_argument("layout requires at least one region");
    }
    return layout;
}

void ParkingLayout::addRegion(const std::string &name, Point origin, int rows, int columns,
                              double spotWidth, double spotLength, double aisleWidth,
                              AisleSide aisleSide)
{
    if (regions_.empty()) {
        spotPrefix_ = name;
    }
    const double bayWidth = spotLength + aisleWidth;
    const double regionWidth = columns * bayWidth;
    const double regionHeight = rows * spotWidth;
    const Rectangle region{origin, regionWidth, regionHeight};

    if (origin.x < 0.0 || origin.y < 0.0
        || origin.x + regionWidth > siteWidth_ || origin.y + regionHeight > siteHeight_) {
        throw std::invalid_argument("region " + name + " is outside the site");
    }
    if (std::any_of(regions_.begin(), regions_.end(),
                    [&region](const Rectangle &other) { return rectanglesOverlap(region, other); })) {
        throw std::invalid_argument("region " + name + " overlaps another region");
    }
    if (aisleWidth < 2.5) {
        throw std::invalid_argument("region " + name + " aisle must be at least 2.5m wide");
    }

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const double spotX = origin.x + column * bayWidth
                + (aisleSide == AisleSide::Left ? aisleWidth : 0.0);
            const double spotY = origin.y + row * spotWidth;
            const Rectangle bounds{{spotX, spotY}, spotLength, spotWidth};
            const double laneOffset = 0.25;
            const double accessX = aisleSide == AisleSide::Left
                ? bounds.origin.x - laneOffset
                : bounds.origin.x + bounds.width + laneOffset;
            const Point accessPoint{accessX, bounds.center().y};
            spots_.emplace_back(
                spotIdentifier(spotPrefix_, static_cast<int>(spots_.size()) + 1),
                ParkingSpot::Geometry{name, row, column, bounds, accessPoint});
        }
    }
    regions_.push_back(region);
}

const std::vector<ParkingSpot> &ParkingLayout::spots() const noexcept
{
    return spots_;
}

const std::vector<Rectangle> &ParkingLayout::regions() const noexcept
{
    return regions_;
}

double ParkingLayout::siteWidth() const noexcept
{
    return siteWidth_;
}

double ParkingLayout::siteHeight() const noexcept
{
    return siteHeight_;
}

const Point &ParkingLayout::entrance() const noexcept
{
    return entrance_;
}

const Point &ParkingLayout::exit() const noexcept
{
    return exit_;
}

} // namespace smartpark
