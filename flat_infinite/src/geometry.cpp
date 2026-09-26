#include "flat_infinite/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace flat_infinite {
namespace {

double cross(const Point& a, const Point& b, const Point& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool onSegment(const Point& a, const Point& p, const Point& b) {
    constexpr double eps = 1e-10;
    return p.x >= std::min(a.x, b.x) - eps && p.x <= std::max(a.x, b.x) + eps
        && p.y >= std::min(a.y, b.y) - eps && p.y <= std::max(a.y, b.y) + eps;
}

} // namespace

double distance(const Point& a, const Point& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

double Segment2D::length() const { return flat_infinite::distance(a, b); }

double Segment2D::parameter(const Point& point) const {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double denominator = dx * dx + dy * dy;
    if (denominator <= 1e-18) return 0.0;
    return std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / denominator, 0.0, 1.0);
}

Point Segment2D::closestPoint(const Point& point) const {
    const double t = parameter(point);
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
}

double Segment2D::distance(const Point& point) const {
    return flat_infinite::distance(closestPoint(point), point);
}

double Polygon::area() const {
    if (points.size() < 3) return 0.0;
    double twice_area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        twice_area += a.x * b.y - a.y * b.x;
    }
    return 0.5 * std::abs(twice_area);
}

double Polygon::perimeter() const {
    if (points.size() < 2) return 0.0;
    if (points.size() == 2) return flat_infinite::distance(points[0], points[1]);
    double result = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        result += flat_infinite::distance(points[i], points[(i + 1) % points.size()]);
    }
    return result;
}

bool Polygon::contains(const Point& point) const {
    if (points.size() < 3) return false;
    bool inside = false;
    Point previous = points.back();
    for (const Point& current : points) {
        if ((previous.y > point.y) != (current.y > point.y)
            && point.x < (current.x - previous.x) * (point.y - previous.y)
                    / (current.y - previous.y + 1e-12) + previous.x) {
            inside = !inside;
        }
        previous = current;
    }
    return inside;
}

double Polygon::borderDistance(const Point& point) const {
    if (points.size() < 2) return std::numeric_limits<double>::infinity();
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < points.size(); ++i) {
        best = std::min(best, Segment2D{points[i], points[(i + 1) % points.size()]}.distance(point));
    }
    return best;
}

Polygon Polygon::clip(const Point& plane_point, const Point& inward_normal) const {
    constexpr double eps = 1e-8;
    if (points.empty()) return {};
    auto signedDistance = [&](const Point& p) {
        return (p.x - plane_point.x) * inward_normal.x + (p.y - plane_point.y) * inward_normal.y;
    };
    auto intersection = [&](const Point& a, const Point& b) {
        const double da = signedDistance(a);
        const double db = signedDistance(b);
        const double t = da / (da - db + 1e-12);
        return Point{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
    };

    std::vector<Point> output;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        const bool a_inside = signedDistance(a) >= -eps;
        const bool b_inside = signedDistance(b) >= -eps;
        if (a_inside && b_inside) output.push_back(b);
        else if (a_inside && !b_inside) output.push_back(intersection(a, b));
        else if (!a_inside && b_inside) {
            output.push_back(intersection(a, b));
            output.push_back(b);
        }
    }

    std::vector<Point> deduped;
    for (const Point& p : output) {
        if (deduped.empty() || flat_infinite::distance(p, deduped.back()) > eps) deduped.push_back(p);
    }
    if (deduped.size() > 1 && flat_infinite::distance(deduped.front(), deduped.back()) <= eps) {
        deduped.pop_back();
    }
    return {std::move(deduped)};
}

bool segmentsCross(const Point& a, const Point& b, const Point& c, const Point& d) {
    constexpr double eps = 1e-10;
    const double o1 = cross(a, b, c);
    const double o2 = cross(a, b, d);
    const double o3 = cross(c, d, a);
    const double o4 = cross(c, d, b);
    if (std::abs(o1) <= eps && onSegment(a, c, b)) return true;
    if (std::abs(o2) <= eps && onSegment(a, d, b)) return true;
    if (std::abs(o3) <= eps && onSegment(c, a, d)) return true;
    if (std::abs(o4) <= eps && onSegment(c, b, d)) return true;
    return (o1 > 0.0) != (o2 > 0.0) && (o3 > 0.0) != (o4 > 0.0);
}

} // namespace flat_infinite

