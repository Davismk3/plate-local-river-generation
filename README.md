# Plate-Local River Generation

**Michael K. Davis III** — June 2026

A deterministic procedural world-generation system that produces continents and terrain-aware, downhill-flowing river networks without precomputing an entire planet. River geometry is generated lazily per continent, while terrain is evaluated pointwise on demand.

This system was originally designed for a video game I am developing. I am currently generalizing and annotating the codebase to be non-specific to that video game's mechanics.

This repository is currently a WIP.

## Examples

**Infinite flat world**
![Infinite flat world](figures/infinite_flat.gif)

## (Anticipated) Repository Architecture

```
plate-local-river-generation/
├── davis2026_plate_local_river_generation.pdf
├── README.md
├── LICENSE
├── figures/
├── flat-infinite/
│   ├── src/
│   ├── examples/
│   └── README.md
├── cube-planet/
│   ├── src/
│   ├── examples/
│   └── README.md
└── sphere-planet/
    ├── src/
    ├── examples/
    └── README.md
```

## Documentation

The full technical summary is available in
[davis2026_plate_local_river_generation.pdf](davis2026_plate_local_river_generation.pdf).

## Additional Media

Youtube Video Showing Results: https://youtu.be/4Ai_13znvgg \
Discord Server: https://discord.gg/fPAwD7x4va

## License

This work is licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
