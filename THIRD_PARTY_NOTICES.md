# Third-party notices

## DejaVu Sans Bold (embedded as a signed-distance-field atlas in `src/font_data.h`)

Fonts are (c) Bitstream (see below). DejaVu changes are in public domain.

Bitstream Vera Fonts Copyright: Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved. Bitstream Vera is a trademark of Bitstream, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy of the fonts accompanying this license ("Fonts") and associated documentation files (the "Font Software"), to reproduce and distribute the Font Software, including without limitation the rights to use, copy, merge, publish, distribute, and/or sell copies of the Font Software, and to permit persons to whom the Font Software is furnished to do so, subject to the following conditions:

The above copyright and trademark notices and this permission notice shall be included in all copies of one or more of the Font Software typefaces.

The Font Software may be modified, altered, or added to, and in particular the designs of glyphs or characters in the Fonts may be modified and additional glyphs or characters may be added to the Fonts, only if the fonts are renamed to names not containing either the words "Bitstream" or the word "Vera".

THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM OTHER DEALINGS IN THE FONT SOFTWARE.

## Internet radio

Stations listed in `radio_stations.txt` are third-party services streamed live at the player's request. Solace Express does not host, cache or redistribute any audio. Check each station's terms before featuring them in marketing material.

## Voices (`voice/`)

All voices were synthesised for this project with stock synthetic speakers, then headset- or radio-filtered; no real person's voice was used. The instructor, the checkride examiner and the display pilot use Qwen3-TTS-12Hz-1.7B-CustomVoice (stock speakers Ryan, Sohee and Aiden), licensed under the Apache License 2.0. The airport information, cockpit assistant and research computer voices, and the North, Coast and Valley Tower controllers, use the Kokoro-82M v1.0 model (stock voices bf_emma, af_bella, am_fenrir, am_michael, af_sarah and bm_daniel) through kokoro-onnx; Kokoro-82M is licensed under the Apache License 2.0 and kokoro-onnx under the MIT License. The licence texts are in `voice/licenses/`. The recordings themselves are part of this project.

## Material textures (`materials/`)

The photo-scanned surface textures in `materials/` (grass, rock, sand, snow, asphalt, gravel, soil, concrete, roof
tiles, slate, plaster, brick, brushed metal, plastic, fabric, carpet, leather, corrugated iron, bark, planks, forest
floor, shingles and siding) are derived from CC0 (public domain) assets from Poly Haven (polyhaven.com) and ambientCG
(ambientcg.com), resampled and recoloured for this project by `tools/pack_materials.py`, which lists every source.
CC0 needs no attribution; it is given with thanks.

The independent photographed environment set in `materials/environment/` (512 px fallback) and
`materials/high/` (2048 px) is packed by `tools/pack_environment_scans.py` without procedural-palette matching:
- Grass: **Grass004**, ambientCG, https://ambientcg.com/view?id=Grass004 (its colour and roughness matched to the islands' grass layer)
- Asphalt: **Asphalt 04**, Sergej Majboroda (photography), Jenelle van Heerden (processing), https://polyhaven.com/a/asphalt_04
- Concrete: **Concrete Floor Worn 001**, Dimitrios Savva (photography), Rico Cilliers (processing), https://polyhaven.com/a/concrete_floor_worn_001

- Brick: **Red Brick**, Rob Tuytel, https://polyhaven.com/a/red_brick
- Plaster: **Painted Plaster Wall**, Amal Kumar, https://polyhaven.com/a/painted_plaster_wall
- Roof tile: **Clay Roof Tiles 02**, Amal Kumar, https://polyhaven.com/a/clay_roof_tiles_02
- Bark: **Bark Brown 02**, Rob Tuytel, https://polyhaven.com/a/bark_brown_02

All seven are CC0 1.0: https://polyhaven.com/license, https://ambientcg.com/license and https://creativecommons.org/publicdomain/zero/1.0/legalcode.
`assets/materials/environment/manifest.json` records exact official source URLs, source SHA-256 hashes, packed-file
hashes, dimensions and transformations. Source photographs/PNG downloads are not bundled; only the compact runtime
maps are redistributed. The existing aircraft/cockpit-compatible material files remain unchanged.

## Optional Earth data packs (Phase 0 tools)

The offline `tools/earth/` prototype does not replace the game's world. Its checked-in synthetic test fixture is authored test data, not GRIP or a geographic survey. External sources are not silently relicensed by the tool. Each built pack records its own inputs, transformations, checksums and license notices.

- **ETOPO 2022:** NOAA National Centers for Environmental Information (2022), *ETOPO 2022 15 Arc-Second Global Relief Model*, https://doi.org/10.25921/fd45-gt74. NOAA metadata identifies CC0-1.0: https://creativecommons.org/publicdomain/zero/1.0/. Regional source subsets and tile transformations are described in the pack. Heights are relative to EGM2008. Not for navigation; no accuracy or fitness warranty.
- **OurAirports:** airport and runway data by OurAirports contributors, public domain / Unlicense, https://ourairports.com/data/. Retain the pinned source version and list regional selection and unit conversions. Not for navigation.
- **GRIP4:** no GRIP geometry is bundled with this prototype. For later GRIP-derived packs, the owner selected CC BY 4.0 as a working assumption, with attribution to Meijer, Huijbregts, Schotten and Schipper, *Global patterns of current and future road infrastructure*, Environmental Research Letters 13, 064006 (2018), https://doi.org/10.1088/1748-9326/aabd42; dataset https://doi.org/10.5281/zenodo.6420961; license https://creativecommons.org/licenses/by/4.0/. State actual modifications and do not imply endorsement. Publisher CC0 statements conflict with CC BY metadata, and mixed upstream provenance remains unresolved. This working assumption does not establish legal clearance for every upstream contribution.

See `docs/EARTH_DATA_SOURCES.md` for official sources, exact sample provenance and the unresolved terms discrepancy. Attribution notices and license links are not represented as copies of the full legal texts.

## Everything else

All other code, shaders, textures (the rest procedurally generated at startup), audio (synthesised in real time, apart from the tower voices above), aircraft, map and campaign content are original to this project.

## stb_image

`src/third_party/stb_image.h` (v2.30) by Sean Barrett, used to load the loading-screen pictures. Public domain (Unlicense) or MIT, at your choice; see the end of the file.
