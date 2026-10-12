# Third-party notices

Ribterra application code is private and all rights are reserved by its author. Third-party components retain their respective copyright and license terms.

## Direct application dependencies

| Component | Pinned version/source | Notice in packaged build |
|---|---|---|
| SDL3 | 3.4.16 official VC x64 development archive | `licenses/SDL3.txt` |
| SDL_ttf | 3.2.2 official VC x64 development archive | `licenses/SDL3_ttf.txt` |
| Noto Sans Regular | notofonts/noto-fonts commit `ffebf8c1ee449e544955a7e813c54f9b73848eac` | `licenses/Noto-OFL.txt` |
| FlatBuffers | 25.12.19 through pinned vcpkg manifest | `licenses/vcpkg/flatbuffers/copyright` |
| GameNetworkingSockets | 1.6.0 through pinned vcpkg manifest/overlay | `licenses/vcpkg/gamenetworkingsockets/copyright` |
| nlohmann/json | 3.12.0#2 through pinned vcpkg manifest | `licenses/vcpkg/nlohmann-json/copyright` |
| OpenSSL | 3.6.4 through pinned vcpkg manifest | `licenses/vcpkg/openssl/copyright` |
| zstd | 1.5.7 through pinned vcpkg manifest | `licenses/vcpkg/zstd/copyright` |
| RmlUi | 6.3 through pinned vcpkg manifest | `licenses/vcpkg/rmlui/copyright` |
| ICU | 78.3#2 through pinned vcpkg manifest | `licenses/vcpkg/icu/copyright` |
| FreeType | 2.14.3 through pinned vcpkg manifest, FreeType license option | `licenses/vcpkg/freetype/copyright` |
| libpng | 1.6.58 through pinned vcpkg manifest | `licenses/vcpkg/libpng/copyright` |
| EnTT | 3.16.0 through pinned vcpkg manifest | `licenses/vcpkg/entt/copyright` |

The desktop renderer build feature uses SDL_shadercross 3.0.0-preview2, DirectX Shader Compiler 2026-05-27, and SPIRV-Cross 1.4.357.0 as host-side tools. They compile the checked-in HLSL sources into packaged DXIL, JSON reflection, and a manifest. Their executables and libraries are not runtime application dependencies and are not shipped in `dist/Ribterra`.

GameNetworkingSockets brings static protocol dependencies into the Windows build. Their packaged vcpkg notices are preserved as:

| Component | Pinned version | Notice in packaged build |
|---|---|---|
| Abseil | 20260107.1#3 | `licenses/vcpkg/abseil/copyright` |
| Protocol Buffers | 6.33.4#2 | `licenses/vcpkg/protobuf/copyright` |
| utf8-range | 6.33.4 | `licenses/vcpkg/utf8-range/copyright` |

The UI and PNG/font pipeline also links its transitive packages: Brotli 1.2.0, bzip2 1.0.8#6, zlib 1.3.2, itlib 1.12.2 and robin-hood-hashing 3.11.5. Their authoritative notices are copied to the corresponding `licenses/vcpkg/<package>/copyright` paths. ICU and RmlUi are statically linked in the Windows release triplet; the host-side ICU build tools are not packaged.

SimCore links and uses EnTT 3.16.0 for current ECS component storage. The manifest also pins Catch2 3.16.0, but current test targets do not use it. The packaged notice collection contains `licenses/vcpkg/entt/copyright` and `licenses/vcpkg/catch2/copyright`.

SDL_ttf's binary distribution includes font-rendering components whose upstream notices are preserved as `licenses/FreeType-FTL.txt`, `licenses/HarfBuzz.txt`, `licenses/PlutoSVG.txt`, and `licenses/PlutoVG.txt`. Portions of this software are copyright The FreeType Project (www.freetype.org). All rights reserved. See those files for the applicable terms.

The Noto Sans font is unmodified. SHA256: `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5`.

`scripts/bootstrap.ps1` verifies the pinned hashes of the downloaded SDL SDK archives. `vcpkg.json` pins engine packages and the renderer host-tool feature to baseline `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4`; CMake copies each installed target package's authoritative `share/<package>/copyright` file into the packaged `licenses/vcpkg` tree. Host-tool notices remain part of the source/build environment unless their binaries are distributed.

The application package excludes the private source DOCX and concept images. No Steamworks SDK, AppID, private key, server password, or other credential is included.

Official source releases:

- SDL3: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16
- SDL_ttf: https://github.com/libsdl-org/SDL_ttf/releases/tag/release-3.2.2
- vcpkg manifest sources: the registry entries selected by the pinned baseline above

## Cormorant Garamond

Copyright Christian Thalmann and the Cormorant Project Authors. Distributed under SIL Open Font License 1.1; see `licenses/Cormorant-OFL.txt`. Source: https://github.com/google/fonts/tree/main/ofl/cormorantgaramond .
