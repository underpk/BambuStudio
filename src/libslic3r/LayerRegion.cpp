#include "Layer.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "PerimeterGenerator.hpp"
#include "Print.hpp"
#include "Surface.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"
#include "RegionExpansion.hpp"

#include <string>
#include <map>

#include <boost/log/trivial.hpp>
static const double max_deviation = scale_(0.5);
static const double max_variance  = 5 * scale_(0.01) * scale_(0.01);

namespace Slic3r {

Flow LayerRegion::flow(FlowRole role) const
{
    return this->flow(role, m_layer->height);
}

Flow LayerRegion::flow(FlowRole role, double layer_height) const
{
    return m_region->flow(*m_layer->object(), role, layer_height, m_layer->id() == 0);
}

Flow LayerRegion::bridging_flow(FlowRole role, bool thick_bridge) const
{
    const PrintRegion       &region         = this->region();
    const PrintRegionConfig &region_config  = region.config();
    const PrintObject       &print_object   = *this->layer()->object();
    if (thick_bridge) {
        // The old Slic3r way (different from all other slicers): Use rounded extrusions.
        // Get the configured nozzle_diameter for the extruder associated to the flow role requested.
        // Here this->extruder(role) - 1 may underflow to MAX_INT, but then the get_at() will follback to zero'th element, so everything is all right.
        auto nozzle_diameter = float(print_object.print()->config().nozzle_diameter.get_at(region.extruder(role) - 1));
        // Applies default bridge spacing.
        return Flow::bridging_flow(float(sqrt(region_config.bridge_flow)) * nozzle_diameter, nozzle_diameter);
    } else {
        // The same way as other slicers: Use normal extrusions. Apply bridge_flow while maintaining the original spacing.
        return this->flow(role).with_flow_ratio(region_config.bridge_flow);
    }
}

// Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
void LayerRegion::slices_to_fill_surfaces_clipped()
{
    // Note: this method should be idempotent, but fill_surfaces gets modified
    // in place. However we're now only using its boundaries (which are invariant)
    // so we're safe. This guarantees idempotence of prepare_infill() also in case
    // that combine_infill() turns some fill_surface into VOID surfaces.
    // Collect polygons per surface type.
    std::array<SurfacesPtr, size_t(stCount)> by_surface;
    for (Surface &surface : this->slices.surfaces)
        by_surface[size_t(surface.surface_type)].emplace_back(&surface);
    // Trim surfaces by the fill_boundaries.
    this->fill_surfaces.surfaces.clear();
    for (size_t surface_type = 0; surface_type < size_t(stCount); ++ surface_type) {
        const SurfacesPtr &this_surfaces = by_surface[surface_type];
        if (! this_surfaces.empty())
            this->fill_surfaces.append(intersection_ex(this_surfaces, this->fill_expolygons), SurfaceType(surface_type));
    }
}

void LayerRegion::auto_circle_compensation(SurfaceCollection& slices, const AutoContourHolesCompensationParams &auto_contour_holes_compensation_params, float manual_offset)
{
     // filament is 1 base
     int filament_idx = this->region().config().wall_filament - 1;

     double limited_speed = auto_contour_holes_compensation_params.circle_compensation_speed[filament_idx];
     double counter_speed_coef      = auto_contour_holes_compensation_params.counter_speed_coef[filament_idx];
     double counter_diameter_coef   = auto_contour_holes_compensation_params.counter_diameter_coef[filament_idx];
     double counter_compensate_coef = scale_(auto_contour_holes_compensation_params.counter_compensate_coef[filament_idx]);

     double hole_speed_coef      = auto_contour_holes_compensation_params.hole_speed_coef[filament_idx];
     double hole_diameter_coef   = auto_contour_holes_compensation_params.hole_diameter_coef[filament_idx];
     double hole_compensate_coef = scale_(auto_contour_holes_compensation_params.hole_compensate_coef[filament_idx]);

     double counter_limit_min_value = scale_(auto_contour_holes_compensation_params.counter_limit_min_value[filament_idx]);
     double counter_limit_max_value = scale_(auto_contour_holes_compensation_params.counter_limit_max_value[filament_idx]);
     double hole_limit_min_value    = scale_(auto_contour_holes_compensation_params.hole_limit_min_value[filament_idx]);
     double hole_limit_max_value    = scale_(auto_contour_holes_compensation_params.hole_limit_max_value[filament_idx]);

     double diameter_limit_value = scale_(auto_contour_holes_compensation_params.diameter_limit[filament_idx]);

    for (Surface &surface : slices.surfaces) {
        Point  center;
        double diameter = 0;
        if (surface.expolygon.contour.is_approx_circle(max_deviation, max_variance, center, diameter)) {
            double offset_value = scale_(counter_speed_coef * limited_speed) + counter_diameter_coef * diameter + counter_compensate_coef;
            if (offset_value < counter_limit_min_value) {
                offset_value = counter_limit_min_value;
            } else if (offset_value > counter_limit_max_value) {
                offset_value = counter_limit_max_value;
            }
            offset_value -= manual_offset / 2;
            Polygons offseted_polys = offset(surface.expolygon.contour, offset_value);
            if (offseted_polys.size() == 1) {
                surface.expolygon.contour = offseted_polys[0];
                if (diameter < diameter_limit_value)
                    surface.counter_circle_compensation = true;
            }
        }
        for (size_t i = 0; i < surface.expolygon.holes.size(); ++i) {
            Polygon &hole = surface.expolygon.holes[i];
            if (hole.is_approx_circle(max_deviation, max_variance, center, diameter)) {
                double offset_value = scale_(hole_speed_coef * limited_speed) + hole_diameter_coef * diameter + hole_compensate_coef;
                if (offset_value < hole_limit_min_value) {
                    offset_value = hole_limit_min_value;
                } else if (offset_value > hole_limit_max_value) {
                    offset_value = hole_limit_max_value;
                }
                // positive value means shrinking hole, which oppsite to contour
                offset_value            = -offset_value;
                offset_value -= manual_offset / 2;
                Polygons offseted_polys = offset(hole, offset_value);
                if (offseted_polys.size() == 1) {
                    hole = offseted_polys[0];
                    if (diameter < diameter_limit_value)
                        surface.holes_circle_compensation.push_back(i);
                }
            }
        }
    }
}

void LayerRegion::make_perimeters(const SurfaceCollection &slices, const PerimeterRegions &perimeter_regions, SurfaceCollection *fill_surfaces, ExPolygons *fill_no_overlap, std::vector<LoopNode> &loop_nodes)
{
    this->perimeters.clear();
    this->thin_fills.clear();

    const PrintConfig &      print_config  = this->layer()->object()->print()->config();
    const PrintRegionConfig &region_config = this->region().config();
    const PrintObjectConfig& object_config = this->layer()->object()->config();
    // This needs to be in sync with PrintObject::_slice() slicing_mode_normal_below_layer!
    bool spiral_mode = print_config.spiral_mode &&
        //FIXME account for raft layers.
        (this->layer()->id() >= size_t(region_config.bottom_shell_layers.value) &&
         this->layer()->print_z >= region_config.bottom_shell_thickness - EPSILON);

    PerimeterGenerator g(
        // input:
        &slices,
        this->layer()->height,
        this->flow(frPerimeter),
        &region_config,
        &this->layer()->object()->config(),
        &print_config,
        spiral_mode,
        // output:
        &this->perimeters,
        &this->thin_fills,
        fill_surfaces,
        //BBS
        fill_no_overlap,
        &loop_nodes
    );

    if (this->layer()->lower_layer != nullptr)
        // Cummulative sum of polygons over all the regions.
        g.lower_slices = &this->layer()->lower_layer->lslices;
    if (this->layer()->upper_layer != NULL)
        g.upper_slices = &this->layer()->upper_layer->lslices;

    g.layer_id              = (int)this->layer()->id();
    g.slice_z               = this->layer()->slice_z;
    g.ext_perimeter_flow    = this->flow(frExternalPerimeter);
    g.overhang_flow         = this->bridging_flow(frPerimeter, object_config.thick_bridges);
    g.solid_infill_flow     = this->flow(frSolidInfill);
    g.perimeter_regions     = &perimeter_regions;

    if (this->layer()->object()->config().wall_generator.value == PerimeterGeneratorType::Arachne && !spiral_mode)
        g.process_arachne();
    else
        g.process_classic();
}


#if 1

// Extract surfaces of given type from surfaces, extract fill (layer) thickness of one of the surfaces.
static ExPolygons fill_surfaces_extract_expolygons(Surfaces &surfaces, std::initializer_list<SurfaceType> surface_types, double &thickness)
{
    size_t cnt = 0;
    for (const Surface &surface : surfaces)
        if (std::find(surface_types.begin(), surface_types.end(), surface.surface_type) != surface_types.end()) {
            ++cnt;
            thickness = surface.thickness;
        }
    if (cnt == 0)
        return {};

    ExPolygons out;
    out.reserve(cnt);
    for (Surface &surface : surfaces)
        if (std::find(surface_types.begin(), surface_types.end(), surface.surface_type) != surface_types.end())
            out.emplace_back(std::move(surface.expolygon));
    return out;
}

// Cache for detecting bridge orientation and merging regions with overlapping expansions.
struct Bridge {
    ExPolygon expolygon;
    uint32_t group_id;
    std::vector<Algorithm::RegionExpansionEx>::const_iterator bridge_expansion_begin;
    std::optional<double> angle{std::nullopt};
};

// Group the bridge surfaces by overlaps.
uint32_t group_id(std::vector<Bridge> &bridges, uint32_t src_id) {
    uint32_t group_id = bridges[src_id].group_id;
    while (group_id != src_id) {
        src_id = group_id;
        group_id = bridges[src_id].group_id;
    }
    bridges[src_id].group_id = group_id;
    return group_id;
};

std::vector<Bridge> get_grouped_bridges(
    ExPolygons&& bridge_expolygons,
    const std::vector<Algorithm::RegionExpansionEx>& bridge_expansions
) {
    using namespace Algorithm;

    std::vector<Bridge> result;
    {
        result.reserve(bridge_expansions.size());
        uint32_t group_id = 0;
        using std::move_iterator;
        for (ExPolygon& expolygon : bridge_expolygons)
            result.push_back({ std::move(expolygon), group_id ++, bridge_expansions.end() });
    }


    // Detect overlaps of bridge anchors inside their respective shell regions.
    // bridge_expansions are sorted by boundary id and source id.
    for (auto expansion_iterator = bridge_expansions.begin(); expansion_iterator != bridge_expansions.end();) {
        auto boundary_region_begin = expansion_iterator;
        auto boundary_region_end = std::find_if(
            next(expansion_iterator),
            bridge_expansions.end(),
            [&](const RegionExpansionEx& expansion){
                return expansion.boundary_id != expansion_iterator->boundary_id;
            }
        );

        // Cache of bboxes per expansion boundary.
        std::vector<BoundingBox> bounding_boxes;
        bounding_boxes.reserve(std::distance(boundary_region_begin, boundary_region_end));
        std::transform(
            boundary_region_begin,
            boundary_region_end,
            std::back_inserter(bounding_boxes),
            [](const RegionExpansionEx& expansion){
                return get_extents(expansion.expolygon.contour);
            }
        );

        // For each bridge anchor of the current source:
        for (;expansion_iterator != boundary_region_end; ++expansion_iterator) {
            auto candidate_iterator = std::next(expansion_iterator);
            for (;candidate_iterator != boundary_region_end; ++candidate_iterator) {
                const BoundingBox& current_bounding_box{
                    bounding_boxes[expansion_iterator - boundary_region_begin]
                };
                const BoundingBox& candidate_bounding_box{
                    bounding_boxes[candidate_iterator - boundary_region_begin]
                };
                if (
                    expansion_iterator->src_id != candidate_iterator->src_id
                    && current_bounding_box.overlap(candidate_bounding_box)
                    // One may ignore holes, they are irrelevant for intersection test.
                    && !intersection(expansion_iterator->expolygon.contour, candidate_iterator->expolygon.contour).empty()
                ) {
                    // The two bridge regions intersect. Give them the same (lower) group id.
                    uint32_t id  = group_id(result, expansion_iterator->src_id);
                    uint32_t id2 = group_id(result, candidate_iterator->src_id);
                    if (id < id2)
                        result[id2].group_id = id;
                    else
                        result[id].group_id = id2;
                }
            }
        }
    }
    return result;
}

void detect_bridge_directions(
    const Algorithm::WaveSeeds& bridge_anchors,
    std::vector<Bridge>& bridges,
    const std::vector<ExpansionZone>& expansion_zones
) {
    if (expansion_zones.empty()) {
        throw std::runtime_error("At least one expansion zone must exist!");
    }
    auto it_bridge_anchor = bridge_anchors.begin();
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++ bridge_id) {
        Bridge &bridge = bridges[bridge_id];
        Polygons anchor_areas;
        int32_t last_anchor_id = -1;
        for (; it_bridge_anchor != bridge_anchors.end() && it_bridge_anchor->src == bridge_id; ++ it_bridge_anchor) {
            if (last_anchor_id != int(it_bridge_anchor->boundary)) {
                last_anchor_id = int(it_bridge_anchor->boundary);

                unsigned start_index{};
                unsigned end_index{};
                for (const ExpansionZone& expansion_zone: expansion_zones) {
                    end_index += expansion_zone.expolygons.size();
                    if (last_anchor_id < static_cast<int64_t>(end_index)) {
                        append(anchor_areas, to_polygons(expansion_zone.expolygons[last_anchor_id - start_index]));
                        break;
                    }
                    start_index += expansion_zone.expolygons.size();
                }
            }
        }
        Lines lines{to_lines(diff_pl(to_polylines(bridge.expolygon), expand(anchor_areas, float(SCALED_EPSILON))))};
        auto [bridging_dir, unsupported_dist] = detect_bridging_direction(lines, to_polygons(bridge.expolygon));
        bridge.angle = M_PI + std::atan2(bridging_dir.y(), bridging_dir.x());

        if constexpr (false) {
            coordf_t    stroke_width = scale_(0.06);
            BoundingBox bbox         = get_extents(anchor_areas);
            bbox.merge(get_extents(bridge.expolygon));
            bbox.offset(scale_(1.));
            ::Slic3r::SVG
                svg(debug_out_path(("bridge" + std::to_string(*bridge.angle) + "_" /* + std::to_string(this->layer()->bottom_z())*/).c_str()),
                bbox);
            svg.draw(bridge.expolygon, "cyan");
            svg.draw(lines, "green", stroke_width);
            svg.draw(anchor_areas, "red");
        }
    }
}

