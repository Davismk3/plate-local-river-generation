import math
import threading
import numpy as np
from app import config
from cache.rivercache import (
    ensurePlateRiverCache,
    heightField,
    packRiverCache,
    packedCacheTuple,
    plateOwnerIndex,
)
from helpers import ids
from plateownership import plateCenter

"""
This module is solely for visualization, and has no part in the PL-RGA. 
"""


def runConfiguredVisualizer():
    center_x, center_y = _configuredViewCenter()
    runTerrainVisualizer(center_x, center_y)


def runTerrainVisualizer(center_x, center_y):
    try:
        import pygame
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "pygame is required for the terrain visualizer. "
            "Install it with: python3 -m pip install pygame"
        ) from exc

    pygame.init()
    pygame.display.set_caption("WorldGen plate terrain")
    screen = pygame.display.set_mode(
        (config.TERRAIN_WINDOW_WIDTH, config.TERRAIN_WINDOW_HEIGHT)
    )
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("Menlo", 15)

    terrain_loader = _TerrainLoadWorker()
    active_request_id = 0
    pending_load_request = True
    last_movement_ms = pygame.time.get_ticks()
    load_request_interval_ms = int(
        1000.0 * getattr(config, "TERRAIN_LOAD_REQUEST_INTERVAL_SECONDS", 0.12)
    )
    terrain_data = None
    camera_yaw = math.radians(config.TERRAIN_INITIAL_YAW_DEG)
    camera_elevation = math.radians(config.TERRAIN_INITIAL_ELEVATION_DEG)
    min_elevation = math.radians(config.TERRAIN_MIN_ELEVATION_DEG)
    max_elevation = math.radians(config.TERRAIN_MAX_ELEVATION_DEG)
    dragging = False
    drag_last_pos = None
    running = True
    last_frame_ms = pygame.time.get_ticks()

    try:
        while running:
            now_ms = pygame.time.get_ticks()
            delta_seconds = min((now_ms - last_frame_ms) / 1000.0, 0.1)
            last_frame_ms = now_ms
            moved = False
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_ESCAPE:
                        running = False
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    dragging = True
                    drag_last_pos = event.pos
                elif event.type == pygame.MOUSEBUTTONUP and event.button == 1:
                    dragging = False
                    drag_last_pos = None
                elif event.type == pygame.MOUSEMOTION:
                    left_button_down = len(event.buttons) > 0 and event.buttons[0]
                    if dragging or left_button_down:
                        if drag_last_pos is None:
                            drag_last_pos = event.pos
                        dx = event.pos[0] - drag_last_pos[0]
                        dy = event.pos[1] - drag_last_pos[1]
                        drag_last_pos = event.pos
                        camera_yaw -= dx * config.TERRAIN_MOUSE_ROTATION_SPEED
                        camera_elevation += dy * config.TERRAIN_MOUSE_ROTATION_SPEED
                        camera_elevation = max(
                            min_elevation, min(max_elevation, camera_elevation)
                        )
                    else:
                        drag_last_pos = None

            mouse_buttons = pygame.mouse.get_pressed()
            left_mouse_pressed = len(mouse_buttons) > 0 and mouse_buttons[0]
            if not dragging and left_mouse_pressed:
                dragging = True
                drag_last_pos = pygame.mouse.get_pos()
            elif dragging and not left_mouse_pressed:
                dragging = False
                drag_last_pos = None

            center_x, center_y, moved = _applyKeyboardMovement(
                pygame,
                center_x,
                center_y,
                delta_seconds,
            )
            if moved:
                pending_load_request = True
                last_movement_ms = pygame.time.get_ticks()

            now_ms = pygame.time.get_ticks()
            if (
                pending_load_request
                and last_movement_ms is not None
                and now_ms - last_movement_ms >= load_request_interval_ms
            ):
                active_request_id = terrain_loader.request(center_x, center_y)
                pending_load_request = False

            loaded_data = terrain_loader.data(active_request_id)
            if loaded_data is not None:
                terrain_data = loaded_data

            if terrain_data is None:
                river_segments, river_nodes, raw_heights, cube_heights, loaded_mask = (
                    [],
                    [],
                    None,
                    None,
                    None,
                )
                loaded_plate_count = 0
                active_plate_count = 0
            else:
                (
                    river_segments,
                    river_nodes,
                    raw_heights,
                    cube_heights,
                    loaded_mask,
                    loaded_plate_count,
                    active_plate_count,
                ) = terrain_data

            _drawTerrainScene(
                screen,
                raw_heights,
                cube_heights,
                loaded_mask,
                center_x,
                center_y,
                font,
                pygame,
                river_segments,
                river_nodes,
                camera_yaw,
                camera_elevation,
                loaded_plate_count,
                active_plate_count,
            )
            pygame.display.flip()
            clock.tick(config.TERRAIN_FPS)
    finally:
        terrain_loader.stop()
        pygame.quit()


