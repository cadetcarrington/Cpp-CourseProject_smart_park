#pragma once
#include "core/model/ParkingSpot.h"
#include <string>
#include <vector>
namespace smartpark{
enum class AisleSide{
    Left,
    Right
};
class ParkingLayout{
    public:
        static ParkingLayout defaultLayout();
        static ParkingLayout fromDescription(const std::string &description);
        const std::vector<ParkingSpot> &spots() const noexcept;
        const std::vector<Rectangle> &regions() const noexcept;
        double siteWidth() const noexcept;
        double siteHeight() const noexcept;
        const std::vector<Point> &entrances() const noexcept;
        const std::vector<Point> &exits() const noexcept;
        const Point &entrance() const;
        const Point &exit() const;
    private:
        explicit ParkingLayout(double siteWidth, double siteHeight);
        void addRegion(const std::string &name, Point origin, int rows, int columns,
                    double spotWidth, double spotLength, double aisleWidth,
                    AisleSide aisleSide, SpotType type);
        std::vector<ParkingSpot> spots_;
        std::vector<Rectangle> regions_;
        std::string spotPrefix_{"A"};
        double siteWidth_;
        double siteHeight_;
        std::vector<Point> entrances_;
        std::vector<Point> exits_;
    };
} // namespace smartpark