struct BridgeAngleScore {
    double    total_good = 0.;   // Summed length of "good" test lines: anchored at both endpoints and not exceeding the span cap.
    double    max_span   = 0.;   // Longest test line regardless of anchoring, the printability metric.
    std::vector<coord_t> good_offsets; // Rotated frame offsets of the good lines, only filled when collect_lines is set.
    BoundingBox          rot_bbox;     // Extents of the clipped area in the rotated frame.
};

// Rasterize the bridge area with test lines along the given angle and classify them by anchoring
// and length, mirroring the visibility check of BridgeDetector::detect_angle().
static BridgeAngleScore score_bridge_angle(
    const ExPolygons &area,
    ExPolygons &anchors,
    const double angle,
    const coord_t spacing,
    const double span_cap,
    const bool collect_lines
) {
    BridgeAngleScore out;
    /*  Outset the bridge expolygons by half the spacing used for detecting anchors;
        we'll use this one to clip our test lines and be sure that their endpoints
        are inside the anchors and not on their contours leading to false negatives. */
    Polygons clip_area = offset(area, 0.5f * float(spacing));
    if (clip_area.empty())
        return out;

    double s = sin(angle);
    double c = cos(angle);
    BoundingBox bbox = get_extents_rotated(clip_area, - angle);
    out.rot_bbox = bbox;
    Lines lines;
    lines.reserve((bbox.max(1) - bbox.min(1) + spacing) / spacing);
    for (coord_t y = bbox.min(1); y <= bbox.max(1); y += spacing)
        lines.push_back(Line(
            Point((coord_t)round(c * bbox.min(0) - s * y), (coord_t)round(c * y + s * bbox.min(0))),
            Point((coord_t)round(c * bbox.max(0) - s * y), (coord_t)round(c * y + s * bbox.max(0)))));

    for (const Line &line : intersection_ln(lines, clip_area)) {
        double len = line.length();
        out.max_span = std::max(out.max_span, len);
        if (len <= span_cap && expolygons_contain(anchors, line.a) && expolygons_contain(anchors, line.b)) {
            out.total_good += len;
            if (collect_lines) {
                // Recover the rotated frame offset of the source line, snapped to the generation grid.
                coord_t y = coord_t(round(c * line.a.y() - s * line.a.x()));
                out.good_offsets.push_back(bbox.min(1) + coord_t(round(double(y - bbox.min(1)) / spacing)) * spacing);
            }
        }
    }
    return out;
}

