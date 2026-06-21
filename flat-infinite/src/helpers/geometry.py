import numpy as np
from dataclasses import dataclass

@dataclass
class Point:
    x: float
    y: float 

    def distance_to_point(self, nbr_point):
        """Distance to a point."""
        return np.sqrt(
            (self.x - nbr_point.x)**2 + 
            (self.y - nbr_point.y)**2
            )
    
    def vector_to_point(self, nbr_point):
        return (
            nbr_point.x - self.x,
            nbr_point.y - self.y,
            )

    def __repr__(self):
        return f'Point: ({self.x}, {self.y})'


@dataclass
class Segment:
    a: object
    b: object

    def __init__(self, a, b):
        self.a = a
        self.b = b
    
    def length(self):
        return self.a.distance_to_point(self.b)
    
    def point_on_segment(self, nbr_point):
        eps = 1e-8
        seg_vect = (self.a.x - self.b.x, self.a.y - self.b.y)
        nbr_vect = (self.a.x - nbr_point.x, self.a.y - nbr_point.y)
        
        seg_norm = np.linalg.norm(seg_vect)
        nbr_norm = np.linalg.norm(nbr_vect)
        
        condition1 = np.dot(seg_vect, nbr_vect) / (seg_norm * nbr_norm) >= 1 - eps
        condition2 = self.a.distance_to_point(nbr_point) <= self.length()
        
        return condition1 and condition2
    
    def midpoint(self):
        return Point(0.5 * (self.a.x + self.b.x), 0.5 * (self.a.y + self.b.y))
    
    def segment_intersects_segment(self, nbr_segment):
        def orientation(a, b, c):
            """
            The sign of this cross product reveals which side of line <-a-b-> the point c is on.
            
            If, for two points of a given segment, the signs are opposite, then that segment must cross line <-a-b->.
            """
            return np.cross(a.vector_to_point(nbr_point=b), a.vector_to_point(nbr_point=c))

        def on_segment(a, p, b):
            return (
                min(a.x, b.x) <= p.x <= max(a.x, b.x)
                and min(a.y, b.y) <= p.y <= max(a.y, b.y)
            )

        a, b = self.a, self.b
        c, d = nbr_segment.a, nbr_segment.b

        o1 = orientation(a, b, c)
        o2 = orientation(a, b, d)
        o3 = orientation(c, d, a)
        o4 = orientation(c, d, b)

        if o1 == 0 and on_segment(a, c, b):
            return True
        if o2 == 0 and on_segment(a, d, b):
            return True
        if o3 == 0 and on_segment(c, a, d):
            return True
        if o4 == 0 and on_segment(c, b, d):
            return True

        return ((o1 > 0) != (o2 > 0)) and ((o3 > 0) != (o4 > 0))

    def points(self):
        return (self.a, self.b)

    def t_param_for_point(self, nbr_point):
        segment_vect = self.a.vector_to_point(self.b)
        nbr_point_vect = self.a.vector_to_point(nbr_point)
        denominator = np.dot(segment_vect, segment_vect)

        if denominator <= 1e-18:
            return 0.0
        
        t = np.dot(nbr_point_vect, segment_vect) / denominator
        t = max(0.0, min(1.0, t))

        return t

    def closest_point(self, nbr_point):
        t = self.t_param_for_point(nbr_point=nbr_point)

        return Point(
            x=self.a.x + t * (self.b.x - self.a.x),
            y=self.a.y + t * (self.b.y - self.a.y),
            )

    def distance_to_point(self, nbr_point):
        closest = self.closest_point(nbr_point=nbr_point)
        
        distance = closest.distance_to_point(nbr_point=nbr_point)
        
        return distance

    def __repr__(self):
        return f'Segment: ({self.a.x}, {self.a.y})-({self.b.x}, {self.b.y})'
    

