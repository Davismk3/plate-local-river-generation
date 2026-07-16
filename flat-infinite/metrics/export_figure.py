"""Export a publication-resolution still from the interactive terrain visualizer."""

import argparse
import csv
import json
import math
import platform
from pathlib import Path
import time

import numpy as np

from app import config
from app import visualizer
from cache.rivercache import ensurePlateRiverCache, packRiverCache
from helpers import ids


def _network_metrics(cache, cache_build_seconds):
    grid = cache["grid"]
    network = cache["network"]
    regions = cache["regions"]
    nodes = network[ids.nodes_id]
    segments = network[ids.segments_id]
    paths = network[ids.paths_id]

    node_type_counts = {}
    for node in nodes:
        node_type_counts[node.type] = node_type_counts.get(node.type, 0) + 1

    degrees = [0] * len(nodes)
    segment_lengths = []
    height_drops = []
    for segment in segments:
        degrees[int(segment.from_node)] += 1
        degrees[int(segment.to_node)] += 1
        segment_lengths.append(
            math.hypot(segment.b.x - segment.a.x, segment.b.y - segment.a.y)
            * config.PLT_SCALE
        )
        height_drops.append(float(segment.from_height) - float(segment.to_height))

    parent = list(range(len(nodes)))

    def find(node_id):
        while parent[node_id] != node_id:
            parent[node_id] = parent[parent[node_id]]
            node_id = parent[node_id]
        return node_id

    def union(a, b):
        root_a, root_b = find(a), find(b)
        if root_a != root_b:
            parent[root_b] = root_a

    for segment in segments:
        union(int(segment.from_node), int(segment.to_node))
    component_count = len({find(node_id) for node_id in range(len(nodes))})

    path_lengths = []
    for path in paths:
        length = 0.0
        for index in range(len(path) - 1):
            a = nodes[int(path[index])]
            b = nodes[int(path[index + 1])]
            length += math.hypot(b.x - a.x, b.y - a.y) * config.PLT_SCALE
        path_lengths.append(length)

    heights = grid[ids.heights_id]
    valid_mask = grid[ids.valid_mask_id]
    valid_heights = heights[valid_mask]
    land_cells = int(np.count_nonzero(valid_heights > config.SEA_LEVEL_FRACTION))
    valid_cells = int(valid_heights.size)
    polygons = regions["segment_polygons"]

    return {
        "plate_x": int(cache["plt_owner_idx"][0]),
        "plate_y": int(cache["plt_owner_idx"][1]),
        "cache_build_seconds": float(cache_build_seconds),
        "grid_valid_cells": valid_cells,
        "grid_land_fraction": land_cells / valid_cells if valid_cells else 0.0,
        "node_count": len(nodes),
        "segment_count": len(segments),
        "path_count": len(paths),
        "source_count": node_type_counts.get("source", 0),
        "sea_outlet_count": node_type_counts.get("outlet_sea", 0),
        "local_minimum_outlet_count": node_type_counts.get(
            "outlet_local_minimum", 0
        ),
        "channel_node_count": node_type_counts.get("channel", 0),
        "confluence_count": sum(degree >= 3 for degree in degrees),
        "connected_component_count": component_count,
        "total_channel_length_plate_cells": float(sum(segment_lengths)),
        "mean_segment_length_plate_cells": float(np.mean(segment_lengths))
        if segment_lengths
        else 0.0,
        "mean_path_length_plate_cells": float(np.mean(path_lengths))
        if path_lengths
        else 0.0,
        "max_path_length_plate_cells": float(max(path_lengths, default=0.0)),
        "mean_segment_height_drop": float(np.mean(height_drops))
        if height_drops
        else 0.0,
        "downhill_violation_count": sum(drop < -1e-9 for drop in height_drops),
        "region_count": len(polygons),
        "region_polygon_vertex_count": sum(
            len(region["polygon"].points) for region in polygons
        ),
    }


def _write_metrics(
    metrics_output,
    plate_metrics_output,
    metrics,
    per_plate_metrics,
):
    if metrics_output is not None:
        metrics_output = Path(metrics_output)
        metrics_output.parent.mkdir(parents=True, exist_ok=True)
        metrics_output.write_text(json.dumps(metrics, indent=2) + "\n")

    if plate_metrics_output is not None:
        plate_metrics_output = Path(plate_metrics_output)
        plate_metrics_output.parent.mkdir(parents=True, exist_ok=True)
        with plate_metrics_output.open("w", newline="") as output_file:
            writer = csv.DictWriter(
                output_file,
                fieldnames=list(per_plate_metrics[0]) if per_plate_metrics else [],
            )
            if per_plate_metrics:
                writer.writeheader()
                writer.writerows(per_plate_metrics)


