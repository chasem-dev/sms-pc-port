# platform/trace — native-vs-retail lockstep testing

The goal is that the native port behaves like retail.
Three pieces check that against the Dolphin oracle (`/home/netflix/dolphin-oracle`):

1. **Movie player** (`trace.cpp`): feeds the oracle's `.dtm` pad input into `PADRead`, so the native game gets the same inputs as the retail run.
2. **Trace writer** (`trace.cpp`): writes the same named memory ranges every VI field, in the oracle's trace format.
   Values are converted to big-endian with each range's DWARF layout, so they compare byte for byte with retail.
3. **Tools**:
   - `tools/trace_resolve.py` (gdb Python) turns the oracle's symbolic range files into native addresses and layouts.
   - `tools/trace_compare.py` reports the first diverging field, range and member between a retail trace and a native trace.

Everything is off unless `SMS_MOVIE` or `SMS_TRACE_OUT` is set.

## Hooks (for the bring-up lead)

Two calls, declared in `platform/trace/trace.h`:

```c
// platform/vi/vi.cpp, retrace_irq(), right after g_retrace_count++ (before the game's callbacks):
port_trace_on_retrace(g_retrace_count);

// platform/pad/pad.cpp, top of PADRead() after init():
if (port_trace_pad_read(status))
    return PAD_CHAN0_BIT;
```

`port_trace_movie_active()` tells whether a movie is playing, if other input sources should be muted.
The trace sources are compiled into `sms` by the `platform/**/*.cpp` glob.
They are 32-bit only: native pointer members must be 4 bytes for the layouts to line up with retail.

## Running

```sh
# 1. resolve the oracle's range file against this binary (repeat after every relink)
TRACE_RANGES=/home/netflix/dolphin-oracle/ranges/play.txt TRACE_OUT=build/play.ranges \
    gdb -batch -x tools/trace_resolve.py build/sms

# 2. native run: same movie as the retail run, poll timing taken from the retail trace
SMS_MOVIE=/home/netflix/dolphin-oracle/movies/play5.dtm \
SMS_MOVIE_POLLMAP=/home/netflix/dolphin-oracle/runs/play-r9/trace.txt \
SMS_TRACE_RANGES=build/play.ranges SMS_TRACE_OUT=build/play-native.txt SMS_TRACE_FRAMES=11500 \
    build/sms --headless

# 3. compare
tools/trace_compare.py /home/netflix/dolphin-oracle/runs/play-r9/trace.txt build/play-native.txt \
    --names build/play.ranges.json
```

