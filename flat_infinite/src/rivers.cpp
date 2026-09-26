#include "flat_infinite/world.hpp"

#include "flat_infinite/noise.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>

namespace flat_infinite {
namespace {

using Pixel = std::pair<int, int>;

bool sameOptions(const RiverOptions& a, const RiverOptions& b) {
    return a.resolution == b.resolution && a.river_count == b.river_count
        && a.source_min_height == b.source_min_height
        && a.min_source_spacing == b.min_source_spacing && a.step_size == b.step_size
        && a.max_steps == b.max_steps && a.border_margin == b.border_margin;
}

RiverOptions sanitize(RiverOptions options) {
    options.resolution = std::max(2, options.resolution);
    options.river_count = std::max(0, options.river_count);
    options.min_source_spacing = std::max(0, options.min_source_spacing);
    options.step_size = std::max(1, options.step_size);
    options.max_steps = std::max(1, options.max_steps);
    return options;
}

bool segmentExists(std::size_t from, std::size_t to, const std::vector<RiverSegment>& segments) {
    return std::any_of(segments.begin(), segments.end(), [&](const RiverSegment& segment) {
        return (segment.from == from && segment.to == to) || (segment.from == to && segment.to == from);
    });
}

double orientation(const Pixel& a, const Pixel& b, const Pixel& c) {
    return static_cast<double>(b.first - a.first) * static_cast<double>(c.second - a.second)
        - static_cast<double>(b.second - a.second) * static_cast<double>(c.first - a.first);
}

bool onSegment(const Pixel& a, const Pixel& p, const Pixel& b) {
    return p.first >= std::min(a.first, b.first) && p.first <= std::max(a.first, b.first)
        && p.second >= std::min(a.second, b.second) && p.second <= std::max(a.second, b.second);
}

bool pixelSegmentsCross(const Pixel& a, const Pixel& b, const Pixel& c, const Pixel& d) {
    if (a == b || c == d || a == c || a == d || b == c || b == d) return false;
    const double o1 = orientation(a, b, c);
    const double o2 = orientation(a, b, d);
    const double o3 = orientation(c, d, a);
    const double o4 = orientation(c, d, b);
    if (o1 == 0.0 && onSegment(a, c, b)) return true;
    if (o2 == 0.0 && onSegment(a, d, b)) return true;
    if (o3 == 0.0 && onSegment(c, a, d)) return true;
    if (o4 == 0.0 && onSegment(c, b, d)) return true;
    return (o1 > 0.0) != (o2 > 0.0) && (o3 > 0.0) != (o4 > 0.0);
}

bool wouldCross(
    std::size_t from,
    const Pixel& to_pixel,
    const std::vector<RiverSegment>& segments,
    const std::vector<RiverNode>& nodes,
    std::optional<std::size_t> to_node = std::nullopt) {
    const Pixel from_pixel{nodes[from].row, nodes[from].col};
    for (const RiverSegment& segment : segments) {
        if (segment.from == from || segment.to == from) continue;
        if (to_node && (segment.from == *to_node || segment.to == *to_node)) continue;
        const Pixel a{nodes[segment.from].row, nodes[segment.from].col};
        const Pixel b{nodes[segment.to].row, nodes[segment.to].col};
        if (pixelSegmentsCross(from_pixel, to_pixel, a, b)) return true;
    }
    return false;
}

// Every eligible source sample, highest first. Spacing from other sources and
// from existing rivers is enforced while tracing, against the rivers accepted
// so far (see nearExistingRiver).
std::vector<Pixel> sourceCandidates(const PlateGrid& grid, const RiverOptions& options, const Config& config) {
    std::vector<std::tuple<double, int, int>> candidates;
    const double minimum = std::max(options.source_min_height, config.sea_level_fraction);
    for (int row = 0; row < grid.resolution; ++row) {
        for (int col = 0; col < grid.resolution; ++col) {
            const std::size_t index = grid.index(row, col);
            if (!grid.valid[index] || !grid.border_safe[index] || grid.heights[index] < minimum) continue;
            candidates.emplace_back(grid.heights[index], row, col);
        }
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<>());
    std::vector<Pixel> ordered;
    ordered.reserve(candidates.size());
    for (const auto& [height_value, row, col] : candidates) {
        (void)height_value;
        ordered.emplace_back(row, col);
    }
    return ordered;
}

// A source closer than the minimum source spacing to an already traced river
// would start on (or beside) that river's channel. Skipping it keeps rivers from
// overlapping and keeps node heights consistent across paths.
bool nearExistingRiver(const Pixel& source, const PlateRiverCache& cache, int spacing) {
    if (spacing <= 0) return false;
    const double limit = static_cast<double>(spacing);
    const Point p{static_cast<double>(source.first), static_cast<double>(source.second)};
    for (const RiverNode& node : cache.nodes) {
        if (distance(p, {static_cast<double>(node.row), static_cast<double>(node.col)}) < limit) return true;
    }
    for (const RiverSegment& segment : cache.segments) {
        const RiverNode& a = cache.nodes[segment.from];
        const RiverNode& b = cache.nodes[segment.to];
        const Segment2D pixels{
            {static_cast<double>(a.row), static_cast<double>(a.col)},
            {static_cast<double>(b.row), static_cast<double>(b.col)}};
        if (pixels.distance(p) < limit) return true;
    }
    return false;
}

std::size_t nodeForPixel(
    const Pixel& pixel,
    RiverNodeType type,
    PlateRiverCache& cache,
    std::map<Pixel, std::size_t>& node_by_pixel) {
    const auto existing = node_by_pixel.find(pixel);
    if (existing != node_by_pixel.end()) {
        RiverNode& node = cache.nodes[existing->second];
        if (type == RiverNodeType::SeaOutlet
            || (type == RiverNodeType::LocalMinimum && node.type != RiverNodeType::Source)) {
            node.type = type;
        }
        return existing->second;
    }
    const std::size_t grid_index = cache.grid.index(pixel.first, pixel.second);
    RiverNode node;
    node.world_point = cache.grid.world_points[grid_index];
    node.plate_point = cache.grid.plate_points[grid_index];
    node.row = pixel.first;
    node.col = pixel.second;
    node.height = cache.grid.heights[grid_index];
    node.river_height = node.height;
    node.type = type;
    const std::size_t id = cache.nodes.size();
    cache.nodes.push_back(node);
    node_by_pixel[pixel] = id;
    return id;
}

void addSegment(std::size_t from, std::size_t to, PlateRiverCache& cache) {
    if (from == to || segmentExists(from, to, cache.segments)) return;
    cache.segments.push_back({from, to, cache.nodes[from].height, cache.nodes[to].height, 1});
}

std::optional<Pixel> downhillNeighbor(
    const Pixel& pixel,
    std::size_t from_node,
    const PlateRiverCache& cache,
    const std::map<Pixel, std::size_t>& node_by_pixel,
    const Config& config) {
    const int row = pixel.first;
    const int col = pixel.second;
    const double current_height = cache.grid.heights[cache.grid.index(row, col)];
    double best_score = current_height;
    std::optional<Pixel> best;
    for (int next_row = std::max(0, row - cache.options.step_size);
         next_row <= std::min(cache.grid.resolution - 1, row + cache.options.step_size); ++next_row) {
        for (int next_col = std::max(0, col - cache.options.step_size);
             next_col <= std::min(cache.grid.resolution - 1, col + cache.options.step_size); ++next_col) {
            if (next_row == row && next_col == col) continue;
            const std::size_t index = cache.grid.index(next_row, next_col);
            if (!cache.grid.valid[index] || !cache.grid.border_safe[index]) continue;
            const double next_height = cache.grid.heights[index];
            if (next_height >= current_height - config.river_min_drop) continue;
            const Pixel candidate{next_row, next_col};
            const auto found = node_by_pixel.find(candidate);
            const std::optional<std::size_t> to_node = found == node_by_pixel.end()
                ? std::nullopt : std::optional<std::size_t>(found->second);
            if (wouldCross(from_node, candidate, cache.segments, cache.nodes, to_node)) continue;
            const int dr = next_row - row;
            const int dc = next_col - col;
            const double score = next_height + config.river_path_distance_weight * (dr * dr + dc * dc);
            if (score < best_score) {
                best_score = score;
                best = candidate;
            }
        }
    }
    return best;
}

bool hasUnsafeDownhill(const Pixel& pixel, const PlateRiverCache& cache, const Config& config) {
    const int row = pixel.first;
    const int col = pixel.second;
    const double current = cache.grid.heights[cache.grid.index(row, col)];
    for (int r = std::max(0, row - cache.options.step_size);
         r <= std::min(cache.grid.resolution - 1, row + cache.options.step_size); ++r) {
        for (int c = std::max(0, col - cache.options.step_size);
             c <= std::min(cache.grid.resolution - 1, col + cache.options.step_size); ++c) {
            if (r == row && c == col) continue;
            const std::size_t index = cache.grid.index(r, c);
            if (cache.grid.valid[index] && !cache.grid.border_safe[index]
                && cache.grid.heights[index] < current - config.river_min_drop) return true;
        }
    }
    return false;
}

std::optional<std::size_t> nearbyRiverNode(
    const Pixel& pixel,
    const std::set<std::size_t>& previous_nodes,
    std::size_t from_node,
    const PlateRiverCache& cache,
    const Config& config) {
    if (previous_nodes.empty()) return std::nullopt;
    const double current = cache.grid.heights[cache.grid.index(pixel.first, pixel.second)];
    const int max_distance_squared = cache.options.step_size * cache.options.step_size;
    double best_score = 1e30;
    std::optional<std::size_t> best;
    for (std::size_t id : previous_nodes) {
        if (id == from_node || cache.nodes[id].type == RiverNodeType::Source) continue;
        const int dr = cache.nodes[id].row - pixel.first;
        const int dc = cache.nodes[id].col - pixel.second;
        const int squared = dr * dr + dc * dc;
        if (squared == 0 || squared > max_distance_squared) continue;
        if (cache.nodes[id].height >= current - config.river_min_drop) continue;
        if (segmentExists(from_node, id, cache.segments)) continue;
        const Pixel candidate{cache.nodes[id].row, cache.nodes[id].col};
        if (wouldCross(from_node, candidate, cache.segments, cache.nodes, id)) continue;
        const double score = cache.nodes[id].height + 1e-6 * squared;
        if (score < best_score) {
            best_score = score;
            best = id;
        }
    }
    return best;
}

void applySlopeDrop(PlateRiverCache& cache, const Config& config) {
    if (cache.nodes.empty() || cache.segments.empty()) return;
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> segment_by_nodes;
    for (std::size_t i = 0; i < cache.segments.size(); ++i) {
        segment_by_nodes[{cache.segments[i].from, cache.segments[i].to}] = i;
    }
    for (const auto& path : cache.paths) {
        if (path.size() < 2) continue;
        const double source_raw = cache.nodes[path.front()].height;
        const double outlet_raw = cache.nodes[path.back()].height;
        const double outlet_height = cache.nodes[path.back()].river_height;
        const double raw_span = source_raw - outlet_raw;
        double source_height = outlet_height;
        if (raw_span > config.river_min_drop) {
            source_height = std::max(
                source_raw - std::max(0.0, config.river_height_slope_drop),
                outlet_height + config.river_min_drop);
        }
        const double adjusted_span = source_height - outlet_height;
        std::map<std::size_t, double> path_heights;
        for (std::size_t id : path) {
            double river_height = outlet_height;
            if (raw_span > config.river_min_drop) {
                const double t = std::clamp((cache.nodes[id].height - outlet_raw) / raw_span, 0.0, 1.0);
                river_height = outlet_height + adjusted_span * t;
            }
            cache.nodes[id].river_height = river_height;
            path_heights[id] = river_height;
        }
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            const auto found = segment_by_nodes.find({path[i], path[i + 1]});
            if (found == segment_by_nodes.end()) continue;
            RiverSegment& segment = cache.segments[found->second];
            segment.from_height = path_heights[path[i]];
            segment.to_height = path_heights[path[i + 1]];
        }
    }
}

void assignStrahlerOrders(PlateRiverCache& cache) {
    const std::size_t node_count = cache.nodes.size();
    if (node_count == 0 || cache.segments.empty()) return;
    std::vector<std::vector<std::size_t>> incoming(node_count), outgoing(node_count);
    for (std::size_t i = 0; i < cache.segments.size(); ++i) {
        outgoing[cache.segments[i].from].push_back(i);
        incoming[cache.segments[i].to].push_back(i);
    }
    struct Link { std::size_t start; std::size_t end; std::vector<std::size_t> segments; int order = 0; };
    std::set<std::size_t> junctions;
    for (std::size_t i = 0; i < node_count; ++i) {
        if (incoming[i].size() != 1 || outgoing[i].size() != 1) junctions.insert(i);
    }
    std::vector<Link> links;
    for (std::size_t junction : junctions) {
        for (std::size_t segment_id : outgoing[junction]) {
            Link link{junction, cache.segments[segment_id].to, {segment_id}, 0};
            while (junctions.count(link.end) == 0) {
                const std::size_t next = outgoing[link.end][0];
                link.segments.push_back(next);
                link.end = cache.segments[next].to;
            }
            links.push_back(std::move(link));
        }
    }
    std::map<std::size_t, std::vector<std::size_t>> links_in, links_out;
    for (std::size_t i = 0; i < links.size(); ++i) {
        links_in[links[i].end].push_back(i);
        links_out[links[i].start].push_back(i);
    }
    std::map<std::size_t, int> pending, node_order;
    std::vector<std::size_t> stack;
    for (std::size_t junction : junctions) {
        pending[junction] = static_cast<int>(links_in[junction].size());
        if (pending[junction] == 0) {
            node_order[junction] = 1;
            stack.push_back(junction);
        }
    }
    std::set<std::size_t> processed;
    while (!stack.empty()) {
        const std::size_t junction = stack.back();
        stack.pop_back();
        if (!processed.insert(junction).second) continue;
        for (std::size_t link_id : links_out[junction]) {
            links[link_id].order = node_order.count(junction) ? node_order[junction] : 1;
            const std::size_t end = links[link_id].end;
            if (--pending[end] == 0 && processed.count(end) == 0) {
                int highest = 1;
                int highest_count = 0;
                for (std::size_t incoming_link : links_in[end]) {
                    const int order = links[incoming_link].order;
                    if (order > highest) { highest = order; highest_count = 1; }
                    else if (order == highest) ++highest_count;
                }
                node_order[end] = highest_count >= 2 ? highest + 1 : highest;
                stack.push_back(end);
            }
        }
    }
    for (const Link& link : links) {
        const int order = link.order > 0 ? link.order : (node_order.count(link.start) ? node_order[link.start] : 1);
        for (std::size_t segment : link.segments) cache.segments[segment].strahler_order = order;
    }
}

struct SegmentSample {
    double height;
    double distance;
    double lake_surface_distance;
};

SegmentSample sampleSegment(const Point& point, const RiverSegment& segment, const PlateRiverCache& cache, const Config& config) {
    const RiverNode& from = cache.nodes[segment.from];
    const RiverNode& to = cache.nodes[segment.to];
    const Segment2D geometry{from.world_point, to.world_point};
    const double t = geometry.parameter(point);
    double height_value = segment.from_height + (segment.to_height - segment.from_height) * t;
    double distance_value = geometry.distance(point);
    double lake_surface_distance = 1e30;
    auto applyLake = [&](const RiverNode& node, double lake_height) {
        if (node.type != RiverNodeType::LocalMinimum) return;
        const double lake_distance = distance(point, node.world_point) - config.river_lake_radius;
        const double surface_distance = lake_distance - config.river_lake_surface_margin_distance;
        distance_value = std::min(distance_value, lake_distance);
        if (surface_distance <= 0.0 && surface_distance < lake_surface_distance) {
            height_value = lake_height;
            lake_surface_distance = surface_distance;
        } else if (surface_distance > 0.0
            && surface_distance < config.river_lake_node_height_fade_distance) {
            const double fade = 1.0 - smoothStep(
                surface_distance / (config.river_lake_node_height_fade_distance + 1e-12));
            height_value = height_value * (1.0 - fade) + lake_height * fade;
        }
    };
    applyLake(from, segment.from_height);
    applyLake(to, segment.to_height);
    return {height_value, distance_value, lake_surface_distance};
}

} // namespace