def _applyKeyboardMovement(pygame, center_x, center_y, delta_seconds):
    keys = pygame.key.get_pressed()
    move_x = int(keys[pygame.K_RIGHT]) - int(keys[pygame.K_LEFT])
    move_y = int(keys[pygame.K_DOWN]) - int(keys[pygame.K_UP])

    if move_x == 0 and move_y == 0:
        return center_x, center_y, False

    move_length = math.sqrt(move_x * move_x + move_y * move_y)
    move_cells_per_second = getattr(
        config,
        "TERRAIN_MOVE_CELLS_PER_SECOND",
        config.TERRAIN_SHIFT_CELLS * config.TERRAIN_FPS,
    )
    move_world = move_cells_per_second * delta_seconds / config.PLT_SCALE

    return (
        center_x + move_x * move_world / move_length,
        center_y + move_y * move_world / move_length,
        True,
    )


class _TerrainLoadWorker:
    def __init__(self):
        self._condition = threading.Condition()
        self._request_id = 0
        self._loaded_id = 0
        self._requested_center = (0.0, 0.0)
        self._data = None
        self._error = None
        self._stop_requested = False
        self._thread = threading.Thread(
            target=self._run,
            name="platevisualizer-terrain-loader",
            daemon=True,
        )
        self._thread.start()

    def request(self, center_x, center_y):
        with self._condition:
            self._request_id += 1
            self._requested_center = (float(center_x), float(center_y))
            self._data = None
            self._error = None
            self._condition.notify()
            return self._request_id

    def data(self, request_id):
        with self._condition:
            if self._loaded_id != int(request_id):
                return None
            if self._error is not None:
                raise self._error
            return self._data

    def stop(self):
        with self._condition:
            self._stop_requested = True
            self._condition.notify()
        self._thread.join(timeout=1.0)

    def _run(self):
        handled_request_id = 0
        while True:
            with self._condition:
                while (
                    not self._stop_requested
                    and self._request_id == handled_request_id
                ):
                    self._condition.wait()
                if self._stop_requested:
                    return
                request_id = self._request_id
                center_x, center_y = self._requested_center

            try:
                self._loadIncrementalView(request_id, center_x, center_y)
                error = None
            except Exception as exc:
                error = exc

            if error is not None:
                with self._condition:
                    handled_request_id = request_id
                    if request_id == self._request_id:
                        self._loaded_id = request_id
                        self._data = None
                        self._error = error
            else:
                handled_request_id = request_id

    def _loadIncrementalView(self, request_id, center_x, center_y):
        if not config.TERRAIN_VIEW_RIVERS:
            self._publishData(request_id, _terrainViewData(center_x, center_y))
            return

        owner_indices = _orderedVisiblePlateOwnerIndices(center_x, center_y)
        loaded_owner_indices = []
        loaded_caches = []
        active_plate_count = len(owner_indices)

        for owner_idx in owner_indices:
            if not self._isCurrentRequest(request_id):
                return

            cache = ensurePlateRiverCache(
                owner_idx,
                resolution=config.RIVER_GRID_RES,
                river_count=config.RIVER_COUNT,
                source_min_height=config.SOURCE_MIN_HEIGHT,
                min_source_spacing=config.MIN_SOURCE_SPACING,
                step_size=config.STEP_SIZE,
                max_steps=config.MAX_RIVER_STEPS,
            )
            loaded_owner_indices.append(owner_idx)
            loaded_caches.append(cache)

            if not self._isCurrentRequest(request_id):
                return

            packed_cache = packRiverCache(loaded_owner_indices)
            data = _terrainViewDataForLoadedPlates(
                center_x,
                center_y,
                loaded_owner_indices,
                loaded_caches,
                packed_cache,
                active_plate_count,
            )
            self._publishData(request_id, data)

    def _isCurrentRequest(self, request_id):
        with self._condition:
            return not self._stop_requested and request_id == self._request_id

    def _publishData(self, request_id, data):
        with self._condition:
            if request_id == self._request_id:
                self._loaded_id = request_id
                self._data = data
                self._error = None