// Brute force the bridging angle maximizing the coverage by good (anchored, short enough) lines,
// using the same 5 degree resolution and a tie-break rule similar to BridgeDetector::detect_angle().
static std::optional<double> best_anchored_angle(
    const ExPolygons &area,
    ExPolygons &anchors,
    const coord_t spacing,
    const double span_cap
) {
    constexpr int steps = 36;
    struct Candidate { double angle; BridgeAngleScore score; };
    std::vector<Candidate> candidates;
    for (int i = 0; i < steps; ++ i) {
        double angle = i * (PI / steps);
        BridgeAngleScore score = score_bridge_angle(area, anchors, angle, spacing, span_cap, false);
        if (score.total_good > 0.)
            candidates.push_back({ angle, std::move(score) });
    }
    if (candidates.empty())
        return std::nullopt;
    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate &l, const Candidate &r) { return l.score.total_good > r.score.total_good; });
    // If any other direction is within extrusion width of coverage, prefer it if its worst span is shorter.
    size_t i_best = 0;
    for (size_t i = 1; i < candidates.size() && candidates[i_best].score.total_good - candidates[i].score.total_good < spacing; ++ i)
        if (candidates[i].score.max_span < candidates[i_best].score.max_span)
            i_best = i;
    return candidates[i_best].angle;
}

