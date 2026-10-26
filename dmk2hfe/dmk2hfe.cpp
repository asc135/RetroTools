// Copyright 2026 Amardeep Chana
// This software was created with the help of Claude Sonnet 5.5.
// It is free software under the MIT license.
//
// dmk2hfe.cpp - Convert a DMK disk image (TRS-80 / David Keil format) to an
// HxC Floppy Emulator HFE (v1) image.
//
// Build:  g++ -O2 -std=c++17 -o dmk2hfe dmk2hfe.cpp
// Usage:  dmk2hfe [-b kbps] [-r rpm] [-i mode] input.dmk output.hfe
//
// Notes
//  * DMK stores decoded bytes (no clock bits). This program re-creates the raw
//    FM / MFM cell stream, including the special address-mark clock patterns
//    (FM: FC/FE/F8-FB; MFM: A1 and C2 sync bytes with missing clocks).
//  * Density is taken from the DMK header flags and the per-IDAM density bit.
//  * Single-density (FM) data is "doubled" in DMK unless header flag 0x80 is
//    set; the doubling is removed before encoding.
//  * The output HFE track length equals the DMK track length (rounded up to
//    the 256-byte-per-side HFE block size) so rotational timing will match
//    the nominal DMK track size.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

enum Mode : uint8_t { FM = 0, MFM = 1 };

struct Cell {
    uint8_t data;
    Mode    mode;
    bool    special;  // address mark with irregular clocking
    uint8_t clk;      // FM: explicit clock pattern; MFM: mask of clocks to suppress
};

struct Idam {
    int  off;
    Mode mode;
};

constexpr int DMK_HDR_SIZE   = 16;
constexpr int DMK_IDAM_BYTES = 128;
constexpr int HFE_BLOCK      = 512;

enum HfeEncoding : uint8_t { ISOIBM_MFM = 0, ISOIBM_FM = 2 };

bool readFile(const char* path, std::vector<uint8_t>& buf) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    buf.resize(static_cast<size_t>(sz));
    size_t got = sz ? std::fread(buf.data(), 1, buf.size(), f) : 0;
    std::fclose(f);
    return got == buf.size();
}

Mode idamMode(uint8_t flags, uint16_t ptr) {
    if (flags & 0x40) return FM;          // single density only
    if (flags & 0x80) return MFM;         // density bits ignored -> assume MFM
    return (ptr & 0x8000) ? MFM : FM;
}

