# dmk2hfe

A small command-line utility that converts **DMK** disk images (the format used by TRS-80 emulators and David Keil's tools) into **HFE** (v1) disk images for the [HxC Floppy Emulator](https://hxc2001.com/) and compatible hardware such as the Gotek running HxC or FlashFloppy firmware.

DMK stores decoded sector data without clock bits, while HFE stores the raw FM/MFM bit-cell stream. `dmk2hfe` rebuilds that raw stream, including the special clock patterns used by address marks, so the resulting image can be played back as if it were a real disk.

## Features

- Reads single- and double-sided DMK images
- Handles single density (FM), double density (MFM), and tracks that mix the two
- Removes the byte doubling DMK uses for FM data (unless header flag `0x80` says it isn't used)
- Re-creates address-mark clocking:
  - FM: index mark `FC`, ID mark `FE`, data marks `F8`–`FB`
  - MFM: `A1` and `C2` sync bytes with missing clocks
- Writes a standard HFE v1 file with the track list and interleaved side data
- Sets the HFE alternate-encoding fields when track 0 uses a different encoding from the rest of the disk
- No dependencies beyond a C++17 compiler

## Building

Requires a C++17 compiler (g++ or clang++) and `make`. Works on Linux and macOS.

```sh
make
```

This produces the `dmk2hfe` executable in the current directory.

Other targets:

| Command          | Description                                         |
|------------------|-----------------------------------------------------|
| `make install`   | Install to `/usr/local/bin` (may need `sudo`)       |
| `make uninstall` | Remove the installed binary                         |
| `make clean`     | Delete the executable and object files              |

Useful overrides:

```sh
make CXX=clang++                 # choose a different compiler
make install PREFIX=$HOME/.local # install somewhere else
```

You can also build without `make`:

```sh
g++ -O2 -std=c++17 -o dmk2hfe dmk2hfe.cpp
```

## Usage

```
dmk2hfe [-b kbps] [-r rpm] [-i ifmode] input.dmk output.hfe
```

| Option      | Description                                                        | Default                        |
|-------------|--------------------------------------------------------------------|--------------------------------|
| `-b kbps`   | Value for the HFE `bitRate` field                                  | 250 for MFM, 125 for FM        |
| `-r rpm`    | Value for the HFE `floppyRPM` field                                | 300                            |
| `-i ifmode` | HFE floppy interface mode                                          | 7 (generic Shugart DD)         |

### Examples

Basic conversion:

```sh
dmk2hfe mydisk.dmk mydisk.hfe
```

Sample output:

```
DMK: 40 tracks, 1 side(s), track length 6400, flags 0x10 [single-sided]
HFE: 40 tracks, 1 side(s), MFM, bitRate 250 kbps, 300 RPM -> mydisk.hfe
```

An 8" disk spinning at 360 RPM:

```sh
dmk2hfe -r 360 -b 500 disk8.dmk disk8.hfe
```

If an FM (single density) image plays at the wrong speed on your emulator, some tools expect `250` rather than `125` for FM disks:

```sh
dmk2hfe -b 250 sd_disk.dmk sd_disk.hfe
```

## How it works

1. The 16-byte DMK header gives the track count, track length, and flags (single-sided, single-density only, single-byte data).
2. For each track, the 128-byte IDAM table identifies each sector's ID mark and its density. Bytes are decoded per region, with FM doubling removed.
3. Address marks are located from the IDAM table (ID mark, then the data mark that follows it, plus any index mark in the gap before the first sector) and flagged for special clocking.
4. Each byte is encoded to 16 cells (clock/data pairs): FM uses a clock bit of 1 for every bit cell, MFM sets the clock only between two 0 data bits. The cells are packed least-significant bit first, as HFE requires.
5. The two sides are padded to equal length and interleaved in 256-byte blocks, and the HFE header and track list are written.

## Limitations

- **Track length:** each HFE track keeps the DMK's nominal track length, including any slack at the end of the track, so rotation timing follows that length rather than an exact one-revolution bit count.
- **Mixed-density tracks:** density changes are applied at sector (IDAM) boundaries, so the gap bytes just before an IDAM take the density of the previous sector.
- **MFM data-mark detection:** the data mark is found by looking for `A1 A1 A1` followed by `F8`–`FB` within about 60 bytes of the ID field. Sector data that happens to contain this exact pattern right after the ID field could be mislabeled, though this is very rare.
- **Copy-protected disks:** weak bits, non-standard sync patterns, and other low-level tricks aren't represented in DMK and therefore can't be reproduced in the HFE output.
- **Output format:** only HFE v1 is written. HFE v3 is not supported.
- **Bit rate field:** the HFE `bitRate` convention for FM disks varies between tools. If playback speed is wrong for FM images, try `-b 250`.

## DMK format reference

```
Header (16 bytes)
  0      write protect (0x00 / 0xFF)
  1      number of tracks (per side)
  2-3    track length in bytes, little-endian (includes the 128-byte IDAM table)
  4      flags: 0x10 single-sided, 0x40 single density only, 0x80 single-byte data
  5-15   reserved

Per track
  128 bytes   IDAM table: up to 64 little-endian pointers
              bits 0-13 = offset from start of the track record
              bit 15    = 1 for double density
  remainder   decoded track data
```

For double-sided images, track records alternate side 0, side 1 for each cylinder.

## License

This software was created using Claude Sonnet 5.5.  It is made available under the MIT License.

Copyright 2026 Amardeep Chana

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