// Produce bridge surfaces for a merged bridge group. Normally emits one surface per expolygon with
// the single detected angle. When that angle produces excessively long bridge lines (e.g. an
// annular bridge, where lines near-tangent to the ring span a long chord while radial crossings
// are short), the region is split into sub-regions, each bridged along its own best direction.
static Surfaces make_bridge_surfaces(
    ExPolygons &&bridge_area,
    const ExPolygons *lower_slices,
    const double default_angle,
    const coord_t spacing
) {
    auto emit = [](ExPolygons &&expolygons, double angle, Surfaces &out) {
        Surface templ{ stBottomBridge, {} };
        templ.bridge_angle = angle;
        for (ExPolygon &ex : expolygons)
            out.emplace_back(templ, std::move(ex));
    };
    auto emit_single = [&]() {
        Surfaces out;
        emit(std::move(bridge_area), default_angle, out);
        return out;
    };
    if (lower_slices == nullptr || lower_slices->empty() || bridge_area.empty() || default_angle < 0)
        return emit_single();
    // The anchors are the lower layer material in the vicinity of the bridge,
    // the same anchor definition as BridgeDetector::initialize().
    ExPolygons anchors = intersection_ex(offset(bridge_area, 2.f * float(spacing)), *lower_slices);
    if (anchors.empty())
        return emit_single();

    // Quality gate: split only when the single angle produces a bridge line longer than what is
    // considered printable (10mm, in line with the support module's max_bridge_length default).
    constexpr double infinite_span = std::numeric_limits<double>::max();
    const double bad_span = scaled<double>(10.);
    const double gate_span = score_bridge_angle(bridge_area, anchors, default_angle, spacing, infinite_span, false).max_span;
    if (gate_span < bad_span)
        return emit_single();

    // Iteratively carve out the area covered by good (anchored, short enough) lines along the best
    // direction, then re-detect on the rest.
    constexpr size_t max_sub_regions = 4;
    const double span_cap = std::max(bad_span, 0.5 * gate_span);
    const double min_sub_area = sqr(scaled<double>(2.));
    ExPolygons remaining = bridge_area;
    std::vector<std::pair<double, ExPolygons>> subs;
    for (size_t i = 0; i < max_sub_regions && ! remaining.empty(); ++ i) {
        std::optional<double> angle = best_anchored_angle(remaining, anchors, spacing, span_cap);
        if (! angle)
            break;
        BridgeAngleScore score = score_bridge_angle(remaining, anchors, *angle, spacing, span_cap, true);
        if (score.good_offsets.empty())
            break;
        // Convert runs of adjacent good lines into straight strips parallel to the bridging
        // direction. Straight strip boundaries keep the seams between sub-regions free of
        // sawtooth slivers that would otherwise be filled with gap infill hanging in the air.
        std::sort(score.good_offsets.begin(), score.good_offsets.end());
        score.good_offsets.erase(std::unique(score.good_offsets.begin(), score.good_offsets.end()), score.good_offsets.end());
        Polygons strips;
        {
            double  s  = sin(*angle);
            double  c  = cos(*angle);
            coord_t x0 = score.rot_bbox.min(0) - spacing;
            coord_t x1 = score.rot_bbox.max(0) + spacing;
            auto emit_strip = [&](coord_t lo, coord_t hi) {
                lo -= spacing / 2;
                hi += spacing / 2;
                Polygon poly;
                for (const Point &p : { Point(x0, lo), Point(x1, lo), Point(x1, hi), Point(x0, hi) })
                    poly.points.emplace_back(coord_t(round(c * p.x() - s * p.y())), coord_t(round(c * p.y() + s * p.x())));
                strips.emplace_back(std::move(poly));
            };
            coord_t lo = score.good_offsets.front(), prev = lo;
            for (coord_t o : score.good_offsets) {
                if (o - prev > coord_t(1.5 * spacing)) {
                    emit_strip(lo, prev);
                    lo = o;
                }
                prev = o;
            }
            emit_strip(lo, prev);
        }
        ExPolygons covered = intersection_ex(strips, to_polygons(remaining));
        // A strip may pick up a disconnected piece that had no good lines of its own
        // (e.g. the opposite side of a ring); keep only pieces printable at this angle.
        covered.erase(std::remove_if(covered.begin(), covered.end(),
            [&](const ExPolygon &ex) {
                if (ex.area() < min_sub_area)
                    return true;
                ExPolygons one{ ex };
                return score_bridge_angle(one, anchors, *angle, spacing, infinite_span, false).max_span > 1.25 * span_cap;
            }), covered.end());
        if (covered.empty())
            break;
        remaining = diff_ex(remaining, covered);
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
            [min_sub_area](const ExPolygon &ex) { return ex.area() < min_sub_area; }), remaining.end());
        subs.emplace_back(*angle, std::move(covered));
    }
    if (subs.empty())
        return emit_single();

    // Gap-free reassembly: whatever was not assigned to a sub-region (scraps below min_sub_area,
    // unfinished remainder) merges into the touching sub-region, so the union of the emitted
    // surfaces equals the input bridge area exactly.
    ExPolygons assigned;
    for (const auto &sub : subs)
        append(assigned, sub.second);
    for (ExPolygon &lo : diff_ex(bridge_area, assigned)) {
        Polygons lo_grown = offset(lo, float(spacing));
        size_t best = 0;
        double best_contact = -1.;
        for (size_t i = 0; i < subs.size(); ++ i) {
            double contact = 0.;
            for (const Polygon &p : intersection(lo_grown, to_polygons(subs[i].second)))
                contact += p.area();
            if (contact > best_contact) {
                best_contact = contact;
                best = i;
            }
        }
        if (best_contact <= 0.)
            // No contact at all, attach to the largest sub-region.
            best = std::max_element(subs.begin(), subs.end(), [](const auto &l, const auto &r) {
                    return area(l.second) < area(r.second);
                }) - subs.begin();
        subs[best].second.emplace_back(std::move(lo));
        subs[best].second = union_ex(subs[best].second);
    }

    // Keep the split only when it measurably improves on the single angle, measured on the final
    // sub-regions including the merged leftovers.
    double worst_after = 0.;
    for (const auto &[angle, region] : subs)
        worst_after = std::max(worst_after, score_bridge_angle(region, anchors, angle, spacing, infinite_span, false).max_span);
    if (worst_after > 0.6 * gate_span)
        return emit_single();

    Surfaces out;
    for (auto &[angle, region] : subs)
        emit(std::move(region), angle, out);
    return out;
}

