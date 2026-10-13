# Third-party notices

PadPorter does not vendor third-party code in this repository. All dependencies are downloaded at build time (CMake FetchContent).

| Component | Version | Used for | License |
|---|---|---|---|
| [JUCE](https://juce.com) | 8.0.16-security | App, plugin, audio decoding | AGPLv3 or commercial JUCE license |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled with JUCE) | - | VST3 wrapper | MIT (since 3.8) |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | JSON parsing | MIT |
| [miniz](https://github.com/richgel999/miniz) | 3.0.2 | gzip decompression | MIT |
| [doctest](https://github.com/doctest/doctest) | 2.4.12 | Unit tests only (not shipped) | MIT |

## Format research

Knowledge about the `.xpj` file structure was derived from
[MPC2Live](https://github.com/Duffman007/MPC2Live) by duffman (MIT License, Copyright (c) 2025 duffman).
No source code was copied; PadPorter is an independent C++ implementation.

## Trademarks

MPC and Akai are trademarks of inMusic Brands, Inc. Ableton and Ableton Live are trademarks of Ableton AG.
VST is a trademark of Steinberg Media Technologies GmbH. PadPorter is not affiliated with or endorsed by these companies.
