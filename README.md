# BambuStudio LAN Plus

A feature-enhanced fork of [BambuStudio](https://github.com/bambulab/BambuStudio) focused on LAN printer management, quality-of-life improvements, and advanced slicing features sourced from OrcaSlicer, SuperSlicer, PrusaSlicer, and CrealityPrint.

## Downloads

Prebuilt binaries for **Windows**, **macOS** (ARM + Intel), and **Linux** (Ubuntu 22.04/24.04) are available from [GitHub Actions artifacts](https://github.com/underpk/BambuStudio/actions).

## LAN Plus Features

- **LAN Printer Persistence** - Save, rename, delete, and auto-connect LAN printers without Bambu Cloud
- **Physical Printer Binding** - Bind a printer to a preset for auto-connect and auto-sync on preset switch
- **Universal Profiles** - Profiles compatible with all printers, no more "incompatible printer" warnings

## Slicing Enhancements

- **Per-filament Ironing Overrides** - Override ironing flow, speed, and spacing per filament (e.g. different settings for PETG vs PLA) via the filament Setting Overrides tab
- **Aligned Back Seam** - Seam position option from OrcaSlicer
- **External/Internal Bridge Density** - Separate bridge density controls from OrcaSlicer
- **Support Interface Ironing** - Iron support interface layers for smoother contact surfaces

## UI Improvements

- **Default to Line Type view** - Opens on Prepare page with Line Type coloring
- **Duplicate Count in Object List** - Shows instance count (1/6, 2/6, etc.)
- **Ironing Calibration Tool** - Built-in calibration for ironing settings
- **Printer Selection Dialog** - Printer picker on Sync Info button

## How to Compile

### Windows
```batch
build_win.bat -d "path\to\deps"
```

### macOS
```bash
./BuildMac.sh -ds   # Build deps then slicer
```

### Linux (Ubuntu)
```bash
sudo ./BuildLinux.sh -u   # First time: install system deps
./BuildLinux.sh -dsi       # Build deps, slicer, and AppImage
```

See the [wiki](https://github.com/bambulab/BambuStudio/wiki) for detailed compile guides.

## CI/CD

GitHub Actions builds all platforms automatically on push to master:
- Ubuntu 22.04 / 24.04 (AppImage)
- Windows (portable zip)
- macOS ARM (arm64) and Intel (x86_64)

Dependencies are cached after first build for faster subsequent runs.

## Credits

Based on [BambuStudio](https://github.com/bambulab/BambuStudio) by Bambu Lab, which is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

Features researched and ported from [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer), [SuperSlicer](https://github.com/supermerill/SuperSlicer), [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer), and [CrealityPrint](https://www.creality.com/).

## License

Licensed under the GNU Affero General Public License, version 3. See [LICENSE](LICENSE) for details.
