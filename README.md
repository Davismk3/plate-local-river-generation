# Plate-Local River Generation

**Michael K. Davis III** — June 2026

This repository documents the Plate-Local River Generation Algorithm (PL-RGA), a deterministic procedural world-generation system that produces continents and terrain-aware, downhill-flowing river networks without precomputing the entire world environment. River geometry is generated lazily per continent, while terrain is evaluated pointwise on demand.

The PL-RGA is ${\color{red}\text{not specific to rivers}}$. The same core algorithm can be adapted for road networks, volcano placement, settlement placement, or other features that require a hard-constrained topology and/or direct control over continental placement. 

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

NOTE: Converting this multi-month's long personal coding project into concise works with citations is a project on its own. I'm still piecing together what does or does not need citing/mentioning/elaborating. This is not the final draft and may have mistakes. If anything looks wrong or needs elaborating, let me know.

## Additional Media

Youtube Video Showing Results: https://youtu.be/4Ai_13znvgg \
Discord Server: https://discord.gg/fPAwD7x4va

## License

This work is licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