PlateRiverCache World::buildPlateRivers(PlateIndex owner, const RiverOptions& raw_options) const {
    PlateRiverCache cache;
    cache.owner = owner;
    cache.options = sanitize(raw_options);
    cache.grid = buildPlateGrid(owner, cache.options);
    std::map<Pixel, std::size_t> node_by_pixel;

    for (const Pixel& source : sourceCandidates(cache.grid, cache.options, config_)) {
        if (static_cast<int>(cache.paths.size()) >= cache.options.river_count) break;
        // Also covers source-to-source spacing: accepted sources are river nodes.
        if (nearExistingRiver(source, cache, cache.options.min_source_spacing)) continue;
        const std::size_t node_start = cache.nodes.size();
        const std::size_t segment_start = cache.segments.size();
        std::vector<std::size_t> path;
        std::set<Pixel> path_seen;
        std::set<std::size_t> previous_nodes;
        for (std::size_t i = 0; i < node_start; ++i) previous_nodes.insert(i);
        Pixel pixel = source;
        std::size_t current = nodeForPixel(pixel, RiverNodeType::Source, cache, node_by_pixel);
        path.push_back(current);
        path_seen.insert(pixel);
        bool rolled_back = false;

        for (int step = 0; step < cache.options.max_steps; ++step) {
            if (cache.grid.heights[cache.grid.index(pixel.first, pixel.second)] <= config_.sea_level_fraction) {
                cache.nodes[path.back()].type = RiverNodeType::SeaOutlet;
                break;
            }
            const auto existing = nearbyRiverNode(pixel, previous_nodes, current, cache, config_);
            if (existing) {
                addSegment(current, *existing, cache);
                path.push_back(*existing);
                break;
            }
            const auto next = downhillNeighbor(pixel, current, cache, node_by_pixel, config_);
            if (!next) {
                const std::size_t index = cache.grid.index(pixel.first, pixel.second);
                if (hasUnsafeDownhill(pixel, cache, config_) || !cache.grid.lake_safe[index]) {
                    cache.segments.resize(segment_start);
                    for (std::size_t i = node_start; i < cache.nodes.size(); ++i) {
                        node_by_pixel.erase({cache.nodes[i].row, cache.nodes[i].col});
                    }
                    cache.nodes.resize(node_start);
                    path.clear();
                    rolled_back = true;
                } else if (cache.nodes[path.back()].type != RiverNodeType::Source) {
                    cache.nodes[path.back()].type = RiverNodeType::LocalMinimum;
                }
                break;
            }
            const auto found = node_by_pixel.find(*next);
            const bool joins_existing = found != node_by_pixel.end();
            const RiverNodeType type = cache.grid.heights[cache.grid.index(next->first, next->second)]
                    <= config_.sea_level_fraction
                ? RiverNodeType::SeaOutlet : RiverNodeType::Channel;
            const std::size_t next_node = nodeForPixel(*next, type, cache, node_by_pixel);
            addSegment(current, next_node, cache);
            path.push_back(next_node);
            if (type == RiverNodeType::SeaOutlet) break;
            if (path_seen.count(*next) != 0) {
                const std::size_t index = cache.grid.index(next->first, next->second);
                if (!cache.grid.lake_safe[index]) {
                    cache.segments.resize(segment_start);
                    for (std::size_t i = node_start; i < cache.nodes.size(); ++i) {
                        node_by_pixel.erase({cache.nodes[i].row, cache.nodes[i].col});
                    }
                    cache.nodes.resize(node_start);
                    path.clear();
                    rolled_back = true;
                } else if (cache.nodes[path.back()].type != RiverNodeType::Source) {
                    cache.nodes[path.back()].type = RiverNodeType::LocalMinimum;
                }
                break;
            }
            if (joins_existing && path.size() >= 2 && next_node != path[path.size() - 2]) break;
            path_seen.insert(*next);
            pixel = *next;
            current = next_node;
        }
        if (!rolled_back && !path.empty()) cache.paths.push_back(std::move(path));
    }
    applySlopeDrop(cache, config_);
    assignStrahlerOrders(cache);
    return cache;
}