Surfaces merge_bridges(
    std::vector<Bridge>& bridges,
    const std::vector<Algorithm::RegionExpansionEx>& bridge_expansions,
    const float closing_radius,
    const ExPolygons *lower_slices,
    const coord_t bridge_spacing
) {
    for (auto it = bridge_expansions.begin(); it != bridge_expansions.end(); ) {
        bridges[it->src_id].bridge_expansion_begin = it;
        uint32_t src_id = it->src_id;
        for (++ it; it != bridge_expansions.end() && it->src_id == src_id; ++ it) ;
    }

    Surfaces result;
    for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++ bridge_id) {
        if (group_id(bridges, bridge_id) == bridge_id) {
            // Head of the group.
            Polygons acc;
            for (uint32_t bridge_id2 = bridge_id; bridge_id2 < uint32_t(bridges.size()); ++ bridge_id2)
                if (group_id(bridges, bridge_id2) == bridge_id) {
                    append(acc, to_polygons(std::move(bridges[bridge_id2].expolygon)));
                    auto it_bridge_expansion = bridges[bridge_id2].bridge_expansion_begin;
                    assert(it_bridge_expansion == bridge_expansions.end() || it_bridge_expansion->src_id == bridge_id2);
                    for (; it_bridge_expansion != bridge_expansions.end() && it_bridge_expansion->src_id == bridge_id2; ++ it_bridge_expansion)
                        append(acc, to_polygons(it_bridge_expansion->expolygon));
                }
            if (!bridges[bridge_id].angle) {
                assert(false && "Bridge angle must be pre-calculated!");
            }
            //NOTE: The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
            // without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
            // look for narrow_ensure_vertical_wall_thickness_region_radius filter.
            ExPolygons final = closing_ex(acc, closing_radius);
            // without safety offset, artifacts are generated (GH #2494)
            // union_safety_offset_ex(acc)
            append(result, make_bridge_surfaces(
                std::move(final), lower_slices,
                bridges[bridge_id].angle ? *bridges[bridge_id].angle : -1.,
                bridge_spacing));
        }
    }
    return result;
}

struct ExpansionResult {
    Algorithm::WaveSeeds anchors;
    std::vector<Algorithm::RegionExpansionEx> expansions;
};