def sampleTerrainGrid(center_x, center_y, river_caches=None):
    raw_heights, cube_heights, _ = _sampleTerrainGridForLoadedPlates(
        center_x,
        center_y,
        river_caches,
    )
    return raw_heights, cube_heights


def _sampleTerrainGridForLoadedPlates(
    center_x,
    center_y,
    river_caches=None,
    loaded_owner_indices=None,
):
    resolution = max(2, int(config.TERRAIN_RESOLUTION))
    raw_heights = np.zeros((resolution, resolution), dtype=np.float64)
    cube_heights = np.zeros((resolution, resolution), dtype=np.float64)
    loaded_mask = np.ones((resolution, resolution), dtype=bool)
    loaded_owner_set = None
    if loaded_owner_indices is not None:
        loaded_owner_set = {_ownerKey(owner_idx) for owner_idx in loaded_owner_indices}
        loaded_mask[:, :] = False

    world_width = config.TERRAIN_PLATE_CELLS / config.PLT_SCALE
    world_height = world_width
    min_x = center_x - world_width * 0.5
    min_y = center_y - world_height * 0.5

    for row in range(resolution):
        y = min_y + row * world_height / (resolution - 1)
        for col in range(resolution):
            x = min_x + col * world_width / (resolution - 1)
            if loaded_owner_set is not None:
                owner_idx = plateOwnerIndex(float(x), float(y))
                if _ownerKey(owner_idx) not in loaded_owner_set:
                    continue
                loaded_mask[row, col] = True

            height = float(heightField(x, y, river_caches))
            raw_heights[row, col] = height
            cube_heights[row, col] = max(
                0.0, min(1.0, height * config.TERRAIN_HEIGHT_SCALE)
            )

    return raw_heights, cube_heights, loaded_mask


def _terrainViewData(center_x, center_y):
    raw_heights, cube_heights, loaded_mask = _sampleTerrainGridForLoadedPlates(
        center_x,
        center_y,
        None,
    )
    return [], [], raw_heights, cube_heights, loaded_mask, 0, 0


def _terrainViewDataForLoadedPlates(
    center_x,
    center_y,
    loaded_owner_indices,
    loaded_caches,
    packed_cache,
    active_plate_count,
):
    packed_river_cache = packedCacheTuple(packed_cache)
    river_segments, river_nodes = _riverGeometryFromLoadedCaches(
        center_x,
        center_y,
        loaded_caches,
    )
    raw_heights, cube_heights, loaded_mask = _sampleTerrainGridForLoadedPlates(
        center_x,
        center_y,
        packed_river_cache,
        loaded_owner_indices,
    )
    return (
        river_segments,
        river_nodes,
        raw_heights,
        cube_heights,
        loaded_mask,
        len(loaded_owner_indices),
        active_plate_count,
    )


def _riverGeometryFromLoadedCaches(center_x, center_y, loaded_caches):
    world_width = float(config.TERRAIN_PLATE_CELLS) / float(config.PLT_SCALE)
    min_x = float(center_x) - world_width * 0.5
    max_x = float(center_x) + world_width * 0.5
    min_y = float(center_y) - world_width * 0.5
    max_y = float(center_y) + world_width * 0.5
    margin = world_width * config.RIVER_VIEW_MARGIN_FRACTION

    segments = []
    nodes = []
    for cache in loaded_caches:
        network = cache["network"]
        for segment in network[ids.segments_id]:
            if not _segmentIntersectsBounds(
                _nodePoint(segment.a),
                _nodePoint(segment.b),
                min_x,
                max_x,
                min_y,
                max_y,
                margin,
            ):
                continue
            segments.append((segment.a, segment.b, cache["plt_owner_idx"]))
        for node in network[ids.nodes_id]:
            x, y = _nodePoint(node)
            if (
                min_x - margin <= x <= max_x + margin
                and min_y - margin <= y <= max_y + margin
            ):
                nodes.append((node, cache["plt_owner_idx"]))

    return segments, nodes


def _segmentIntersectsBounds(a, b, min_x, max_x, min_y, max_y, margin):
    ax, ay = a
    bx, by = b
    if max(ax, bx) < min_x - margin or min(ax, bx) > max_x + margin:
        return False
    if max(ay, by) < min_y - margin or min(ay, by) > max_y + margin:
        return False
    return True