def export_terrain_figure(
    output_path,
    width,
    height,
    plate_cells,
    resolution,
    metrics_output=None,
    plate_metrics_output=None,
    draw_bounding_box=False,
    supersample=3,
):
    try:
        import pygame
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "pygame is required for figure export. "
            "Install it with: python3 -m pip install pygame"
        ) from exc

    total_start = time.perf_counter()
    config.TERRAIN_PLATE_CELLS = float(plate_cells)
    config.TERRAIN_RESOLUTION = int(resolution)
    # Fill a wide paper figure more fully than the interactive HUD layout.
    config.TERRAIN_SCREEN_SCALE = 0.47
    config.TERRAIN_SCREEN_BASELINE = 0.72

    center_x, center_y = visualizer._configuredViewCenter()
    owner_indices = visualizer._orderedVisiblePlateOwnerIndices(center_x, center_y)
    caches = []
    cache_build_times = []
    for owner_idx in owner_indices:
        cache_start = time.perf_counter()
        caches.append(
            ensurePlateRiverCache(
                owner_idx,
                resolution=config.RIVER_GRID_RES,
                river_count=config.RIVER_COUNT,
                source_min_height=config.SOURCE_MIN_HEIGHT,
                min_source_spacing=config.MIN_SOURCE_SPACING,
                step_size=config.STEP_SIZE,
                max_steps=config.MAX_RIVER_STEPS,
            )
        )
        cache_build_times.append(time.perf_counter() - cache_start)

    pack_start = time.perf_counter()
    packed_cache = packRiverCache(owner_indices)
    pack_seconds = time.perf_counter() - pack_start
    sample_start = time.perf_counter()
    (
        river_segments,
        river_nodes,
        raw_heights,
        cube_heights,
        loaded_mask,
        _,
        _,
    ) = visualizer._terrainViewDataForLoadedPlates(
        center_x,
        center_y,
        owner_indices,
        caches,
        packed_cache,
        len(owner_indices),
    )
    sample_seconds = time.perf_counter() - sample_start

    # The interactive renderer keeps a small margin so branches enter and leave
    # the view smoothly while the camera moves. A static paper figure should not
    # draw that margin beyond the finite mesh shown in the frame.
    world_width = config.TERRAIN_PLATE_CELLS / config.PLT_SCALE
    half_width = world_width * 0.5
    min_x, max_x = center_x - half_width, center_x + half_width
    min_y, max_y = center_y - half_width, center_y + half_width

    def inside_mesh(node):
        x, y = visualizer._nodePoint(node)
        return min_x <= x <= max_x and min_y <= y <= max_y

    river_segments = [
        segment
        for segment in river_segments
        if inside_mesh(segment[0]) and inside_mesh(segment[1])
    ]
    river_nodes = [entry for entry in river_nodes if inside_mesh(entry[0])]

    render_start = time.perf_counter()
    pygame.init()
    try:
        # Render at an integer multiple of the target size and downscale
        # with a smooth filter: pygame's polygon/line rasterizer has no
        # anti-aliasing, so faceted terrain edges and jagged coastlines
        # otherwise show up directly in the saved image.
        supersample = max(1, int(supersample))
        render_width = int(width) * supersample
        render_height = int(height) * supersample

        surface = pygame.Surface((render_width, render_height))
        surface.fill(config.TERRAIN_BACKGROUND_COLOR)
        yaw = math.radians(config.TERRAIN_INITIAL_YAW_DEG)
        elevation = math.radians(config.TERRAIN_INITIAL_ELEVATION_DEG)

        faces = visualizer._terrainFaces(
            raw_heights,
            cube_heights,
            loaded_mask,
            render_width,
            render_height,
            yaw,
            elevation,
        )
        for _, color, points in faces:
            if len(points) >= 3:
                pygame.draw.polygon(surface, color, points)

        depth_buffer = visualizer._terrainDepthBuffer(
            faces, pygame, render_width, render_height
        )

        if draw_bounding_box:
            visualizer._drawCubeEdges(
                surface, render_width, render_height, pygame, yaw, elevation
            )

        # Keep river symbols legible when the figure is reduced to page width.
        render_scale = max(1.0, min(render_width, render_height) / 760.0)
        original_style = (
            config.RIVER_CHANNEL_WIDTH,
            config.RIVER_CHANNEL_HIGHLIGHT_WIDTH,
            config.RIVER_SOURCE_RADIUS,
            config.RIVER_LOCAL_MINIMUM_RADIUS,
            config.RIVER_SEA_OUTLET_RADIUS,
            config.RIVER_NODE_RADIUS,
        )
        config.RIVER_CHANNEL_WIDTH = max(2, round(2 * render_scale))
        config.RIVER_CHANNEL_HIGHLIGHT_WIDTH = max(1, round(render_scale))
        config.RIVER_SOURCE_RADIUS = max(2, round(2 * render_scale))
        config.RIVER_LOCAL_MINIMUM_RADIUS = max(2, round(2 * render_scale))
        config.RIVER_SEA_OUTLET_RADIUS = max(2, round(2 * render_scale))
        config.RIVER_NODE_RADIUS = max(3, round(3 * render_scale))
        try:
            visualizer._drawRiverGeometry(
                surface,
                pygame,
                river_segments,
                river_nodes,
                center_x,
                center_y,
                yaw,
                elevation,
                depth_buffer,
            )
        finally:
            (
                config.RIVER_CHANNEL_WIDTH,
                config.RIVER_CHANNEL_HIGHLIGHT_WIDTH,
                config.RIVER_SOURCE_RADIUS,
                config.RIVER_LOCAL_MINIMUM_RADIUS,
                config.RIVER_SEA_OUTLET_RADIUS,
                config.RIVER_NODE_RADIUS,
            ) = original_style

        if supersample > 1:
            surface = pygame.transform.smoothscale(surface, (int(width), int(height)))

        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        pygame.image.save(surface, str(output_path))
    finally:
        pygame.quit()

    render_seconds = time.perf_counter() - render_start

    per_plate_metrics = [
        _network_metrics(cache, cache_seconds)
        for cache, cache_seconds in zip(caches, cache_build_times)
    ]
    loaded_heights = raw_heights[loaded_mask]
    visible_segment_lengths = [
        math.hypot(segment[1].x - segment[0].x, segment[1].y - segment[0].y)
        * config.PLT_SCALE
        for segment in river_segments
    ]
    visible_node_types = {}
    for node, _ in river_nodes:
        visible_node_types[node.type] = visible_node_types.get(node.type, 0) + 1

    packed_bytes = sum(
        int(value.nbytes)
        for value in packed_cache.values()
        if isinstance(value, np.ndarray)
    )
    all_segments = sum(row["segment_count"] for row in per_plate_metrics)
    all_nodes = sum(row["node_count"] for row in per_plate_metrics)
    all_paths = sum(row["path_count"] for row in per_plate_metrics)
    all_sources = sum(row["source_count"] for row in per_plate_metrics)
    all_sea_outlets = sum(row["sea_outlet_count"] for row in per_plate_metrics)
    all_local_outlets = sum(
        row["local_minimum_outlet_count"] for row in per_plate_metrics
    )
    all_confluences = sum(row["confluence_count"] for row in per_plate_metrics)
    all_components = sum(
        row["connected_component_count"] for row in per_plate_metrics
    )
    all_length = sum(
        row["total_channel_length_plate_cells"] for row in per_plate_metrics
    )
    all_violations = sum(
        row["downhill_violation_count"] for row in per_plate_metrics
    )

    metrics = {
        "metadata": {
            "seed": int(config.SEED),
            "python_version": platform.python_version(),
            "numpy_version": np.__version__,
            "platform": platform.platform(),
            "machine": platform.machine(),
        },
        "figure_domain": {
            "image_width_pixels": int(width),
            "image_height_pixels": int(height),
            "plate_cell_width": float(plate_cells),
            "world_coordinate_width": float(world_width),
            "terrain_resolution": int(resolution),
            "terrain_sample_count": int(loaded_heights.size),
            "visible_plate_count": len(owner_indices),
        },
        "generation_parameters": {
            "river_grid_resolution_per_plate": int(config.RIVER_GRID_RES),
            "maximum_requested_paths_per_plate": int(config.RIVER_COUNT),
            "source_minimum_height": float(config.SOURCE_MIN_HEIGHT),
            "minimum_source_spacing_grid_cells": int(config.MIN_SOURCE_SPACING),
            "river_step_size_grid_cells": int(config.STEP_SIZE),
            "maximum_river_steps": int(config.MAX_RIVER_STEPS),
            "sea_level": float(config.SEA_LEVEL_FRACTION),
        },
        "terrain": {
            "minimum_height": float(np.min(loaded_heights)),
            "maximum_height": float(np.max(loaded_heights)),
            "mean_height": float(np.mean(loaded_heights)),
            "standard_deviation_height": float(np.std(loaded_heights)),
            "height_percentile_05": float(np.percentile(loaded_heights, 5)),
            "height_percentile_50": float(np.percentile(loaded_heights, 50)),
            "height_percentile_95": float(np.percentile(loaded_heights, 95)),
            "land_sample_count": int(
                np.count_nonzero(loaded_heights > config.SEA_LEVEL_FRACTION)
            ),
            "land_fraction": float(
                np.mean(loaded_heights > config.SEA_LEVEL_FRACTION)
            ),
            "ocean_fraction": float(
                np.mean(loaded_heights <= config.SEA_LEVEL_FRACTION)
            ),
        },
        "all_visible_plate_networks": {
            "node_count": all_nodes,
            "segment_count": all_segments,
            "accepted_path_count": all_paths,
            "requested_path_count": int(config.RIVER_COUNT) * len(owner_indices),
            "path_acceptance_fraction": all_paths
            / (int(config.RIVER_COUNT) * len(owner_indices)),
            "source_count": all_sources,
            "sea_outlet_count": all_sea_outlets,
            "local_minimum_outlet_count": all_local_outlets,
            "confluence_count": all_confluences,
            "connected_component_count": all_components,
            "total_channel_length_plate_cells": float(all_length),
            "mean_channel_length_per_plate_cells": float(
                all_length / len(owner_indices)
            ),
            "downhill_violation_count": all_violations,
            "downhill_compliance_fraction": 1.0
            if all_segments == 0
            else 1.0 - all_violations / all_segments,
            "region_count": sum(row["region_count"] for row in per_plate_metrics),
            "region_polygon_vertex_count": sum(
                row["region_polygon_vertex_count"] for row in per_plate_metrics
            ),
        },
        "clipped_figure_network": {
            "node_count": len(river_nodes),
            "segment_count": len(river_segments),
            "node_type_counts": visible_node_types,
            "total_channel_length_plate_cells": float(sum(visible_segment_lengths)),
            "mean_segment_length_plate_cells": float(
                np.mean(visible_segment_lengths)
            )
            if visible_segment_lengths
            else 0.0,
        },
        "packed_cache": {
            "array_count": sum(
                isinstance(value, np.ndarray) for value in packed_cache.values()
            ),
            "bytes": packed_bytes,
            "mebibytes": packed_bytes / (1024.0 * 1024.0),
            "bytes_per_segment": packed_bytes / all_segments if all_segments else 0.0,
        },
        "timings": {
            "cache_build_total_seconds": float(sum(cache_build_times)),
            "cache_build_mean_seconds": float(np.mean(cache_build_times)),
            "cache_build_median_seconds": float(np.median(cache_build_times)),
            "cache_build_minimum_seconds": float(min(cache_build_times)),
            "cache_build_maximum_seconds": float(max(cache_build_times)),
            "pack_seconds": float(pack_seconds),
            "terrain_and_geometry_sampling_seconds": float(sample_seconds),
            "render_and_save_seconds": float(render_seconds),
            "total_export_seconds": float(time.perf_counter() - total_start),
        },
        "per_plate": per_plate_metrics,
    }
    _write_metrics(
        metrics_output,
        plate_metrics_output,
        metrics,
        per_plate_metrics,
    )

    print(
        f"Saved {output_path} ({width}x{height}, "
        f"{plate_cells} plate cells, {len(owner_indices)} visible plates)"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--width", type=int, default=2400)
    parser.add_argument("--height", type=int, default=1600)
    parser.add_argument("--plate-cells", type=float, default=2.0)
    parser.add_argument("--resolution", type=int, default=220)
    parser.add_argument("--metrics-output", type=Path)
    parser.add_argument("--plate-metrics-output", type=Path)
    parser.add_argument("--draw-bounding-box", action="store_true",
                         help="draw the debug view-frustum wireframe (off by default)")
    parser.add_argument("--supersample", type=int, default=3,
                         help="render at this integer multiple of --width/--height and "
                              "downscale for anti-aliasing (1 disables supersampling)")
    args = parser.parse_args()
    export_terrain_figure(
        args.output,
        args.width,
        args.height,
        args.plate_cells,
        args.resolution,
        args.metrics_output,
        args.plate_metrics_output,
        args.draw_bounding_box,
        args.supersample,
    )


if __name__ == "__main__":
    main()