// Decode one DMK track record into a list of data bytes with mode/mark info.
// Returns the track's default (dominant) mode.
Mode decodeTrack(const uint8_t* rec, int trackLen, uint8_t flags,
                 std::vector<Cell>& cells) {
    cells.clear();

    // --- IDAM table ---------------------------------------------------------
    std::vector<Idam> idams;
    for (int i = 0; i < 64; i++) {
        uint16_t v = rec[2 * i] | (rec[2 * i + 1] << 8);
        if (v == 0 || v == 0xFFFF) continue;
        int off = v & 0x3FFF;
        if (off < DMK_IDAM_BYTES || off >= trackLen) continue;
        idams.push_back({off, idamMode(flags, v)});
    }
    std::sort(idams.begin(), idams.end(),
              [](const Idam& a, const Idam& b) { return a.off < b.off; });
    idams.erase(std::unique(idams.begin(), idams.end(),
                            [](const Idam& a, const Idam& b) { return a.off == b.off; }),
                idams.end());

    Mode defMode = (flags & 0x40) ? FM : MFM;
    if (!idams.empty()) defMode = idams[0].mode;

    const bool doubled = !(flags & 0x80);

    // --- Expand bytes, undoing FM doubling ---------------------------------
    std::vector<int> map(trackLen, -1);   // DMK offset -> cell index
    auto emitSeg = [&](int s, int e, Mode m) {
        int step = (m == FM && doubled) ? 2 : 1;
        for (int p = s; p < e; p += step) {
            int idx = static_cast<int>(cells.size());
            cells.push_back({rec[p], m, false, 0});
            for (int q = p; q < p + step && q < e; q++) map[q] = idx;
        }
    };

    if (idams.empty()) {
        emitSeg(DMK_IDAM_BYTES, trackLen, defMode);
    } else {
        emitSeg(DMK_IDAM_BYTES, idams[0].off, idams[0].mode);
        for (size_t k = 0; k < idams.size(); k++) {
            int e = (k + 1 < idams.size()) ? idams[k + 1].off : trackLen;
            emitSeg(idams[k].off, e, idams[k].mode);
        }
    }

    const int n = static_cast<int>(cells.size());
    auto isDataMark = [](uint8_t b) { return b >= 0xF8 && b <= 0xFB; };

    // --- Flag address marks -------------------------------------------------
    std::vector<int> idIdx;
    for (const auto& id : idams)
        if (map[id.off] >= 0) idIdx.push_back(map[id.off]);

    for (size_t k = 0; k < idIdx.size(); k++) {
        int i     = idIdx[k];
        int limit = (k + 1 < idIdx.size()) ? idIdx[k + 1] : n;
        int jmax  = std::min(limit, i + 7 + 60);

        if (cells[i].mode == FM) {
            cells[i].special = true;           // ID address mark FE, clock C7
            cells[i].clk     = 0xC7;
            for (int j = i + 7; j < jmax; j++) {
                if (cells[j].mode == FM && isDataMark(cells[j].data)) {
                    cells[j].special = true;   // data address mark, clock C7
                    cells[j].clk     = 0xC7;
                    break;
                }
            }
        } else {
            for (int t = 1; t <= 3; t++) {     // A1 A1 A1 FE
                if (i - t >= 0 && cells[i - t].data == 0xA1) {
                    cells[i - t].special = true;
                    cells[i - t].clk     = 0x04;   // drop clock of bit 2
                }
            }
            for (int j = i + 7; j + 3 < jmax + 3 && j + 3 < n; j++) {
                if (cells[j].data == 0xA1 && cells[j + 1].data == 0xA1 &&
                    cells[j + 2].data == 0xA1 && isDataMark(cells[j + 3].data)) {
                    for (int t = 0; t < 3; t++) {
                        cells[j + t].special = true;
                        cells[j + t].clk     = 0x04;
                    }
                    break;
                }
            }
        }
    }

    // Index mark (only if present in the pre-first-IDAM gap)
    int firstId = idIdx.empty() ? n : idIdx[0];
    for (int j = 0; j < firstId; j++) {
        if (cells[j].mode == FM && cells[j].data == 0xFC) {
            cells[j].special = true;
            cells[j].clk     = 0xD7;
            break;
        }
        if (cells[j].mode == MFM && j + 3 < n && cells[j].data == 0xC2 &&
            cells[j + 1].data == 0xC2 && cells[j + 2].data == 0xC2 &&
            cells[j + 3].data == 0xFC) {
            for (int t = 0; t < 3; t++) {
                cells[j + t].special = true;
                cells[j + t].clk     = 0x08;   // drop clock of bit 3
            }
            break;
        }
    }

    return defMode;
}

// Produce the raw cell stream (clock,data pairs; first cell = bit 0 of byte 0,
// as HFE requires). Each data byte yields 16 cells = 2 bytes.
std::vector<uint8_t> encodeSide(const std::vector<Cell>& cells) {
    std::vector<uint8_t> out(cells.size() * 2, 0);
    size_t nbits = 0;
    int prev = 0;
    auto put = [&](int b) {
        if (b) out[nbits >> 3] |= static_cast<uint8_t>(1u << (nbits & 7));
        nbits++;
    };
    for (const Cell& c : cells) {
        for (int i = 7; i >= 0; i--) {
            int d = (c.data >> i) & 1;
            int clk;
            if (c.mode == FM) {
                clk = c.special ? ((c.clk >> i) & 1) : 1;
            } else {
                clk = (!prev && !d) ? 1 : 0;
                if (c.special && ((c.clk >> i) & 1)) clk = 0;
            }
            put(clk);
            put(d);
            prev = d;
        }
    }
    return out;
}

