#pragma once

#include <vector>

#include "linear_algebra.hpp"

// Point [24 bytes]
struct Point {
    double x;
    double y;
    double z;

    double pointDistanceToPoint(const Point& other_point) const;
    Vector3D pointVectorToPoint(const Point& other_point) const;
};

// Segment [48 bytes]
struct Segment {
    Point point_1;
    Point point_2;

    double segmentLength() const;
    Point segmetMidpoint() const;
    bool pointOnSegment(const Point& point_3) const;
    bool segmentIntersectsSegment(const Segment& other_segment) const;
    double segmentDistanceToPoint(const Point& point_3) const;
};

// Plane [48 bytes]
struct Plane {
    Point plane_point;
    Vector3D plane_normal;
};

// Polygon (2D) [24 bytes Per Point]
struct Polygon {
    std::vector<Point> points;

    Vector3D polygonNormal() const;
    bool polygonIsValid() const;
    double polygonPerimeter() const;
    double polygonArea() const;
    std::vector<Segment> polygonSegments() const;
    bool polygonEnclosesPoint(const Point& point) const;
    double polygonBorderDistanceToPoint(const Point& point) const;
    Polygon polygonClip(const Plane& clipping_plane) const;
};

// Polyhedron (3D) [24 bytes Per Point Per Polygon]
struct Polyhedron {
    std::vector<Polygon> polygons;

    Polyhedron polyhedronClip(const Plane& clipping_plane);
    Polygon polyhedronCrossSection(const Plane& cross_section_plane) const;
};
