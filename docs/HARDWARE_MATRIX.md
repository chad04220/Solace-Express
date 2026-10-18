# Hardware matrix

The game's system requirements must come from measurements on real PCs. This page says which PCs to measure, how to
collect the measurements, and how to turn them into the requirements in the README.

## Collecting

On each PC, with the release build:

1. `diagnostics.bat` (the full run, 15–25 minutes). It records the system (GPU, driver, video memory, CPU, memory,
   Windows, monitors), times the first-run shader compile, and benchmarks every scene full screen at 1920x1080,
   research craft included.
2. `diagnostics.bat soak` (two hours). One long session with a new scene every two minutes. It logs the frame rate,
   memory and handles every 10 s, and says whether anything grew.

Each run leaves a `diagnostics_<version>.zip`. Then:

```
python tools/hardware_matrix.py pc1.zip pc2.zip pc3/ --md matrix.md --csv matrix.csv
```

You get one row per PC. Each scene shows its frame rate and 1% low, judged against the 60 fps target:

- **OK**: the average holds 60 fps and the 1% low holds 50.
- **SLOW**: the average holds 60 fps but the 1% low doesn't, so the scene stutters.
- **BELOW**: the average doesn't reach 60 fps.

## The PCs to cover

Start with at least one PC per row. Laptops count separately from desktops, because their GPUs share power with the
CPU and their drivers switch between two GPUs.

| Tier | GPUs (any one) | Why |
|---|---|---|
| Integrated | Intel UHD 620 / 770, Iris Xe, Arc (laptop); AMD Radeon 680M / 780M | The floor: many laptops have nothing else |
| Entry | GTX 1050 Ti / 1650, RX 570 / 6500 XT, Arc A380 | The likely minimum |
| Mainstream | GTX 1660, RTX 2060 / 3060 / 4060, RX 6600 / 7600 | The likely recommended tier |
| High | RTX 3070–4070 Ti, RX 6800 / 7800 XT | 1440p and high refresh rates |
| Top | RTX 4080 / 4090 / 5080, RX 7900 | 4K |

Also cover at least one of each of these:

- 8 GB of system memory
- a 4-core CPU
- Windows 10 and Windows 11
- a laptop with NVIDIA Optimus or AMD switchable graphics
- a 144 Hz monitor
- a 4K monitor at 150% scaling

## From the matrix to the requirements

- **Minimum**: the slowest tier on which the main scenes (`air`, `forest`, `night`, `cockpit`, `hud`) are OK or SLOW
  at Low quality and Auto resolution.
- **Recommended**: the slowest tier on which every scene is OK at Medium, research craft included.
- **Memory and disk**: the largest working set the soak saw, and the unpacked size plus the cache's size (`cache.txt`
  in the diagnostics).

Until the matrix has those rows, the README says what is checked and measured, and nothing more.
