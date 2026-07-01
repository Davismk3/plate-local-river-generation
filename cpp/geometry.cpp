#include "utilities/geometry.hpp"

#include <algorithm>
#include <cmath>

double Point::pointDistanceToPoint(const Point& other_point) const {
    return std::hypot(x - other_point.x, y - other_point.y, z - other_point.z);
}

Vector3D Point::pointVectorToPoint(const Point& other_point) const {
    return {other_point.x - x, other_point.y - y, other_point.z - z};
}

double Segment::segmentLength() const {
    return point_1.pointDistanceToPoint(point_2);
}

Point Segment::segmetMidpoint() const {
    return {
        (point_1.x + point_2.x) / 2.0,
        (point_1.y + point_2.y) / 2.0,
        (point_1.z + point_2.z) / 2.0
    };
}

bool Segment::pointOnSegment(const Point& point_3) const {
    double eps = 1e-8;
    Vector3D vect_1 = point_1.pointVectorToPoint(point_2);
    Vector3D vect_2 = point_1.pointVectorToPoint(point_3);
    Vector3D cross_product = crossProduct(vect_1, vect_2);
    bool on_line = (std::hypot(cross_product.x, cross_product.y, cross_product.z) < eps);
    if (!on_line) {
        return false;
    }
    bool on_segment = (
        std::min(point_1.x, point_2.x) <= point_3.x && point_3.x <= std::max(point_1.x, point_2.x) &&
        std::min(point_1.y, point_2.y) <= point_3.y && point_3.y <= std::max(point_1.y, point_2.y) &&
        std::min(point_1.z, point_2.z) <= point_3.z && point_3.z <= std::max(point_1.z, point_2.z)
    );
    if (on_segment) {
        return true;
    }
    return false;
}

bool Segment::segmentIntersectsSegment(const Segment& other_segment) const {
    constexpr double eps = 1e-8;
    constexpr double eps_squared = eps * eps;

    Vector3D direction_1 = point_1.pointVectorToPoint(point_2);
    Vector3D direction_2 = other_segment.point_1.pointVectorToPoint(other_segment.point_2);
    Vector3D offset = other_segment.point_1.pointVectorToPoint(point_1);

    double a = dotProduct(direction_1, direction_1);
    double b = dotProduct(direction_1, direction_2);
    double c = dotProduct(direction_1, offset);
    double e = dotProduct(direction_2, direction_2);
    double f = dotProduct(direction_2, offset);
    double s = 0.0;
    double t = 0.0;

    if (a <= eps_squared && e <= eps_squared) {
        return point_1.pointDistanceToPoint(other_segment.point_1) <= eps;
    }

    if (a <= eps_squared) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else if (e <= eps_squared) {
        s = std::clamp(-c / a, 0.0, 1.0);
    } else {
        double denominator = a * e - b * b;
        if (std::abs(denominator) > eps_squared) {
            s = std::clamp((b * f - c * e) / denominator, 0.0, 1.0);
        }
        t = (b * s + f) / e;
        if (t < 0.0) {
            t = 0.0;
            s = std::clamp(-c / a, 0.0, 1.0);
        } else if (t > 1.0) {
            t = 1.0;
            s = std::clamp((b - c) / a, 0.0, 1.0);
        }
    }
    Point closest_1{
        point_1.x + s * direction_1.x,
        point_1.y + s * direction_1.y,
        point_1.z + s * direction_1.z
    };
    Point closest_2{
        other_segment.point_1.x + t * direction_2.x,
        other_segment.point_1.y + t * direction_2.y,
        other_segment.point_1.z + t * direction_2.z
    };
    return closest_1.pointDistanceToPoint(closest_2) <= eps;
}

double Segment::segmentDistanceToPoint(const Point& point_3) const {
    double eps = 1e-12;
    Vector3D vect_1 = point_1.pointVectorToPoint(point_2);
    Vector3D vect_2 = point_1.pointVectorToPoint(point_3);
    double numerator = dotProduct(vect_2, vect_1);
    double denominator = dotProduct(vect_1, vect_1);
    if (denominator <= eps * eps) {
        return point_1.pointDistanceToPoint(point_3);
    }
    double t = std::max(0.0, std::min(1.0, numerator / denominator));

    Point segment_point = {
        point_1.x + t * (point_2.x - point_1.x),
        point_1.y + t * (point_2.y - point_1.y),
        point_1.z + t * (point_2.z - point_1.z)
    };

    return segment_point.pointDistanceToPoint(point_3);
}

Vector3D Polygon::polygonNormal() const {
    double eps = 1e-8;

    if (points.size() < 3) {
        return {0.0, 0.0, 0.0};
    }

    Vector3D vect_1 = points[0].pointVectorToPoint(points[1]);
    Vector3D vect_2 = points[0].pointVectorToPoint(points[2]);
    Vector3D normal = crossProduct(vect_1, vect_2);
    double normal_length = std::hypot(normal.x, normal.y, normal.z);

    if (normal_length < eps) {
        return {0.0, 0.0, 0.0};
    }
    return {normal.x / normal_length, normal.y / normal_length, normal.z / normal_length};
}

