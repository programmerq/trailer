# Trailer app icon — Blender pipeline

Parametric 3D scene rendered through `linuxserver/blender` in Docker. Headless,
ray-traced (Cycles), CPU-only on this machine (Docker on macOS has no GPU
passthrough).

## Files

- `render.py` — the entire scene as Blender Python. All knobs (loupe geometry,
  film strip dimensions, materials, lighting, camera) live near the top of
  each helper. Edit, re-run, view the PNG.
- `render.sh` — runs `render.py` inside the Blender container with the worktree
  bind-mounted into the container at the same path.
- `render_all.sh` — renders all five size variants (64, 128, 256, 512, 1024).
- `inputs/` — drop your high-res Arches JPEG here as `arches.jpg`. If the file
  is missing, the script falls back to a procedural sky-blue → canyon-red
  gradient so the pipeline still runs.
- `output/` — rendered PNGs land here.

## Quick start

```bash
# One-off render at any size.
./render.sh --size 1024 --samples 256 --out output/icon_1024.png

# Lower-res quick previews while iterating on the scene.
./render.sh --size 256 --samples 48 --out output/preview.png

# All sizes at once.
./render_all.sh
```

CLI flags (forwarded to `render.py`):

| flag         | default                | notes                                   |
|--------------|------------------------|-----------------------------------------|
| `--size`     | `1024`                 | Output is square (`size × size`).       |
| `--samples`  | `256`                  | Cycles samples. 32–64 for previews.     |
| `--out`      | `output/icon.png`      | PNG with alpha (transparent background).|
| `--photo`    | `inputs/arches.jpg`    | Used as the film-frame texture.         |
| `--engine`   | `CYCLES`               | `BLENDER_EEVEE_NEXT` for fast previews. |
| `--background` | `transparent`        | Or `rounded_white` (TODO).              |

## Iteration loop

The point of this setup is a tight `tweak → render → look → tweak` cycle:

1. Open `render.py`, change a parameter (e.g. `lens_dome`, `housing_inner_r`,
   IORs in `make_clear_abs_material`, light positions in `make_lighting`).
2. `./render.sh --size 384 --samples 64 --out output/preview.png`
3. Open `output/preview.png` in Preview.app or your editor.
4. Repeat. A 384px / 64-sample preview is on the order of seconds; a 1024px /
   256-sample final is ~1–2 minutes.

## Brief alignment

| Brief item                              | Where it lives in `render.py`                |
|-----------------------------------------|----------------------------------------------|
| Film strip diagonal, 7 → 2 o'clock      | `strip.rotation_euler` in `build_scene`      |
| 35 mm × 36×24 mm frames                 | `make_film_strip` defaults                   |
| Sprocket holes (KS-1870-ish pitch)      | `_build_border_mesh`                         |
| Three frames, same photo, separators    | `_build_frame_separators` + UV `Scale`       |
| Loupe rect→circle morph                 | `make_loupe_body` (superellipse interpolation) |
| Black ABS housing ring                  | `make_housing_ring`                          |
| Optical glass plano-convex lens         | `make_plano_convex_lens`                     |
| Photo-driven palette                    | `--photo` flag                               |
| Mild barrel distortion through plastic  | `make_clear_abs_material(ior=1.20)` (low IOR)|
| Diffuse, soft, cartoon-ish lighting     | `make_lighting` + world ambient              |

## Known limitations / future work

- **Lens see-through is muted.** Chained Fresnel losses across body+lens make
  the photo dim through the lens center even with low IORs and bumped
  emission. To fix, either bake the photo as a stronger emitter or replace the
  lens material with a NPR shader that "fakes" the see-through.
- **Chromatic aberration (1024+ tier) not yet implemented.** Easiest path: a
  compositor node tree that splits R/G/B and offsets at the lens rim.
- **Lens coating iridescence not yet implemented.** Add a Layer Weight →
  Color Ramp drive into a thin glossy mix on the dome.
- **Per-size scene tweaks not yet wired.** The brief asks for sprocket-hide at
  ≤128, lens-coating-arc only at ≥512, etc. Today every size renders the same
  scene; we can add `--detail-tier {silhouette,basic,full,maxed}` flags to
  `render.py` that gate features.
- **SVG export.** The render is raster. To get SVG-as-source-of-truth, the path
  is: render the geometry passes from Cycles, vector-trace the geometry layers
  (vtracer or Inkscape's `Trace Bitmap`), and keep the optical-effects layer
  as embedded raster inside the SVG.