const PlateRiverCache& World::ensurePlate(PlateIndex owner, const RiverOptions& raw_options) {
    const RiverOptions options = sanitize(raw_options);
    auto found = cache_.find(owner);
    if (found == cache_.end() || !sameOptions(found->second.options, options)) {
        PlateRiverCache generated = buildPlateRivers(owner, options);
        found = cache_.insert_or_assign(owner, std::move(generated)).first;
    }
    return found->second;
}

void World::ensureActive(double world_x, double world_y, int radius, const RiverOptions& options) {
    for (PlateIndex owner : activePlateIndices(world_x, world_y, radius)) ensurePlate(owner, options);
}

RiverFields World::riverFields(double world_x, double world_y) const {
    const auto found = cache_.find(plateOwner(world_x, world_y));
    if (found == cache_.end() || found->second.segments.empty()) return {};
    const PlateRiverCache& cache = found->second;
    const Point point{world_x, world_y};
    double best_distance = 1e30;
    double best_height = 0.0;
    double lake_surface_distance = 1e30;
    double lake_height = 0.0;
    double zero_height_sum = 0.0;
    int zero_count = 0;
    double height_weight_sum = 0.0;
    double weight_sum = 0.0;
    for (const RiverSegment& segment : cache.segments) {
        const SegmentSample sample = sampleSegment(point, segment, cache, config_);
        if (sample.distance < best_distance) {
            best_distance = sample.distance;
            best_height = sample.height;
        }
        if (sample.lake_surface_distance <= 0.0) {
            if (lake_surface_distance > 0.0 || sample.lake_surface_distance < lake_surface_distance) {
                lake_surface_distance = sample.lake_surface_distance;
                lake_height = sample.height;
            }
        } else if (sample.distance <= 1e-12) {
            zero_height_sum += sample.height;
            ++zero_count;
        } else if (sample.distance < config_.river_height_blend_distance) {
            const double fade = 1.0 - smoothStep(sample.distance / (config_.river_height_blend_distance + 1e-12));
            const double weight = fade * fade / ((sample.distance + 1e-9) * (sample.distance + 1e-9));
            height_weight_sum += sample.height * weight;
            weight_sum += weight;
        }
    }
    double river_height = best_height;
    if (lake_surface_distance <= 0.0) river_height = lake_height;
    else if (zero_count > 0) river_height = zero_height_sum / zero_count;
    else if (weight_sum > 0.0) river_height = height_weight_sum / weight_sum;
    double normalized_distance = 1.0;
    if (best_distance <= 0.0) normalized_distance = 0.0;
    else if (best_distance < config_.river_distance_width) {
        normalized_distance = std::pow(
            best_distance / (config_.river_distance_width + 1e-12),
            config_.river_distance_field_power);
    }
    return {river_height, normalized_distance};
}