ExpansionResult expand_expolygons(
    const ExPolygons& expolygons,
    std::vector<ExpansionZone>& expansion_zones
) {
    using namespace Algorithm;
    WaveSeeds bridge_anchors;
    std::vector<RegionExpansionEx> bridge_expansions;

    unsigned processed_bridges_count = 0;
    for (ExpansionZone& expansion_zone : expansion_zones) {
        WaveSeeds seeds{wave_seeds(
            expolygons,
            expansion_zone.expolygons,
            expansion_zone.parameters.tiny_expansion,
            true
        )};
        std::vector<RegionExpansionEx> expansions{propagate_waves_ex(
            seeds,
            expansion_zone.expolygons,
            expansion_zone.parameters
        )};

        for (WaveSeed &seed : seeds)
            seed.boundary += processed_bridges_count;
        for (RegionExpansionEx &expansion : expansions)
            expansion.boundary_id += processed_bridges_count;

        expansion_zone.expanded_into = ! expansions.empty();

        append(bridge_anchors, std::move(seeds));
        append(bridge_expansions, std::move(expansions));

        processed_bridges_count += expansion_zone.expolygons.size();
    }
    return {bridge_anchors, bridge_expansions};
}

// Extract bridging surfaces from "surfaces", expand them into "shells" using expansion_params,
// detect bridges.
// Trim "shells" by the expanded bridges.
Surfaces expand_bridges_detect_orientations(
    Surfaces &surfaces,
    std::vector<ExpansionZone>& expansion_zones,
    const float closing_radius,
    const ExPolygons *lower_slices,
    const coord_t bridge_spacing
)
{
    using namespace Slic3r::Algorithm;

    double thickness;
    ExPolygons bridge_expolygons = fill_surfaces_extract_expolygons(surfaces, {stBottomBridge}, thickness);
    if (bridge_expolygons.empty())
        return {};

    // Calculate bridge anchors and their expansions in their respective shell region.
    ExpansionResult expansion_result{expand_expolygons(
        bridge_expolygons,
        expansion_zones
    )};

    std::vector<Bridge> bridges{get_grouped_bridges(
        std::move(bridge_expolygons),
        expansion_result.expansions
    )};
    bridge_expolygons.clear();

    std::sort(expansion_result.anchors.begin(), expansion_result.anchors.end(), Algorithm::lower_by_src_and_boundary);
    detect_bridge_directions(expansion_result.anchors, bridges, expansion_zones);

    // Merge the groups with the same group id, produce surfaces by merging source overhangs with their newly expanded anchors.
    std::sort(expansion_result.expansions.begin(), expansion_result.expansions.end(), [](auto &l, auto &r) {
        return l.src_id < r.src_id || (l.src_id == r.src_id && l.boundary_id < r.boundary_id);
    });
    Surfaces out{merge_bridges(bridges, expansion_result.expansions, closing_radius, lower_slices, bridge_spacing)};

    // Clip by the expanded bridges.
    for (ExpansionZone& expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, out);
    return out;
}

Surfaces expand_merge_surfaces(
    Surfaces &surfaces,
    SurfaceType surface_type,
    std::vector<ExpansionZone>& expansion_zones,
    const float closing_radius,
    const double bridge_angle
)
{
    using namespace Slic3r::Algorithm;

    double thickness;
    ExPolygons src = fill_surfaces_extract_expolygons(surfaces, {surface_type}, thickness);
    if (src.empty())
        return {};

    unsigned processed_expolygons_count = 0;
    std::vector<RegionExpansion> expansions;
    for (ExpansionZone& expansion_zone : expansion_zones) {
        std::vector<RegionExpansion> zone_expansions = propagate_waves(src, expansion_zone.expolygons, expansion_zone.parameters);
        expansion_zone.expanded_into = !zone_expansions.empty();

        for (RegionExpansion &expansion : zone_expansions)
            expansion.boundary_id += processed_expolygons_count;

        processed_expolygons_count += expansion_zone.expolygons.size();
        append(expansions, std::move(zone_expansions));
    }

    std::vector<ExPolygon> expanded = merge_expansions_into_expolygons(std::move(src), std::move(expansions));
    //NOTE: The current regularization of the shells can create small unasigned regions in the object (E.G. benchy)
    // without the following closing operation, those regions will stay unfilled and cause small holes in the expanded surface.
    // look for narrow_ensure_vertical_wall_thickness_region_radius filter.
    expanded = closing_ex(expanded, closing_radius);
    // Trim the zones by the expanded expolygons.
    for (ExpansionZone& expansion_zone : expansion_zones)
        if (expansion_zone.expanded_into)
            expansion_zone.expolygons = diff_ex(expansion_zone.expolygons, expanded);

    Surface templ{ surface_type, {} };
    templ.bridge_angle = bridge_angle;
    Surfaces out;
    out.reserve(expanded.size());
    for (auto &expoly : expanded)
        out.emplace_back(templ, std::move(expoly));
    return out;
}

