#pragma once

#include <cstddef>
#include <vector>

namespace flat_infinite {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct Segment2D {
    Point a;
    Point b;

    double length() const;
    double parameter(const Point& point) const;
    Point closestPoint(const Point& point) const;
    double distance(const Point& point) const;
};

struct Polygon {
    std::vector<Point> points;

    double area() const;
    double perimeter() const;
    bool contains(const Point& point) const;
    double borderDistance(const Point& point) const;
    Polygon clip(const Point& plane_point, const Point& inward_normal) const;
};

double distance(const Point& a, const Point& b);
bool segmentsCross(const Point& a, const Point& b, const Point& c, const Point& d);

} // namespace flat_infinite

