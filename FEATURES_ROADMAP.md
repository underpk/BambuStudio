# BambuStudio LAN Plus - Feature Roadmap

Features researched from OrcaSlicer, CrealityPrint, PrusaSlicer, and SuperSlicer.
Sorted by implementation effort and value.

## Already Implemented

- [x] Default to Line Type view + Prepare page on startup
- [x] Aligned Back seam position (from OrcaSlicer)
- [x] External/Internal Bridge Density settings (from OrcaSlicer)
- [x] Duplicate count in object list (1/6, 2/6)
- [x] Ironing Calibration tool
- [x] Printer selection dialog on Sync Info button
- [x] LAN Plus (persist, rename, delete, auto-connect printers)
- [x] Universal Profiles (compatible with all printers)
- [x] Physical Printer Binding (auto-connect + auto-sync on preset switch)
- [x] Support Interface Ironing (already in upstream BambuStudio)

## Tier 1: Easy Wins (1-2 files, <100 lines each)

### 1. Per-filament ironing settings
**Source:** OrcaSlicer v2.3.2
**Value:** High - PETG needs different ironing flow/speed than PLA
**What:** Allow ironing_flow, ironing_speed, ironing_spacing to be set per-filament instead of globally in process profile.
**Files:** PrintConfig.cpp (move options to filament config), Preset.cpp, Tab.cpp

### 2. Dense Infill Below Top Layers
**Source:** SuperSlicer
**Value:** High - eliminates pillowing on top surfaces
**What:** Automatically increases infill density for the last N layers before top solid layers. Setting: `dense_infill_below_top_layers` (int, default 0 = disabled).
**Files:** PrintConfig.cpp, PrintObject.cpp (infill density override), Tab.cpp

### 3. Auto-slice on settings change
**Source:** OrcaSlicer
**Value:** Medium - workflow improvement
**What:** Automatically re-slices when any setting changes, with configurable delay. Cancel and restart on rapid edits.
**Files:** Tab.cpp (on_value_change), Plater.cpp (auto-slice timer)

### 4. Polyhole Conversion
**Source:** SuperSlicer
**Value:** Medium - better dimensional accuracy for bolt holes
**What:** Auto-converts circular holes to inscribed polygons based on nozzle width for better dimensional accuracy.
**Files:** PerimeterGenerator.cpp or a new post-processing step

### 5. Show default values in tooltips
**Source:** OrcaSlicer
**Value:** Low-Medium - helps users understand what changed
**What:** Tooltips show the default value from parent/system profile alongside the description.
**Files:** OptionsGroup.cpp, Field.cpp

### 6. Fixed ironing angle
**Source:** OrcaSlicer
**Value:** Medium - eliminates tiger-striping on ironed surfaces
**What:** Option to use a fixed ironing direction every layer instead of alternating.
**Files:** PrintConfig.cpp (new option), Fill/FillRectilinear.cpp

### 7. First Layer Strong Start
**Source:** SuperSlicer
**Value:** Medium - better first layer adhesion
**What:** Extrudes a small blob at the start position before first layer begins.
**Files:** GCode.cpp (first layer start sequence)

### 8. Filament presets grouped by vendor/type
**Source:** OrcaSlicer
**Value:** Medium - much better UX with many filaments
**What:** Four grouping modes for filament dropdown: All, None, By Vendor, By Type.
**Files:** PresetComboBoxes.cpp

### 9. Show/hide gridlines toggle
**Source:** OrcaSlicer
**Value:** Low - quick visual preference
**What:** Toggle build plate gridlines from View menu or right-click context menu.
**Files:** GLCanvas3D.cpp, PartPlate.cpp

### 10. Configurable filament area height
**Source:** OrcaSlicer
**Value:** Low-Medium - reduces scrolling with 16+ filaments
**What:** User-configurable height for the filament list area in sidebar.
**Files:** Plater.cpp (sidebar layout)

## Tier 2: Medium Effort (3-5 files, 100-500 lines)

### 11. Brim Ears
**Source:** SuperSlicer + CrealityPrint
**Value:** High - very popular community feature
**What:** Corner-only brim reinforcement instead of full perimeter brim. Uses convex hull corner detection to place brim only where needed.
**Files:** PrintConfig.cpp, Brim.cpp, Tab.cpp, possibly new BrimEars.cpp

### 12. Per-feature flow rate tuning
**Source:** OrcaSlicer
**Value:** High - tune flow independently for outer walls, inner walls, infill, overhangs, gap fill, supports
**What:** Separate flow ratio settings per extrusion type. Eliminates wrinkle effect on thin layers.
**Files:** PrintConfig.cpp (6+ new options), GCode.cpp (apply per-feature flow), Tab.cpp