def _ownerKey(plt_owner_idx):
    return int(plt_owner_idx[0]), int(plt_owner_idx[1])


def _orderedVisiblePlateOwnerIndices(center_x, center_y):
    center_owner_key = _ownerKey(plateOwnerIndex(float(center_x), float(center_y)))
    owner_counts = {}

    resolution = max(2, int(config.TERRAIN_RESOLUTION))
    world_width = config.TERRAIN_PLATE_CELLS / config.PLT_SCALE
    world_height = world_width
    min_x = center_x - world_width * 0.5
    min_y = center_y - world_height * 0.5

    for row in range(resolution):
        y = min_y + row * world_height / (resolution - 1)
        for col in range(resolution):
            x = min_x + col * world_width / (resolution - 1)
            owner_key = _ownerKey(plateOwnerIndex(float(x), float(y)))
            owner_counts[owner_key] = owner_counts.get(owner_key, 0) + 1

    owner_indices = list(owner_counts)
    owner_indices.sort(
        key=lambda idx: (
            idx != center_owner_key,
            -owner_counts[idx],
            (idx[0] - center_owner_key[0]) * (idx[0] - center_owner_key[0])
            + (idx[1] - center_owner_key[1]) * (idx[1] - center_owner_key[1]),
            idx[1],
            idx[0],
        )
    )
    return owner_indices


def _drawTerrainScene(
    screen,
    raw_heights,
    cube_heights,
    loaded_mask,
    center_x,
    center_y,
    font,
    pygame,
    river_segments,
    river_nodes,
    camera_yaw,
    camera_elevation,
    loaded_plate_count,
    active_plate_count,
    ):
    width, height = screen.get_size()
    screen.fill(config.TERRAIN_BACKGROUND_COLOR)

    if raw_heights is not None and cube_heights is not None:
        faces = _terrainFaces(
            raw_heights,
            cube_heights,
            loaded_mask,
            width,
            height,
            camera_yaw,
            camera_elevation,
        )
        for _, color, points in faces:
            if len(points) >= 3:
                pygame.draw.polygon(screen, color, points)

    _drawCubeEdges(screen, width, height, pygame, camera_yaw, camera_elevation)
    if raw_heights is not None and cube_heights is not None:
        _drawRiverGeometry(
            screen,
            pygame,
            river_segments,
            river_nodes,
            center_x,
            center_y,
            camera_yaw,
            camera_elevation,
        )
    _drawTerrainHud(
        screen,
        center_x,
        center_y,
        raw_heights,
        loaded_mask,
        font,
        pygame,
        river_segments,
        river_nodes,
        camera_yaw,
        camera_elevation,
        loaded_plate_count,
        active_plate_count,
    )


def _terrainFaces(
    raw_heights,
    cube_heights,
    loaded_mask,
    screen_width,
    screen_height,
    camera_yaw,
    camera_elevation,
):
    resolution = cube_heights.shape[0]
    faces = []
    for row in range(resolution - 1):
        z0 = _gridCoord(row, resolution)
        z1 = _gridCoord(row + 1, resolution)
        for col in range(resolution - 1):
            if loaded_mask is not None and not (
                loaded_mask[row, col]
                and loaded_mask[row, col + 1]
                and loaded_mask[row + 1, col + 1]
                and loaded_mask[row + 1, col]
            ):
                continue
            x0 = _gridCoord(col, resolution)
            x1 = _gridCoord(col + 1, resolution)
            p0 = (x0, cube_heights[row, col], z0)
            p1 = (x1, cube_heights[row, col + 1], z0)
            p2 = (x1, cube_heights[row + 1, col + 1], z1)
            p3 = (x0, cube_heights[row + 1, col], z1)
            h0 = raw_heights[row, col]
            h1 = raw_heights[row, col + 1]
            h2 = raw_heights[row + 1, col + 1]
            h3 = raw_heights[row + 1, col]
            faces.append(
                _terrainTriangle(
                    (p0, p1, p2),
                    (h0, h1, h2),
                    screen_width,
                    screen_height,
                    camera_yaw,
                    camera_elevation,
                )
            )
            faces.append(
                _terrainTriangle(
                    (p0, p2, p3),
                    (h0, h2, h3),
                    screen_width,
                    screen_height,
                    camera_yaw,
                    camera_elevation,
                )
            )

    faces.sort(key=lambda face: face[0])
    return faces