double World::height(double world_x, double world_y, bool include_rivers) {
    const double terrain_height = baseHeight(world_x, world_y);
    if (!include_rivers) return terrain_height;
    const PlateIndex owner = plateOwner(world_x, world_y);
    if (cache_.find(owner) == cache_.end()) ensurePlate(owner, defaultRiverOptions(config_));
    const RiverFields river = riverFields(world_x, world_y);
    if (river.normalized_distance >= 1.0) return terrain_height;
    const double terrain_weight = std::pow(river.normalized_distance, config_.river_blend_exponent);
    return terrain_height * terrain_weight + river.height * (1.0 - terrain_weight);
}

TerrainGrid World::terrainGrid(
    double center_x,
    double center_y,
    double plate_cells,
    int resolution,
    bool include_rivers,
    int active_radius,
    const RiverOptions& options) {
    resolution = std::max(2, resolution);
    const double width = plate_cells / config_.plate_scale;
    TerrainGrid grid;
    grid.resolution = resolution;
    grid.min_x = center_x - width * 0.5;
    grid.max_x = center_x + width * 0.5;
    grid.min_y = center_y - width * 0.5;
    grid.max_y = center_y + width * 0.5;
    grid.heights.resize(static_cast<std::size_t>(resolution * resolution));
    if (include_rivers) ensureActive(center_x, center_y, active_radius, options);
    for (int row = 0; row < resolution; ++row) {
        const double y = grid.min_y + (grid.max_y - grid.min_y) * row / (resolution - 1.0);
        for (int col = 0; col < resolution; ++col) {
            const double x = grid.min_x + (grid.max_x - grid.min_x) * col / (resolution - 1.0);
            grid.heights[static_cast<std::size_t>(row * resolution + col)] = height(x, y, include_rivers);
        }
    }
    return grid;
}

void World::clearCache() { cache_.clear(); }

const PlateRiverCache* World::cachedPlate(PlateIndex owner) const {
    const auto found = cache_.find(owner);
    return found == cache_.end() ? nullptr : &found->second;
}

} // namespace flat_infinite