void LayerRegion::process_external_surfaces(const Layer *lower_layer, const Polygons *lower_layer_covered)
{
    using namespace Slic3r::Algorithm;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Width of the perimeters.
    float shell_width = 0;
    float expansion_min = 0;
    if (int num_perimeters = this->region().config().wall_loops; num_perimeters > 0) {
        Flow external_perimeter_flow = this->flow(frExternalPerimeter);
        Flow perimeter_flow          = this->flow(frPerimeter);
        shell_width  = 0.5f * external_perimeter_flow.scaled_width() + external_perimeter_flow.scaled_spacing();
        shell_width += perimeter_flow.scaled_spacing() * (num_perimeters - 1);
        expansion_min = perimeter_flow.scaled_spacing();
    } else {
        // TODO: Maybe there is better solution when printing with zero perimeters, but this works reasonably well, given the situation
        shell_width   = float(SCALED_EPSILON);
        expansion_min = float(SCALED_EPSILON);;
    }

    // Scaled expansions of the respective external surfaces.
    float                           expansion_top           = shell_width * sqrt(2.);
    float                           expansion_bottom        = expansion_top;
    float                           expansion_bottom_bridge = expansion_top;
    // Expand by waves of expansion_step size (expansion_step is scaled), but with no more steps than max_nr_expansion_steps.
    static constexpr const float    expansion_step          = scaled<float>(0.1);
    // Don't take more than max_nr_steps for small expansion_step.
    static constexpr const size_t   max_nr_expansion_steps  = 5;
    // Radius (with added epsilon) to absorb empty regions emering from regularization of ensuring, viz  const float narrow_ensure_vertical_wall_thickness_region_radius = 0.5f * 0.65f * min_perimeter_infill_spacing;
    const float closing_radius = 0.55f * 0.65f * 1.05f * this->flow(frSolidInfill).scaled_spacing();

    // Expand the top / bottom / bridge surfaces into the shell thickness solid infills.
    double     layer_thickness;
    ExPolygons shells = union_ex(fill_surfaces_extract_expolygons(fill_surfaces.surfaces, { stInternalSolid }, layer_thickness));
    ExPolygons sparse = union_ex(fill_surfaces_extract_expolygons(fill_surfaces.surfaces, { stInternal }, layer_thickness));
    ExPolygons top_expolygons = union_ex(fill_surfaces_extract_expolygons(fill_surfaces.surfaces, { stTop }, layer_thickness));
    const auto expansion_params_into_sparse_infill = RegionExpansionParameters::build(expansion_min, expansion_step, max_nr_expansion_steps);
    const auto expansion_params_into_solid_infill  = RegionExpansionParameters::build(expansion_bottom_bridge, expansion_step, max_nr_expansion_steps);

    std::vector<ExpansionZone> expansion_zones{
        ExpansionZone{std::move(shells), expansion_params_into_solid_infill},
        ExpansionZone{std::move(sparse), expansion_params_into_sparse_infill},
        ExpansionZone{std::move(top_expolygons), expansion_params_into_solid_infill}
    };

    SurfaceCollection bridges;
    {
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges. layer" << this->layer()->print_z;
        const double custom_angle = this->region().config().bridge_angle.value;
        const coord_t bridge_spacing = this->bridging_flow(frSolidInfill, this->layer()->object()->config().thick_bridges).scaled_spacing();
        bridges.surfaces = custom_angle > 0 ?
            expand_merge_surfaces(fill_surfaces.surfaces, stBottomBridge, expansion_zones, closing_radius, Geometry::deg2rad(custom_angle)) :
            expand_bridges_detect_orientations(fill_surfaces.surfaces, expansion_zones, closing_radius,
                // Bridge splitting is disabled by passing no lower slices to anchor on.
                (this->region().config().split_external_bridges && lower_layer != nullptr) ? &lower_layer->lslices : nullptr,
                bridge_spacing);
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges - done";
#if 0
        {
            static int iRun = 0;
            bridges.export_to_svg(debug_out_path("bridges-after-grouping-%d.svg", iRun++), true);
        }
#endif
    }

    fill_surfaces.remove_types({ stTop });
    {
        Surface top_templ(stTop, {});
        top_templ.thickness = layer_thickness;
        fill_surfaces.append(std::move(expansion_zones.back().expolygons), top_templ);
    }

    expansion_zones.pop_back();

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_bottom, expansion_step, max_nr_expansion_steps);
    Surfaces bottoms = expand_merge_surfaces(fill_surfaces.surfaces, stBottom, expansion_zones, closing_radius);

    expansion_zones.at(0).parameters = RegionExpansionParameters::build(expansion_top, expansion_step, max_nr_expansion_steps);
    Surfaces tops = expand_merge_surfaces(fill_surfaces.surfaces, stTop, expansion_zones, closing_radius);

    //expansion_zone[0]: shell , expansion_zone[1]: sparse
    //apply minimu sparse infill area logic, this should also be added in bridge_over_infill
    if (!this->layer()->object()->print()->config().spiral_mode && this->region().config().sparse_infill_density.value > 0) {
        auto &sparse=expansion_zones[1].expolygons;
        auto &shells=expansion_zones[0].expolygons;
        double min_area = scale_(scale_(this->region().config().minimum_sparse_infill_area.value));
        ExPolygons areas_to_be_solid{};
        sparse.erase(std::remove_if(sparse.begin(), sparse.end(), [min_area, &areas_to_be_solid](ExPolygon& expoly) {
            if (expoly.area() <= min_area) {
                areas_to_be_solid.push_back(expoly);
                return true;
            }
            return false;
        }), sparse.end());

        if (!areas_to_be_solid.empty())
            shells = union_ex(shells, areas_to_be_solid);
    }


//    m_fill_surfaces.remove_types({ stBottomBridge, stBottom, stTop, stInternal, stInternalSolid });
    fill_surfaces.clear();
    unsigned zones_expolygons_count = 0;
    for (const ExpansionZone& zone : expansion_zones)
        zones_expolygons_count += zone.expolygons.size();
    reserve_more(fill_surfaces.surfaces, zones_expolygons_count + bridges.size() + bottoms.size() + tops.size());
    {
        Surface solid_templ(stInternalSolid, {});
        solid_templ.thickness = layer_thickness;
        fill_surfaces.append(std::move(expansion_zones[0].expolygons), solid_templ);
    }
    {
        Surface sparse_templ(stInternal, {});
        sparse_templ.thickness = layer_thickness;
        fill_surfaces.append(std::move(expansion_zones[1].expolygons), sparse_templ);
    }
    fill_surfaces.append(std::move(bridges.surfaces));
    fill_surfaces.append(std::move(bottoms));
    fill_surfaces.append(std::move(tops));

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