def _terrainTriangle(
    points_3d, heights, screen_width, screen_height, camera_yaw, camera_elevation
):
    projected = [
        _projectPoint(point, screen_width, screen_height, camera_yaw, camera_elevation)
        for point in points_3d
    ]
    depth = sum(point[2] for point in projected) / len(projected)
    points = [(point[0], point[1]) for point in projected]
    avg_height = sum(float(height) for height in heights) / len(heights)
    return depth, _terrainColor(avg_height), points


def _drawCubeEdges(
    screen, screen_width, screen_height, pygame, camera_yaw, camera_elevation
):
    corners = [
        (-1.0, 0.0, -1.0),
        (1.0, 0.0, -1.0),
        (1.0, 0.0, 1.0),
        (-1.0, 0.0, 1.0),
        (-1.0, 1.0, -1.0),
        (1.0, 1.0, -1.0),
        (1.0, 1.0, 1.0),
        (-1.0, 1.0, 1.0),
    ]
    projected = [
        _projectPoint(point, screen_width, screen_height, camera_yaw, camera_elevation)
        for point in corners
    ]
    points = [(point[0], point[1]) for point in projected]
    edges = (
        (0, 1),
        (1, 2),
        (2, 3),
        (3, 0),
        (4, 5),
        (5, 6),
        (6, 7),
        (7, 4),
        (0, 4),
        (1, 5),
        (2, 6),
        (3, 7),
    )
    for a, b in edges:
        pygame.draw.line(screen, config.TERRAIN_EDGE_COLOR, points[a], points[b], 1)


def _drawRiverGeometry(
    screen,
    pygame,
    river_segments,
    river_nodes,
    center_x,
    center_y,
    camera_yaw,
    camera_elevation,
):
    width, height = screen.get_size()

    for from_node, to_node, _ in river_segments:
        p0 = _riverNodeToScreen(
            from_node, center_x, center_y, width, height, camera_yaw, camera_elevation
        )
        p1 = _riverNodeToScreen(
            to_node, center_x, center_y, width, height, camera_yaw, camera_elevation
        )
        if p0 is None or p1 is None:
            continue
        pygame.draw.line(
            screen, config.RIVER_CHANNEL_COLOR, p0, p1, config.RIVER_CHANNEL_WIDTH
        )
        pygame.draw.line(
            screen,
            config.RIVER_CHANNEL_HIGHLIGHT_COLOR,
            p0,
            p1,
            config.RIVER_CHANNEL_HIGHLIGHT_WIDTH,
        )

    for node, _ in river_nodes:
        point = _riverNodeToScreen(
            node, center_x, center_y, width, height, camera_yaw, camera_elevation
        )
        if point is None:
            continue

        color, radius = _riverNodeStyle(_nodeType(node))
        pygame.draw.circle(screen, color, point, radius)


def _riverNodeStyle(node_type):
    if node_type == "source":
        return config.RIVER_SOURCE_COLOR, config.RIVER_SOURCE_RADIUS
    if node_type == "outlet_local_minimum":
        return config.RIVER_LOCAL_MINIMUM_COLOR, config.RIVER_LOCAL_MINIMUM_RADIUS
    if node_type == "outlet_sea":
        return config.RIVER_SEA_OUTLET_COLOR, config.RIVER_SEA_OUTLET_RADIUS
    return config.RIVER_NODE_COLOR, config.RIVER_NODE_RADIUS


def _riverNodeToScreen(
    node, center_x, center_y, screen_width, screen_height, camera_yaw, camera_elevation
):
    world_width = config.TERRAIN_PLATE_CELLS / config.PLT_SCALE
    min_x = center_x - world_width * 0.5
    min_y = center_y - world_width * 0.5
    x, y = _nodePoint(node)
    cube_x = (float(x) - min_x) / world_width * 2.0 - 1.0
    cube_z = (float(y) - min_y) / world_width * 2.0 - 1.0

    if cube_x < -1.1 or cube_x > 1.1 or cube_z < -1.1 or cube_z > 1.1:
        return None

    cube_y = (
        max(0.0, min(1.0, _nodeHeight(node) * config.TERRAIN_HEIGHT_SCALE))
        + config.TERRAIN_RIVER_HEIGHT_OFFSET
    )
    projected = _projectPoint(
        (cube_x, cube_y, cube_z),
        screen_width,
        screen_height,
        camera_yaw,
        camera_elevation,
    )
    return int(projected[0]), int(projected[1])