void put16(uint8_t* p, unsigned v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [-b kbps] [-r rpm] [-i ifmode] input.dmk output.hfe\n"
        "  -b kbps   HFE bitRate field (default 250 for MFM, 125 for FM)\n"
        "  -r rpm    floppyRPM field (default 300)\n"
        "  -i mode   HFE floppy interface mode (default 7 = generic Shugart DD)\n",
        prog);
}

}  // namespace

int main(int argc, char** argv) {
    int bitrate = -1, rpm = 300, ifmode = 7;
    std::vector<const char*> files;

    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "-b") && i + 1 < argc)      bitrate = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "-r") && i + 1 < argc) rpm = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "-i") && i + 1 < argc) ifmode = std::atoi(argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1]) { usage(argv[0]); return 1; }
        else files.push_back(argv[i]);
    }
    if (files.size() != 2) { usage(argv[0]); return 1; }

    std::vector<uint8_t> dmk;
    if (!readFile(files[0], dmk)) {
        std::fprintf(stderr, "Error: cannot read '%s'\n", files[0]);
        return 1;
    }
    if (dmk.size() < DMK_HDR_SIZE) {
        std::fprintf(stderr, "Error: file too small to be a DMK image\n");
        return 1;
    }

    const int     tracks   = dmk[1];
    const int     trackLen = dmk[2] | (dmk[3] << 8);
    const uint8_t flags    = dmk[4];
    const int     sides    = (flags & 0x10) ? 1 : 2;

    if (tracks == 0 || trackLen <= DMK_IDAM_BYTES || trackLen > 0x4000) {
        std::fprintf(stderr, "Error: invalid DMK header (tracks=%d, track length=%d)\n",
                     tracks, trackLen);
        return 1;
    }
    size_t need = DMK_HDR_SIZE + static_cast<size_t>(tracks) * sides * trackLen;
    if (dmk.size() < need) {
        std::fprintf(stderr, "Error: file is %zu bytes, header implies %zu\n",
                     dmk.size(), need);
        return 1;
    }

    std::printf("DMK: %d tracks, %d side(s), track length %d, flags 0x%02X%s%s%s\n",
                tracks, sides, trackLen, flags,
                (flags & 0x40) ? " [SD only]" : "",
                (flags & 0x80) ? " [single-byte]" : "",
                (flags & 0x10) ? " [single-sided]" : "");

    // --- Convert every track ------------------------------------------------
    std::vector<std::vector<uint8_t>> hfeTracks(tracks);
    std::vector<Mode> trackMode(static_cast<size_t>(tracks) * 2, MFM);
    int modeCount[2] = {0, 0};

    for (int c = 0; c < tracks; c++) {
        std::vector<Cell> side[2];
        for (int s = 0; s < sides; s++) {
            const uint8_t* rec = dmk.data() + DMK_HDR_SIZE +
                                 (static_cast<size_t>(c) * sides + s) * trackLen;
            Mode m = decodeTrack(rec, trackLen, flags, side[s]);
            trackMode[c * 2 + s] = m;
            modeCount[m]++;
        }

        size_t nDec = std::max(side[0].size(), side[1].size());
        nDec = (nDec + 127) / 128 * 128;   // -> multiple of 256 encoded bytes

        std::vector<uint8_t> enc[2];
        for (int s = 0; s < 2; s++) {
            if (s < sides) {
                Mode m = side[s].empty() ? trackMode[c * 2 + s] : side[s].back().mode;
                uint8_t filler = (m == FM) ? 0xFF : 0x4E;
                while (side[s].size() < nDec) side[s].push_back({filler, m, false, 0});
                enc[s] = encodeSide(side[s]);
            } else {
                enc[s].assign(nDec * 2, 0x00);   // unused second side
            }
        }

        size_t perSide = nDec * 2;
        if (perSide * 2 > 0xFFFF) {
            std::fprintf(stderr, "Error: track %d too long for HFE (%zu bytes)\n",
                         c, perSide * 2);
            return 1;
        }
        std::vector<uint8_t>& t = hfeTracks[c];
        t.reserve(perSide * 2);
        for (size_t blk = 0; blk < perSide; blk += 256) {
            t.insert(t.end(), enc[0].begin() + blk, enc[0].begin() + blk + 256);
            t.insert(t.end(), enc[1].begin() + blk, enc[1].begin() + blk + 256);
        }
    }

    // --- HFE header ---------------------------------------------------------
    const Mode diskMode = (modeCount[FM] > modeCount[MFM]) ? FM : MFM;
    const uint8_t diskEnc = (diskMode == FM) ? ISOIBM_FM : ISOIBM_MFM;
    if (bitrate <= 0) bitrate = (diskMode == FM) ? 125 : 250;

    uint8_t hdr[HFE_BLOCK];
    std::memset(hdr, 0xFF, sizeof(hdr));
    std::memcpy(hdr, "HXCPICFE", 8);
    hdr[8]  = 0;                 // format revision
    hdr[9]  = static_cast<uint8_t>(tracks);
    hdr[10] = static_cast<uint8_t>(sides);
    hdr[11] = diskEnc;
    put16(hdr + 12, bitrate);
    put16(hdr + 14, rpm);
    hdr[16] = static_cast<uint8_t>(ifmode);
    hdr[17] = 1;                 // reserved
    put16(hdr + 18, 1);          // track list at block 1
    hdr[20] = 0xFF;              // write allowed
    hdr[21] = 0xFF;              // single step
    hdr[22] = hdr[23] = 0xFF;    // track 0 side 0 alt encoding (disabled)
    hdr[24] = hdr[25] = 0xFF;    // track 0 side 1 alt encoding (disabled)

    auto encOf = [](Mode m) { return static_cast<uint8_t>(m == FM ? ISOIBM_FM : ISOIBM_MFM); };
    if (trackMode[0] != diskMode) { hdr[22] = 0x00; hdr[23] = encOf(trackMode[0]); }
    if (sides == 2 && trackMode[1] != diskMode) { hdr[24] = 0x00; hdr[25] = encOf(trackMode[1]); }

    // --- Track list ---------------------------------------------------------
    size_t lutBlocks = (static_cast<size_t>(tracks) * 4 + HFE_BLOCK - 1) / HFE_BLOCK;
    std::vector<uint8_t> lut(lutBlocks * HFE_BLOCK, 0xFF);
    size_t block = 1 + lutBlocks;
    for (int c = 0; c < tracks; c++) {
        size_t len = hfeTracks[c].size();
        if (block > 0xFFFF) {
            std::fprintf(stderr, "Error: output too large for HFE v1\n");
            return 1;
        }
        put16(&lut[c * 4], static_cast<unsigned>(block));
        put16(&lut[c * 4 + 2], static_cast<unsigned>(len));
        block += (len + HFE_BLOCK - 1) / HFE_BLOCK;
    }

    // --- Write output -------------------------------------------------------
    FILE* out = std::fopen(files[1], "wb");
    if (!out) {
        std::fprintf(stderr, "Error: cannot create '%s'\n", files[1]);
        return 1;
    }
    bool ok = std::fwrite(hdr, 1, sizeof(hdr), out) == sizeof(hdr);
    ok = ok && std::fwrite(lut.data(), 1, lut.size(), out) == lut.size();
    for (int c = 0; ok && c < tracks; c++) {
        const auto& t = hfeTracks[c];
        ok = std::fwrite(t.data(), 1, t.size(), out) == t.size();
        size_t pad = (HFE_BLOCK - t.size() % HFE_BLOCK) % HFE_BLOCK;
        for (size_t i = 0; ok && i < pad; i++) ok = std::fputc(0x00, out) != EOF;
    }
    if (std::fclose(out) != 0) ok = false;
    if (!ok) {
        std::fprintf(stderr, "Error: write failed\n");
        return 1;
    }

    std::printf("HFE: %d tracks, %d side(s), %s, bitRate %d kbps, %d RPM -> %s\n",
                tracks, sides, diskMode == FM ? "FM" : "MFM", bitrate, rpm, files[1]);
    return 0;
}