bool Polygon::polygonIsValid() const {
    double eps = 1e-8;
    Vector3D norm = polygonNormal();

    if (points.size() < 3) {
        return false;
    }

    if (std::hypot(norm.x, norm.y, norm.z) < eps) {
        return false;
    }

    for (std::size_t i = 3; i < points.size(); ++i) {
        Vector3D vect_3 = points[0].pointVectorToPoint(points[i]);
        double error = std::abs(dotProduct(norm, vect_3));
        if (error > eps) {
            return false;
        }
    }
    return true;
}

double Polygon::polygonPerimeter() const {
    double perim = 0.0;

    if (points.size() == 1) {
        return perim;
    }

    if (points.size() == 2) {
        Point point_1 = points[0];
        Point point_2 = points[1];
        return point_1.pointDistanceToPoint(point_2);
    }

    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& point_a = points[i];
        const Point& point_b = points[(i + 1) % points.size()];

        perim += point_a.pointDistanceToPoint(point_b);
    }
    return perim;
}

double Polygon::polygonArea() const {
    if (points.size() < 3) {
        return 0.0;
    }
    Vector3D area_vector{0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& point_a = points[i];
        const Point& point_b = points[(i + 1) % points.size()];

        area_vector.x += (point_a.y - point_b.y) * (point_a.z + point_b.z);
        area_vector.y += (point_a.z - point_b.z) * (point_a.x + point_b.x);
        area_vector.z += (point_a.x - point_b.x) * (point_a.y + point_b.y);
    }
    return 0.5 * std::hypot(area_vector.x, area_vector.y, area_vector.z);
}

std::vector<Segment> Polygon::polygonSegments() const {
    std::vector<Segment> segments = {};

    if (points.size() <= 1) {
        return segments;
    }

    if (points.size() == 2) {
        Segment segment = {points[0], points[1]};
        segments.push_back(segment);
        return segments;
    }

    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& point_a = points[i];
        const Point& point_b = points[(i + 1) % points.size()];
        Segment segment = {point_a, point_b};
        segments.push_back(segment);
    }
    return segments;
}

bool Polygon::polygonEnclosesPoint(const Point& point) const {
    double inf = 1e12;
    double eps = 1e-8;

    if (points.size() < 3) {
        return false;
    }

    Vector3D vect_1 = points[0].pointVectorToPoint(point);
    Vector3D vect_2 = polygonNormal();

    if (std::abs(dotProduct(vect_1, vect_2)) > eps) {
        return false;
    }

    Vector3D helper_axis = {0.0, 0.0, 1.0};
    if (std::abs(vect_2.x) <= std::abs(vect_2.y) && std::abs(vect_2.x) <= std::abs(vect_2.z)) {
        helper_axis = {1.0, 0.0, 0.0};
    } else if (std::abs(vect_2.y) <= std::abs(vect_2.z)) {
        helper_axis = {0.0, 1.0, 0.0};
    }

    Vector3D ray_direction = crossProduct(vect_2, helper_axis);
    Segment ref_ray = {point, {point.x + inf * ray_direction.x, point.y + inf * ray_direction.y, point.z + inf * ray_direction.z}};

    std::vector<Segment> polygon_segments = polygonSegments();
    int intersection_count = 0;
    for (const Segment& polygon_segment : polygon_segments) {
        if (ref_ray.segmentIntersectsSegment(polygon_segment)) {
            ++intersection_count;
        }
    }
    return intersection_count % 2 != 0;
}

double Polygon::polygonBorderDistanceToPoint(const Point& point) const {
    std::vector<Segment> segments = polygonSegments();
    std::vector<double> distances = {};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        distances.push_back(segments[i].segmentDistanceToPoint(point));
    }
    return *std::min_element(distances.begin(), distances.end());
}

Polygon Polygon::polygonClip(const Plane& clipping_plane) const {
    double eps = 1e-12;
    std::vector<Point> new_points = {};

    auto _signedDistance = [&](const Point& point) {
        Vector3D dist_vect = clipping_plane.plane_point.pointVectorToPoint(point);
        return dotProduct(dist_vect, clipping_plane.plane_normal);
    };
    auto _inside = [&](const Point& point) {
        return _signedDistance(point) >= -eps;
    };
    auto _intersection = [&](const Point& point_a, const Point& point_b) {
        double dist_a = _signedDistance(point_a);
        double dist_b = _signedDistance(point_b);
        double t = dist_a / (dist_a - dist_b);
        Point intersection_point = {
            point_a.x + t * (point_b.x - point_a.x),
            point_a.y + t * (point_b.y - point_a.y),
            point_a.z + t * (point_b.z - point_a.z)
        };
        return intersection_point;
    };
    auto _dedupeSequentialPoints = [&](const std::vector<Point>& points_to_dedupe) {
        std::vector<Point> deduped_points = {};
        if (points_to_dedupe.size() == 0) {
            return deduped_points;
        }
        for (std::size_t i = 0; i < points_to_dedupe.size(); ++i) {
            if (deduped_points.size() == 0 || points_to_dedupe[i].pointDistanceToPoint(deduped_points.back()) > eps) {
                deduped_points.push_back(points_to_dedupe[i]);
            }
        }
        if (deduped_points.size() > 1 && deduped_points[0].pointDistanceToPoint(deduped_points.back()) <= eps) {
            deduped_points.pop_back();
        }
        return deduped_points;
    };

    for (std::size_t i = 0; i < points.size(); ++i) {
        Point point_a = points[i];
        Point point_b = points[(i + 1) % points.size()];
        bool point_a_inside = _inside(point_a);
        bool point_b_inside = _inside(point_b);

        if (point_a_inside && point_b_inside) {
            new_points.push_back(point_b);
        } else if (point_a_inside && !point_b_inside) {
            Point cap_point = _intersection(point_a, point_b);
            new_points.push_back(cap_point);
        } else if (!point_a_inside && point_b_inside) {
            Point cap_point = _intersection(point_a, point_b);
            new_points.push_back(point_b);
            new_points.push_back(cap_point);
        }
    }
    new_points = _dedupeSequentialPoints(new_points);
    return Polygon{new_points};
}