#else

#endif

void LayerRegion::prepare_fill_surfaces()
{
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-initial");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    /*  Note: in order to make the psPrepareInfill step idempotent, we should never
        alter fill_surfaces boundaries on which our idempotency relies since that's
        the only meaningful information returned by psPerimeters. */

    bool spiral_mode = this->layer()->object()->print()->config().spiral_mode;

#if 0
    // if no solid layers are requested, turn top/bottom surfaces to internal
    if (! spiral_mode && this->region().config().top_shell_layers == 0) {
        for (Surface &surface : this->fill_surfaces.surfaces)
            if (surface.is_top())
                //BBS
                //surface.surface_type = this->layer()->object()->config().infill_only_where_needed ? stInternalVoid : stInternal;
                surface.surface_type = PrintObject::infill_only_where_needed ? stInternalVoid : stInternal;
    }
    if (this->region().config().bottom_shell_layers == 0) {
        for (Surface &surface : this->fill_surfaces.surfaces)
            if (surface.is_bottom()) // (surface.surface_type == stBottom)
                surface.surface_type = stInternal;
    }
#endif

    // turn too small internal regions into solid regions according to the user setting
    if (! spiral_mode && this->region().config().sparse_infill_density.value > 0) {
        // scaling an area requires two calls!
        double min_area = scale_(scale_(this->region().config().minimum_sparse_infill_area.value));
        for (Surface &surface : this->fill_surfaces.surfaces)
            if (surface.surface_type == stInternal && surface.area() <= min_area)
                surface.surface_type = stInternalSolid;
    }

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-final");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

double LayerRegion::infill_area_threshold() const
{
    double ss = this->flow(frSolidInfill).scaled_spacing();
    return ss*ss;
}

void LayerRegion::trim_surfaces(const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices.surfaces)
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
	this->slices.set(intersection_ex(this->slices.surfaces, trimming_polygons), stInternal);
}

void LayerRegion::elephant_foot_compensation_step(const float elephant_foot_compensation_perimeter_step, const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices.surfaces)
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
    Polygons tmp = intersection(this->slices.surfaces, trimming_polygons);
    append(tmp, diff(this->slices.surfaces, opening(this->slices.surfaces, elephant_foot_compensation_perimeter_step)));
    this->slices.set(union_ex(tmp), stInternal);
}

void LayerRegion::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        svg.draw(surface->expolygon.lines(), surface_type_to_color_name(surface->surface_type));
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_slices_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_slices_to_svg(debug_out_path("LayerRegion-slices-%s-%d.svg", name, idx ++).c_str());
}

void LayerRegion::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const Surface &surface : this->fill_surfaces.surfaces) {
        svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
        svg.draw_outline(surface.expolygon, "black", "blue", scale_(0.05));
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_fill_surfaces_to_svg(debug_out_path("LayerRegion-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}

void LayerRegion::simplify_entity_collection(ExtrusionEntityCollection* entity_collection)
{
    for (size_t i = 0; i < entity_collection->entities.size(); i++) {
        if (ExtrusionEntityCollection* collection = dynamic_cast<ExtrusionEntityCollection*>(entity_collection->entities[i]))
            this->simplify_entity_collection(collection);
        else if (ExtrusionPath* path = dynamic_cast<ExtrusionPath*>(entity_collection->entities[i]))
            this->simplify_path(path);
        else if (ExtrusionMultiPath* multipath = dynamic_cast<ExtrusionMultiPath*>(entity_collection->entities[i]))
            this->simplify_multi_path(multipath);
        else if (ExtrusionLoop* loop = dynamic_cast<ExtrusionLoop*>(entity_collection->entities[i]))
            this->simplify_loop(loop);
        else
            throw Slic3r::InvalidArgument("Invalid extrusion entity supplied to simplify_entity_collection()");
    }
}

void LayerRegion::simplify_path(ExtrusionPath* path)
{
    const auto print_config = this->layer()->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    if (enable_arc_fitting &&
        !spiral_mode) {
        if (path->role() == erInternalInfill)
            path->simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
        else
            path->simplify_by_fitting_arc(scaled_resolution);
    } else {
        path->simplify(scaled_resolution);
    }
}

void LayerRegion::simplify_multi_path(ExtrusionMultiPath* multipath)
{
    const auto print_config = this->layer()->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < multipath->paths.size(); ++i) {
        if (enable_arc_fitting &&
            !spiral_mode) {
            if (multipath->paths[i].role() == erInternalInfill)
                multipath->paths[i].simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
            else
                multipath->paths[i].simplify_by_fitting_arc(scaled_resolution);
        } else {
            multipath->paths[i].simplify(scaled_resolution);
        }
    }
}

void LayerRegion::simplify_loop(ExtrusionLoop* loop)
{
    const auto print_config = this->layer()->object()->print()->config();
    const bool spiral_mode = print_config.spiral_mode;
    const bool enable_arc_fitting = print_config.enable_arc_fitting;
    const auto scaled_resolution = scaled<double>(print_config.resolution.value);

    for (size_t i = 0; i < loop->paths.size(); ++i) {
        if (enable_arc_fitting &&
            !spiral_mode) {
            if (loop->paths[i].role() == erInternalInfill)
                loop->paths[i].simplify_by_fitting_arc(SCALED_SPARSE_INFILL_RESOLUTION);
            else
                loop->paths[i].simplify_by_fitting_arc(scaled_resolution);
        } else {
            loop->paths[i].simplify(scaled_resolution);
        }
    }
}

}
