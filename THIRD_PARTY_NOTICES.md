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

## Everything else

All other code, shaders, textures (procedurally generated at startup), audio (synthesised in real time, apart from the tower voices above), aircraft, map and campaign content are original to this project.

## stb_image

`src/third_party/stb_image.h` (v2.30) by Sean Barrett, used to load the loading-screen pictures. Public domain (Unlicense) or MIT, at your choice; see the end of the file.