Polyhedron Polyhedron::polyhedronClip(const Plane& clipping_plane) {
    double eps = 1e-12;
    std::vector<Point> new_points = {};
    std::vector<Point> cap_points = {};

    auto _signedDistance = [&](const Point& point) {
        Vector3D dist_vect = clipping_plane.plane_point.pointVectorToPoint(point);
        return dotProduct(dist_vect, clipping_plane.plane_normal);
    };
    auto _inside = [&](const Point& point) {
        return _signedDistance(point) >= -eps;
    };
    auto _intersection = [&](const Point& point_a, const Point& point_b) {
        double dist_a = _signedDistance(point_a);
        double dist_b = _signedDistance(point_b);
        double t = dist_a / (dist_a - dist_b);
        Point intersection_point = {
            point_a.x + t * (point_b.x - point_a.x),
            point_a.y + t * (point_b.y - point_a.y),
            point_a.z + t * (point_b.z - point_a.z)
        };
        return intersection_point;
    };
    auto _dedupeSequentialPoints = [&](const std::vector<Point>& points_to_dedupe) {
        std::vector<Point> deduped_points = {};
        if (points_to_dedupe.size() == 0) {
            return deduped_points;
        }
        for (std::size_t i = 0; i < points_to_dedupe.size(); ++i) {
            if (deduped_points.size() == 0 || points_to_dedupe[i].pointDistanceToPoint(deduped_points.back()) > eps) {
                deduped_points.push_back(points_to_dedupe[i]);
            }
        }
        if (deduped_points.size() > 1 && deduped_points[0].pointDistanceToPoint(deduped_points.back()) <= eps) {
            deduped_points.pop_back();
        }
        return deduped_points;
    };
    (void)_dedupeSequentialPoints;

    for (std::size_t i = 0; i < polygons.size(); ++i) {
        Polygon unclipped_polygon = polygons[i];
        std::vector<Point> points = unclipped_polygon.points;
        for (std::size_t j = 0; j < points.size(); ++j) {
            Point point_a = points[j];
            Point point_b = points[(j + 1) % points.size()];
            bool point_a_inside = _inside(point_a);
            bool point_b_inside = _inside(point_b);

            if (point_a_inside && point_b_inside) {
                new_points.push_back(point_b);
            } else if (point_a_inside && !point_b_inside) {
                Point cap_point = _intersection(point_a, point_b);
                new_points.push_back(cap_point);
                cap_points.push_back(cap_point);
            } else if (!point_a_inside && point_b_inside) {
                Point cap_point = _intersection(point_a, point_b);
                new_points.push_back(point_b);
                new_points.push_back(cap_point);
                cap_points.push_back(cap_point);
            }
        }
    }
    (void)new_points;

    Polygon cap_polygon = {cap_points};
    polygons.push_back(cap_polygon);
    return Polyhedron{polygons};
}

Polygon Polyhedron::polyhedronCrossSection(const Plane& cross_section_plane) const {
    std::vector<Point> intersection_points = {};
    Point plane_point = cross_section_plane.plane_point;
    Vector3D plane_normal = cross_section_plane.plane_normal;
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        Polygon polygon = polygons[i];
        std::vector<Segment> segments = polygon.polygonSegments();
        for (std::size_t j = 0; j < segments.size(); ++j) {
            Segment segment = segments[j];
            Point point_1 = segment.point_1;
            Point point_2 = segment.point_2;
            double distance_a = dotProduct(plane_point.pointVectorToPoint(point_1), plane_normal);
            double distance_b = dotProduct(plane_point.pointVectorToPoint(point_2), plane_normal);

            Point segment_point = point_1;
            double t = distance_a / (distance_a - distance_b);
            segment_point = {
                point_1.x + t * (point_2.x - point_1.x),
                point_1.y + t * (point_2.y - point_1.y),
                point_1.z + t * (point_2.z - point_1.z)
            };

            if (distance_a * distance_b < 0.0) {
                intersection_points.push_back(segment_point);
            }
        }
    }
    return Polygon{intersection_points};
}