def _terrainHudLines(
    center_x,
    center_y,
    raw_heights,
    loaded_mask,
    river_segments,
    river_nodes,
    camera_yaw,
    camera_elevation,
    loaded_plate_count,
    active_plate_count,
):
    if raw_heights is None or loaded_mask is None or not np.any(loaded_mask):
        height_line = "height=..."
    else:
        loaded_heights = raw_heights[loaded_mask]
        min_height = float(np.min(loaded_heights))
        max_height = float(np.max(loaded_heights))
        height_line = f"height={min_height:.3f}..{max_height:.3f}"
    if active_plate_count > 0:
        load_line = f"loaded_plates={loaded_plate_count}/{active_plate_count}"
    else:
        load_line = "loaded_plates=..."
    return (
        f"center=({center_x:.0f}, {center_y:.0f})",
        f"plate_cells={config.TERRAIN_PLATE_CELLS:.2f}  "
        f"resolution={config.TERRAIN_RESOLUTION}",
        load_line,
        height_line,
        f"rivers={len(river_segments)} segments  {len(river_nodes)} nodes",
        f"view yaw={math.degrees(camera_yaw):.1f}  "
        f"elev={math.degrees(camera_elevation):.1f}",
    )


def _drawTerrainHud(
    screen,
    center_x,
    center_y,
    raw_heights,
    loaded_mask,
    font,
    pygame,
    river_segments,
    river_nodes,
    camera_yaw,
    camera_elevation,
    loaded_plate_count,
    active_plate_count,
):
    lines = _terrainHudLines(
        center_x,
        center_y,
        raw_heights,
        loaded_mask,
        river_segments,
        river_nodes,
        camera_yaw,
        camera_elevation,
        loaded_plate_count,
        active_plate_count,
    )
    x = 14
    y = 12
    for line in lines:
        text = font.render(line, True, config.TERRAIN_HUD_TEXT_COLOR)
        screen.blit(text, (x, y))
        y += 19


def _projectPoint(point, screen_width, screen_height, camera_yaw, camera_elevation):
    x, y, z = point

    cos_yaw = math.cos(camera_yaw)
    sin_yaw = math.sin(camera_yaw)

    x1 = x * cos_yaw - z * sin_yaw
    z1 = x * sin_yaw + z * cos_yaw

    scale = min(screen_width, screen_height) * config.TERRAIN_SCREEN_SCALE
    depth_slope = math.sin(camera_elevation)
    height_scale = math.cos(camera_elevation) * config.TERRAIN_VERTICAL_SCALE
    screen_x = screen_width * 0.5 + x1 * scale
    screen_y = (
        screen_height * config.TERRAIN_SCREEN_BASELINE
        + z1 * scale * depth_slope
        - y * scale * height_scale
    )
    depth = z1 * math.cos(camera_elevation) + y * math.sin(camera_elevation)
    return screen_x, screen_y, depth


def _terrainColor(height):
    sea_level = config.SEA_LEVEL_FRACTION
    if height <= sea_level:
        t = max(0.0, min(1.0, height / (sea_level + 1e-8)))
        return _lerpColor((22, 55, 106), (56, 121, 165), t)

    t = max(0.0, min(1.0, (height - sea_level) / (1.0 - sea_level + 1e-8)))
    if t < 0.45:
        return _lerpColor((74, 132, 72), (129, 157, 86), t / 0.45)
    if t < 0.8:
        return _lerpColor((129, 157, 86), (139, 105, 78), (t - 0.45) / 0.35)
    return _lerpColor((139, 105, 78), (232, 232, 222), (t - 0.8) / 0.2)


def _lerpColor(a, b, t):
    return (
        int(a[0] + (b[0] - a[0]) * t),
        int(a[1] + (b[1] - a[1]) * t),
        int(a[2] + (b[2] - a[2]) * t),
    )


def _gridCoord(index, resolution):
    return -1.0 + 2.0 * index / (resolution - 1)


def _configuredViewCenter():
    owner_center = plateCenter(config.VISUALIZER_PLT_OWNER_IDX)
    return (
        float(owner_center[0]) / config.PLT_SCALE,
        float(owner_center[1]) / config.PLT_SCALE,
    )


def _nodePoint(node):
    return float(node.x), float(node.y)


def _nodeHeight(node):
    return float(getattr(node, "river_height", getattr(node, "height", 0.0)))


def _nodeType(node):
    return getattr(node, "type", "channel")
