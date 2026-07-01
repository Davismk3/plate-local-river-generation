# Plate-Local River Generation Algorithm (PL-RGA)

I may rewrite the PL-RGA in C++ for this repository since this language is a far better choice for it. In the meantime, I have gone ahead and created the core math helpers for the PL-RGA, which is highly dependent on computational geometry and linear algebra. 

`geometry.hpp` contains the computational geometry for points, segments, polygons, and polyhedrons; each object contains very useful methods such as clipping polygons and polyhedrons. 

`linear_algebra.hpp` contains the vector helpers such as the cross, dot, and other things. 