@dataclass
class Polygon:
    points: tuple

    def perimeter(self):
        perim = 0.0
        if len(self.points) == 1: 
            return perim  # point fallback
        if len(self.points) == 2: 
            a = self.points[0]
            b = self.points[1]
            perim = Segment(a=a, b=b).length()  # segment fallback
            return perim 
        
        for i in range(len(self.points)):
            a = self.points[i]
            if i == len(self.points) - 1: 
                b = self.points[0]
            else:
                b = self.points[i + 1]

            perim += Segment(a=a, b=b).length()

        return perim
    
    def area(self):
        if len(self.points) <= 2: 
            return 0.0
        
        lhs = 0.0
        rhs = 0.0

        for i in range(len(self.points)):
            next_coord = i + 1
            if i + 1 > len(self.points) - 1: 
                next_coord = 0

            lhs += self.points[i].x * self.points[next_coord].y
            rhs += self.points[i].y * self.points[next_coord].x
        
        return 0.5 * abs(lhs - rhs)
    
    def segments(self):
        segments = []
        if len(self.points) <= 1: 
            return segments  # no segments fallback
        if len(self.points) == 2: 
            a = self.points[0]
            b = self.points[1]
            segments.append(Segment(a=a, b=b))  # one segment fallback
            return segments
        
        for i in range(len(self.points)):
            a = self.points[i]
            if i == len(self.points) - 1: 
                b = self.points[0]
            else:
                b = self.points[i + 1]

            segments.append(Segment(a=a, b=b))

        return segments

    def point_inside(self, nbr_point): 
        """
        Uses the even-odd rule for determining if a point is inside a polygon.
        """
        inf = 1e8
        reference_ray = Segment(
            a=nbr_point,
            b=Point(
                x=nbr_point.x,
                y=nbr_point.y + inf,
            )
        )

        count = 0
        for segment in self.segments(): 
            if segment.segment_intersects_segment(nbr_segment=reference_ray):
                count += 1

        if count == 0:
            return False
        elif count % 2 != 0: 
            return True
        else: 
            return False
    
    def clip_polygon(
        self, 
        plane_point,  # point along the clipping plane
        normal_dir,  # defines the plane for plane_point; note directionality polygon_center<-plane_point
        ):
        """
        This is the Sutherland-Hodgman algorithm.
        """
        eps=1e-8
        points = []

        def _signed_distance(point):
            return (
                (point.x - plane_point.x) * normal_dir[0]
                + (point.y - plane_point.y) * normal_dir[1]
            )

        def _inside(point):
            return _signed_distance(point) >= -eps

        def _intersection(a, b):
            da = _signed_distance(a)
            db = _signed_distance(b)
            t = da / (da - db + 1e-12)
            return Point(
                x=a.x + t * (b.x - a.x),
                y=a.y + t * (b.y - a.y),
            )

        def _dedupe_sequential_points(points):
            if not points:
                return []

            deduped = []
            for point in points:
                if not deduped or point.distance_to_point(deduped[-1]) > eps:
                    deduped.append(point)

            if len(deduped) > 1 and deduped[0].distance_to_point(deduped[-1]) <= eps:
                deduped.pop()

            return deduped

        for i in range(len(self.points)):
            a = self.points[i]
            b = self.points[(i + 1) % len(self.points)]

            if _inside(a) and _inside(b):
                points.append(b)
            elif _inside(a) and not _inside(b):
                points.append(_intersection(a, b))
            elif not _inside(a) and _inside(b):
                points.append(_intersection(a, b))
                points.append(b)

        points = tuple(_dedupe_sequential_points(points))

        return Polygon(points=points)

    def border_distance_to_point(self, nbr_point):
        return min(
            segment.distance_to_point(nbr_point)
            for segment in self.segments()
        )

    def dictionary(self):
        return {
            'polygon': self,
            'segments': self.segments(),
            'points': [segment.points() for segment in self.segments()]
        }

    def __repr__(self):
        return f'Polygon: {self.segments()},'