| Variable | Meaning |
|---|---|
| `SMS_MOVIE` | `.dtm` to play on port 1 (from the oracle's `scripts/make_dtm.py`) |
| `SMS_MOVIE_POLLMAP` | a retail trace: its `polls=` per field gives the exact poll index at every field |
| `SMS_MOVIE_POLL_BIAS` | added to the poll index (default 0) |
| `SMS_TRACE_OUT` | trace file |
| `SMS_TRACE_RANGES` | native ranges from `tools/trace_resolve.py` |
| `SMS_TRACE_SYNC` | how native retraces map to retail fields (below); default `app+8:1=2@231` |
| `SMS_TRACE_START` / `SMS_TRACE_EVERY` | first field written / write every n-th field |
| `SMS_TRACE_FRAMES` | exit once this retail-numbered field is reached |

### Timing semantics

The oracle's movie has one record per controller *poll*, not per field.
SMS polls twice per field once running, and the retail trace records the running poll count (`polls=`) at each field boundary.
Retail spends about 230 fields in the IPL/apploader before `main`, which the port does not.

So native retraces are numbered in retail fields.
- With the default `SMS_TRACE_SYNC=app+8:1=2@231`, the first retrace at which `TApplication::mAppState` (byte 8 of the `app` range) reads 2 becomes field 231, as in retail.
  Numbering then advances one per retrace.
- The general form is `<label>+<hexoff>:<width>=<value>@<field>`, evaluated on the big-endian traced bytes.
  `SMS_TRACE_SYNC=+N` instead gives a fixed offset: field = retrace + N.
- Before the sync point, `PADRead` sees a connected pad with neutral input, and nothing is traced.
- During field F, `PADRead` returns movie record `polls(F+1) − 1`.
  That is the last poll before the next boundary, where `polls(F)` is the poll count retail had reached at F's boundary.
  SMS reads the pad late in the field, after both of the field's SI polls.
  This reproduces retail's `JUTGamePad::mPadStatus` exactly: a 6-poll START press at polls 450–455 shows at boundaries 340–341 in both traces.
  `polls(F)` comes from the retail trace (`SMS_MOVIE_POLLMAP`); past its end, or without one, it continues at two polls per field (`polls ≈ 2F − 227`).
  Use `SMS_MOVIE_POLL_BIAS` if a native run turns out to read one poll early or late.
- The trace is written in `retrace_irq`, at the same point as the game's retrace callbacks.
  Each `F` line carries `polls=` (the movie record in use) and `retrace=` (the native count).
  `ticks`, `pc`, `lr` and the MEM1 hash are 0 or absent.

### Ranges and byte order

`tools/trace_resolve.py` runs inside gdb on the binary, without starting it.
For each oracle range (`symbol`, `*symbol+off`, `*(symbol+n)`, `0xADDR`, `size`):
- **Symbols:** looked up in the port's DWARF.
  CodeWarrior-mangled static members such as `mPadStatus__10JUTGamePad` are looked up as `JUTGamePad::mPadStatus`.
- **Types:** the pointee type of `*gpMarioOriginal` is `TMario`.
  For `*(gpApplication+0x20)`, the member at that offset is a `TMarioGamePad*`, so the pointee is `TMarioGamePad`.
- **Layout:** each type is flattened into integer/float/pointer/byte members, written as a compact layout (`4p,4i,2ix3,1bx2,4f,...`).
- **Relocation:** host globals are written relative to `port_trace_anchor` so the PIE load address is irrelevant.
- **Heap:** absolute MEM1 addresses are used as they are, since game heaps live in the emulated MEM1 at `0x80000000`.
  Their layout is unknown and treated as 4-byte integers, unless a `# type <label> <Type>` line in the range file names one.
- **Other output:** `TRACE_OUT.json` lists every member name per label, for the compare report.

At run time each range is copied and every multi-byte member byte-swapped, so the `D` lines hold big-endian bytes like retail's.
- **Unreadable ranges:** a dereference through NULL or outside MEM1 and the host image gives `D <label> - -`.
- **Class layouts:** the port's class layouts equal the GameCube's where the decomp declares the same members.
  x86-32 aligns 8-byte members to 4, so a structure with `s64`/`f64` members after odd padding could shift.
  The per-member report shows where.

## Comparing

`tools/trace_compare.py RETAIL NATIVE [--names X.json] [--align field|polls] [--only RE] [--ignore RE] [--from F] [--to F] [--ftol 1e-3] [--frel 1e-5]`

- **Pairing:** retail and native fields are paired by number (native is already in retail numbering), or by equal `polls=` with `--align polls`.
  `--resync LABEL+OFF:W=VALUE` shifts native numbering so that the first field where the condition holds lines up in both traces, for traces recorded without re-sync points.
- **Ignored differences:**
  - pointer members (layout `p`);
  - any 4-byte word whose retail value points into MEM1 while the native value is non-zero (disable with `--strict-pointers`);
  - float members within the tolerance.
- **Report:** the first divergence overall with member name, both byte windows, both poll counts, and a timing hint (the nearest retail field with the native value).
  Then the first divergence of every label.
- **Exit status:** 0 when nothing differs.

The oracle's own `compare_traces.py` compares two Dolphin runs.
This tool is its native counterpart: it uses the native trace's layouts in place of the two linker maps.

### GameCube layout

The oracle's offsets are GameCube (MWCC) offsets, and the port's g++ layout differs from them.
- **Tail padding:** the Itanium ABI puts a derived class's first member into its base's tail padding.
  For example `JDrama::TViewObj::unkC` sits at 10, not 0xC, so `TPlacement::mPosition` sits at 0xC, not 0x10.
- **Alignment:** 8-byte members align to 4, not 8.

`trace_resolve.py` therefore recomputes every type's MWCC layout from its DWARF members: no tail-padding reuse, 8-byte scalars aligned to 8, members in declaration order.
It emits a copy map (`map=gcoff:natoff:<w><k>[x<n>]`), and the writer rebuilds each range in the GameCube layout before byte-swapping.
Bytes that no member covers stay zero.

The rebuilt layout is only as complete as the decomp's declared members.
Where retail has a member the decomp header does not declare, later offsets shift.
Known case: the `JUTGamePad` header comments put `mPortNum` at 0x7C, but its declared `CRumble` is 0x10 bytes, which puts `mPortNum` at 0x78.
So `gamepad0` is shifted by 4 from `mPortNum` up to `mPadReplay`.

## Status (2026-09-23)

What was checked: a native run (worktree build with the two hooks) traced against `dolphin-oracle/runs/play-r9` (`movies/play5.dtm`, 11500 fields).

- **Tracing and sync:** the whole path traces, 11,260 native fields.
  With `SMS_TRACE_SYNC=app+8:1=2@231,app+e:4=0x0f000000@456,app+e:4=0@1244,mdstate+0:1=0@5352`, sync lands at native retrace 3–4, and the title re-sync moves native by +9..+11 fields.
  Native reaches the logo and title faster than retail's DVD loads.
- **Pad input:** the movie reaches the game correctly.
  `JUTGamePad::mPadStatus[0].button` and the gamepad's `mButton` follow retail's START/A presses, e.g. START at retail fields 340–341 and native 339–340 (or 340–341 in another run).
- **App states:** `app` states match the retail sequence: 2 → 3 → 4 → 5 on area 15, i.e. boot, Nintendo logo, title.
- **Native is not deterministic run to run.**
  - The VI retrace is a real-time 59.94 Hz host timer, so sync lands one retrace earlier or later from run to run, and input lands with it.
  - In one run native left file select into the airstrip at field 1283 (retail 1244, then 4100 fields of opening movie).
  - In two runs it stayed in file select.
  - For true lockstep, VI needs a deterministic clock: one retrace per game frame, or retraces driven by the game's `VIWaitForRetrace`/`GXCopyDisp` instead of wall time.
  - This is platform/vi, the lead's file.
- **Heap ranges:** `cardload` and `blockA/B/C` are absolute retail heap addresses, and the native heap layout differs, so they never match.
  They need symbolic starts (a pointer path from a global) before they are useful natively.
- **Bug found:** tracing the airstrip exposed a stack smash in `J3DSkinDeform::initMtxIndexArray`: display lists were parsed as native u16.
  It is fixed by `decomp-patches/endian-14-J3DSkinDeform-dl-be.patch`.
- **First real divergence:** `TMarioGamePad::mFlags` reads 0x40 in native from field 231 and 0 in retail.
  This comparison is unreliable until the `gamepad0` offsets are corrected (see above).
