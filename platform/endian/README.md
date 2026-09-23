# platform/endian — big-endian resource conversion

Game data on the disc is big-endian; the PC build is 32-bit little-endian.
Loaders that cast a loaded buffer to structs need the buffer converted once, in place, before they run.
This directory holds those converters, the format inventory (`INVENTORY.md`), and host tests.

| File | Contents |
|---|---|
| `port_endian.h` | Public C API. |
| `endian_util.h` | Bounded, unaligned-safe swap helpers (`pe::Buf`) and the offset-ordered section layout used by J3D. |
| `endian_j3d.cpp` | J3D models/material tables (INF1, VTX1, EVP1, DRW1, JNT1, SHP1, MAT3, MAT2, TEX1), all J3D animation blocks, `ResNTAB`, `ResTIMG`/BTI. |
| `endian_jpa.cpp` | JParticle `JEFFjpa1` resources. |
| `endian_jaudio.cpp` | JAudio AAF init data and everything it embeds: sound tables, IBNK, WSYS, sequence-archive header, sound/FX scene tables. |
| `endian_game.cpp` | SMS formats: `.col`, `.ral`, `.ymp`, `.pad`, `.bcr`, `.sb` (SPCB). |
| `endian_dispatch.cpp` | `port_endian_resource()`: recognition by magic, else by file name; `port_endian_fetched()` hook. |
| `tests/` | Host tests against the real disc (`.cc`, so the port's `platform/*.cpp` glob skips them). |

## Rules the converters follow

- Only multi-byte fields are swapped; byte arrays and strings are untouched.
- Texture texels, TLUT entries and GX display lists stay big-endian: `platform/gx` decodes them in disc order.
- J3D vertex arrays **are** swapped to host order, because the CPU reads them too.
  Do not call `GXPC_AddBigEndianRange` on a converted model; unregistered arrays are read in host order.
- Data read through `JSUInputStream` typed reads stays big-endian, since patch 0013 converts at the read.
  That covers `.blo`, `.bmg`, `.prm`, scene `.bin`, JPA BEM1/FLD1 and the AAF stream list; the raw reads in those loaders are fixed by the `endian-*` patches.
- Every converter is bounded by the buffer size and is safe to call twice:
  - formats with a magic flip it (`J3D2` reads back as `2D3J`), and a flipped magic is reported as `PE_FMT_ALREADY_NATIVE`;
  - `.bti` and `.bas` set a marker in a pad byte that is always zero on disc and never read;
  - the other magic-less formats check that a header offset is still a valid big-endian offset;
  - the AAF checks its first command word.

## Integration (for the bring-up lead)

1. **Sources.**
   The `platform/*.cpp` glob in `CMakeLists.txt` already compiles `platform/endian/*.cpp` into `sms`; nothing to add.
   `platform/misc/endian.cpp` calls `port_endian_resource()` (weak) from `port_res_to_native_named()`, so every resource `JKRMemArchive` fetches goes through it.
2. **Pass the file name.**
   BTI, AAF, BAS and the SMS formats have no magic and are recognised by extension, so the fetch hook must pass `name` (patch 0011 does).
3. **Convert a `readResource` copy only if it is fresh.**
   In `JKRMemArchive::fetchResource(void* buffer, ...)`, a copy made from an already fetched `mData` is already converted.
   The markers make a second call harmless, but it is cleaner to call the hook only in the branch that decompresses from the archive.
4. **Patches.**
   `decomp-patches/endian-*.patch` sort after the numbered patches and are applied by `cmake/patches.cmake` like the others; re-run `cmake -S . -B build` after adding or changing one.
   Each change is under `#ifdef TARGET_PC` with the original line in `#else`, so the MWCC build is untouched even if the patches were applied to `decomp/`.
   They use `port_be16`/`port_be32` from `src/port_compat.h`, which is force-included.

| Patch | What it fixes |
|---|---|
| `endian-01-JPAEmitter-bem1-vector-reads` | BEM1's raw `Vec`/`S16Vec` stream reads become per-component typed reads. |
| `endian-02-J2D-blo-raw-reads` | `.blo`: raw peek/read of `SCRN`, block magic and size, old-format tag, pane kind. |
| `endian-03-MessageLoader-bmg-raw-reads` | `.bmg`: `TMessageLoader`'s raw header and INF1 size reads, and the raw-copied `JMSMesgEntry` fields. |
| `endian-04-JUTColor-rgba-by-value` | `TColor(u32)`, `set(u32)` and `toUInt32()` use `0xRRGGBBAA` by value, so colours from typed reads and literals are right. |
| `endian-05-ParamInst-prm-values` | `.prm`: `TParamT<T>::load`'s raw value read (a `TVec3<f32>` is three f32). |
| `endian-06-spcinterp-be-immediates` | SPC byte code: `fetchF32`/`fetchS32`/`fetchU32` assemble big-endian. |
| `endian-07-JAIGFrameStream-header` | Stream (`.afc`) header after it is copied into `StreamHeader`. |

5. **Debugging.**
   `SMS_ENDIAN_LOG=1` makes `port_endian_resource()` log each resource it is given and the format it was converted as.
   `port_endian_fetched(data, size, name)` is a ready-made hook for other loaders (DVD/ARAM archives): it converts, then falls back to `port_res_to_native`.
   Formats nobody converts are still logged once by `port_res_to_native` (`[endian] resource format not converted`).

## Tests

```
make -C platform/endian/tests run            # DISC=/path/to/GMSE01/files to override
```

The test decompresses Yaz0 and walks RARC archives itself (reading big-endian directly).
It converts every J3D, JPA, BTI, BAS, AAF and SMS-format file in the boot, title, option, common, mario, particle, subtitle and four stage archives.
It then checks the converted structures in host order: block walks, counts, offsets in range, keyframe tables within their value arrays, name-table hashes, float sanity, sound-table totals, instrument and wave parameters.
It also checks that a second conversion is refused.
