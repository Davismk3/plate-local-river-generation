
"""
This entire script can be avoided if using dictionaries. I've gotten into the habbit of not using dictionaries since
they are not Numba-friendly. This approach should also be better for performance. 
"""

# Pointwise Fields
crust_weight_id = 0
islands_weight_id = 1
land_weight_id = 2
mountains_weight_id = 3
plateau_weight_id = 4
drift_scalar_id = 5
drift_vector_id = 6

# Platewise Grid & Plate Network
plt_owner_idx_id = 0
resolution_id = 1

# Platewise Grid
polygon_id = 2
world_points_id = 3
plate_points_id = 4
heights_id = 5
valid_mask_id = 6
border_safe_mask_id = 7
lake_safe_mask_id = 8

# Plate Network
river_count_id = 2
source_pixels_id = 3
nodes_id = 4
segments_id = 5
paths_id = 6
