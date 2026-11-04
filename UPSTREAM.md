# Upstream: Aravis Viewer

Limrendir started in January 2023 as a modified copy of the
[Aravis](https://github.com/AravisProject/aravis) viewer (`viewer/` in the aravis repository). Upstream changes are ported by hand.

**Base:** aravis `f06f36604` (2022-12-28), the last commit touching `viewer/` before limrendir
was created.

**Last checked:** aravis `482547a3d` (2026-10-22).

**Supported aravis versions:** 0.8 and 0.10. Any port that uses a newer API must be wrapped
in `#if ARAVIS_CHECK_VERSION (...)` with a 0.8 fallback (see the compatibility macros at the
top of `limrendir.h`).

## Upstream commits since the base

| Aravis commit | Date | Summary | Status | Notes | Limrendir commit |
|---|---|---|---|---|---|
| `1fc8310cb` | 2023-02-10 | Component selector | 🔴 undecided | Multi-part cameras only. Large UI change (~750 lines). | |
| `c4a70719e` | 2023-02-10 | Reset flip button state | 🟢 ported | Done in `start_video` (every pipeline restart, including after recordings), and also resets `viewer->flip_*`, which upstream leaves stale. | |
| `738bd0f44` | 2023-02-10 | Hide binning or offset controls if not available | 🔴 pending | Limrendir already checks availability in `limrendir_modechange.c` (sensitivity); compare and hide instead. | |
| `7bfa356dd` | 2023-02-10 | Make the initial window a bit bigger | 🟢 skipped | Limrendir has its own layout. | |
| `6e1311b8b` | 2023-07-25 | Back to 10 buffers in the stream queue | 🟢 covered | Limrendir uses its own buffer counts (`n_buffers_streaming`, `n_buffers`). | |
| `0b48b0ea9` | 2023-07-25 | Ensure there are still available buffers | 🔴 pending | Skip displaying a buffer when the stream thread would run out of them. Small. | |
| `051ccee8e` | 2023-08-01 | Application id consistency fix | 🟢 skipped | Aravis-specific id and appdata. | |
| `ba0d728d0` | 2023-09-20 | Use representation for exposure time and gain widgets | 🔴 pending | Port together with `9371337a2`, `2a29f8a83`, `85e5a2fd4` (camera settings). | |
| `51fa0f73d` | 2023-11-20 | New `arv_stream_get_n_owned_buffers` | 🟢 covered | Compatibility macro in `limrendir.h`. | |
| `42854d792` | 2023-11-18 | (gentl) whitespace cleanup in viewer | 🟢 skipped | Whitespace only. | |
| `01d3d9ddc` | 2023-11-24 | v4l2 device support (icon) | 🟢 skipped | Icon only. Revisit if v4l2 devices should be listed. | |
| `9371337a2` | 2024-03-12 | Better exposure/gain widget with undefined representation | 🔴 pending | See `ba0d728d0`. | |
| `05fd84cc2` | 2024-08-27 | Workaround for BlackFly U16 pixel endianness | 🔴 undecided | Only for BlackFly cameras with 16-bit formats. | |
| `11d643ba7` | 2024-09-03 | Correct appstream metainfo | 🟢 skipped | Aravis packaging. | |
| `2a29f8a83` | 2024-09-04 | Ignore feature representation for exposure time | 🔴 pending | See `ba0d728d0`. | |
| `d50b09555` | 2025-01-22 | Replace `create_stream` by `create_stream_full` | 🟢 covered | Compatibility macro in `limrendir.h`. | |
| `d3495deaa` | 2025-02-14 | Add SPDX license identifier | 🟢 skipped | Licence headers. | |
| `ed2dd3bc9` | 2025-02-16 | License change to LGPL-2.1-or-later | 🟢 skipped | Licence headers. | |
| `85e5a2fd4` | 2025-11-11 | Exposure time and gain increment functions | 🔴 pending | `arv_camera_get_{exposure_time,gain}_increment` are not in 0.8: needs `ARAVIS_CHECK_VERSION` guard. See `ba0d728d0`. | |
| `57983d013` | 2025-11-11 | Require gtksink | 🟢 ported | Limrendir already avoids gtkglsink for display; drop the leftover detection/fallback code in `limrendir.c`. Port with `28e0cb133`. | |
| `28e0cb133` | 2025-11-11 | Remove dead gtkglsink code | 🟢 ported | See `57983d013`. | |
| `c34a49538` | 2026-03-22 | Display support for 10/12-bit packed formats | 🔴 undecided | Mono12P/Packed, BayerRG10P/12P... Display only; port with `3c8bf4b46`. Large (~800 lines). | |
| `3c8bf4b46` | 2026-04-01 | Unpacking with reusable buffer and worker thread | 🔴 undecided | See `c34a49538`. | |
| `214f84065` | 2026-05-11 | Remove unused include | 🟢 skipped | Include not present in limrendir. | |
