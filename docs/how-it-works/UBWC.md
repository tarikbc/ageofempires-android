# Colour compression for vkd3d-proton's render targets (v1.5.0, 2026-10-09)

**Result:** with the driver `turnip-main-c78a9e9-ubwc` and two container variables, the game's largest colour render
targets keep UBWC, Adreno's lossless framebuffer compression, and AoE IV gains about **+1.5 FPS (+2.6 %)** in the skirmish
benchmark (interleaved A/B below). Without the variables the driver behaves like the v1.3.0 driver.

```
TU_UBWC_RGBA8_IGNORE_R32=1
TU_UBWC_RGBA16F_INT=1
```

## The problem

After the driver and vkd3d-proton fixes the skirmish benchmark is GPU-bound: the GPU is busy about 95 % of the time at
its top clock. Memory traffic is a large part of that work. Turning UBWC off for every image (`TU_DEBUG=noubwc`) cost
5 %: 56.1 / 55.5 FPS against 58.8 / 58.9 the same session.

Turnip's own performance log (`TU_DEBUG=perf`) showed that the game's main colour targets had no UBWC at all:

```
Disabling UBWC on 1280x720 PIPE_FORMAT_R8G8B8A8_UNORM resource due to mutable formats (fmt list present)
Disabling UBWC on 1280x720 PIPE_FORMAT_R16G16B16A16_FLOAT resource due to mutable formats (fmt list present)
```

## Why

The game creates typeless D3D12 resources. vkd3d-proton turns each into a Vulkan image with `MUTABLE_FORMAT` and a
list of every format the resource may be viewed in (usage `0x1f`, including storage). A driver build that printed
the lists showed:

| Image | Format list from vkd3d-proton |
|---|---|
| RGBA8 targets: 1280 × 720, 1440 × 720, 518 × 720 and smaller | R8G8B8A8 UNORM, UINT, SINT, SRGB, SNORM, then R32 UINT, SINT, FLOAT |
| 1280 × 720 R16G16B16A16_FLOAT | R16G16B16A16 FLOAT, UNORM, SNORM, UINT, SINT |
| 1280 × 720 R16_UINT | R16 UINT, FLOAT, SINT, UNORM, SNORM |
| 640 × 360 R16G16_FLOAT | R16G16 FLOAT, UINT, SINT, UNORM, SNORM, then R32 UINT, SINT, FLOAT |

UBWC data can be read in another format only if both formats share a compression class. On the A740, Turnip takes the
classes from what Qualcomm's own driver allows (`src/freedreno/common/freedreno_ubwc.h`): UNORM, SNORM and integer
variants of one layout share a class, but R32 formats and the float formats do not. One incompatible entry in the list
turns UBWC off for the whole image (`tu6_mutable_format_list_ubwc_compatible`).

## How the fix was made safe

A list only says what the application *may* do. A second driver build turned UBWC on for every list
(`TU_UBWC_FORCE_COMPAT=1`, not shipped) and logged each image view whose format is in another class than its UBWC
image. In a full benchmark run the game created such views only on two kinds of image:

- `R16_FLOAT` views of the `R16_UINT` images (512 × 512, 545 × 545, 32 × 32 and 1280 × 720), as storage, sampled
  image and colour attachment;
- `R16G16_UNORM` views of the 640 × 360 `R16G16_FLOAT` image.

It never viewed an RGBA8 target as R32, and never viewed the R16G16B16A16_FLOAT target in another format. So the
shipped patch ([patches/turnip/0001](../../patches/turnip/0001-ubwc-opt-in-for-vkd3d-typeless-targets.patch))
allows exactly the two safe cases and nothing else:

- `TU_UBWC_RGBA8_IGNORE_R32=1`: an R8G8B8A8 family list may also name the R32 formats.
- `TU_UBWC_RGBA16F_INT=1`: an R16G16B16A16_SFLOAT list may also name its UNORM, SNORM, UINT and SINT variants.

The R16 and R16G16 images keep their uncompressed layout. When either variable is set, the driver also warns in
logcat if an application ever views a compressed image in another class:

```
UBWC opt-in: <format> view of a compressed <format> image (<w>x<h>) is in another UBWC class
```

That warning did not appear in the logged runs with the two variables. The screenshots taken in the forced and the
safe-set runs show the match as usual: terrain, trees, buildings, units, fog of war and HUD.

## Measurements

Skirmish benchmark on GameNative 1.3.0 (test release), v1.4.0 FEX package, `VKD3D_CONFIG=no_staggered_submit`. The
Thor's FPS drifted down by about 1 FPS over the afternoon, so the final check alternates the two drivers:

| Run | Driver and variables | FPS (minute 1) |
|---|---|---|
| ab-base1, 15:12 | `turnip-main-c78a9e9` | 58.1 |
| ab-safe1, 15:21 | the patch, both variables | 60.1 |
| ab-base2, 15:31 | `turnip-main-c78a9e9` | 58.6 |
| ab-safe2, 15:40 | the patch, both variables | 59.6 |

Mean of the pairs: 58.4 against 59.9 FPS, +1.5 FPS (+2.6 %).

Earlier the same day (windows at minutes 1 / 3):

| Run | FPS |
|---|---|
| Release driver, 11:14 / 12:22 / 13:46 / 14:28 | 58.8 / 58.9, 59.2 / 58.8, 58.6 / 58.5, 59.0 / 58.3 |
| UBWC forced on every list (not shipped) | 60.4 / 60.3, with the view log 60.2 / 60.0 |
| Both variables (first build of the patch) | 60.8 / 60.6; 58.5 at 14:42 against 57.8 for the release driver at 15:02 |
| `TU_UBWC_RGBA8_IGNORE_R32=1` alone | 59.8 / 59.9, then 59.0 / 58.9 |
| `TU_UBWC_RGBA16F_INT=1` alone | 58.0 (14:52) |
| UBWC off everywhere (`TU_DEBUG=noubwc`) | 56.1 / 55.5 |

Most of the gain comes from the two rules together; each alone stayed within the spread. Frames over 50 ms did not
change.

## Not tested yet

- A full game, and the late-game replay, with the variables.
- Other games: the rules are general for vkd3d-proton titles, but a game that does view these images as R32 would show
  wrong colours there (and the logcat warning). AoE II DE uses DXVK and another driver.
- Reported upstream: not yet. The real fix belongs in vkd3d-proton (add the R32 views only when a typed UAV clear
  needs them) or in Turnip (decompress when an incompatible view is used).