### 13. Interior Brim
**Source:** SuperSlicer
**Value:** Medium - better adhesion for parts with internal holes
**What:** Generate brim on internal surfaces/holes.
**Files:** Brim.cpp, PrintConfig.cpp, Tab.cpp

### 14. Over-Bridge Flow Compensation
**Source:** SuperSlicer
**Value:** Medium - fixes Z-height issues after bridges
**What:** Adjusts extrusion height on layers above bridges to correct accumulated Z error.
**Files:** GCode.cpp, Layer.cpp

### 15. Organic Support Base Infill Patterns
**Source:** OrcaSlicer
**Value:** Medium - stronger tree support bases
**What:** Selectable infill patterns (rectilinear, grid, honeycomb) for the base of organic/tree supports instead of hollow.
**Files:** TreeSupport.cpp, SupportCommon.cpp, PrintConfig.cpp

### 16. Wipe Tower Stability for Mixed Materials
**Source:** OrcaSlicer
**Value:** High for multi-material users
**What:** Temperature boost, pre-extrusion, flushing/notch, skip ironing on interface layers. Prevents tower collapse with PLA+PETG.
**Files:** GCode/WipeTower.cpp, PrintConfig.cpp

### 17. Scattered Rectilinear Infill
**Source:** SuperSlicer
**Value:** Low-Medium - aesthetic/structural variety
**What:** Randomized rectilinear infill pattern for varied internal structure.
**Files:** Fill/FillRectilinear.cpp, PrintConfig.cpp

### 18. Custom G-code Variables
**Source:** PrusaSlicer v2.9.3
**Value:** Medium - power user feature
**What:** User-defined JSON variables in profiles, usable as placeholders in custom G-code sections.
**Files:** PlaceholderParser.cpp, PrintConfig.cpp

### 19. Verbose G-code Output
**Source:** OrcaSlicer
**Value:** Low-Medium - debugging aid
**What:** Adds explanatory comments to every G-code line for human readability.
**Files:** GCode.cpp (comment generation)

## Tier 3: Hard (5+ files, 500+ lines)

### 20. Perimeter Looping
**Source:** SuperSlicer
**Value:** High - fewer seams, continuous extrusion
**What:** Connects all perimeters into one continuous path without travel moves between inner/outer walls.
**Files:** PerimeterGenerator.cpp, ExtrusionEntity.cpp, GCode.cpp

### 21. Instances (lightweight duplicates)
**Source:** OrcaSlicer
**Value:** High for batch printing - much faster slicing
**What:** Lightweight duplicates sharing mesh data. "Fill Bed With Instances" for fast bed filling.
**Files:** Model.cpp, ModelObject.cpp, Print.cpp, Plater.cpp

### 22. Avoid Travel Island Pathfinding
**Source:** SuperSlicer
**Value:** Medium - reduces ooze marks
**What:** Smart travel routing that avoids crossing printed islands, minimizing ooze on external surfaces.
**Files:** GCode/TravelPathfinding.cpp, GCode.cpp

### 23. Sequential Print Collision Detection
**Source:** PrusaSlicer v2.9.1
**Value:** Medium - safety feature
**What:** Automatic object positioning and ordering to avoid extruder collisions during sequential printing.
**Files:** Arrange.cpp, Print.cpp, GLCanvas3D.cpp

### 24. Fuzzy Skin Painting
**Source:** PrusaSlicer v2.9.0
**Value:** Medium - creative feature
**What:** Paint fuzzy skin regions directly on model surface like support painting.
**Files:** FuzzySkin.cpp, GLGizmoPaint.cpp, new gizmo

### 25. Embedding Wall into Infill
**Source:** CrealityPrint v7.0
**Value:** Medium - structural improvement
**What:** Periodic interlocking tooth structures between walls and infill for improved interlayer bonding.
**Files:** PerimeterGenerator.cpp, Fill.cpp

### 26. Multi-Bed-Size Plates
**Source:** Custom (attempted, shelved)
**Value:** High for multi-printer users
**What:** Different plates can have different bed sizes from different printers. Requires per-plate VBO rendering refactor.
**Files:** PartPlate.cpp/hpp, Plater.cpp, GLCanvas3D.cpp (deep architectural change)

## Priority Recommendation

**Phase 1 (Quick wins - do first):**
1, 2, 5, 6, 9

**Phase 2 (Medium effort, high value):**
11, 12, 15, 16

**Phase 3 (Hard but worth it):**
20, 21
