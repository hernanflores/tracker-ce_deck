// eve-replay: replays a display bus trace (recorded with --eve-trace) into a display backend.
//
//   eve-replay TRACE [--backend passive|bteve] [--eve-lib PATH]
//                    [--stats] [--dump] [--png-dir DIR] [--max-frames N]
//
// --stats      bytes, bursts, command-ring kicks, frames, histogram of co-processor commands
// --dump       text disassembly of the co-processor command stream (and display list words in it)
// --png-dir    write a PNG for every frame the backend produces (bteve)
#include "FT81x.h"
#include "eve/BtEmuDevice.h"
#include "eve/EveSpiDecoder.h"
#include "eve/PassiveEveDevice.h"
#include "eve/TraceTee.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>

using namespace eve;

//----------------------------------------------------------------------------------------------
// co-processor command table: name and number of 32-bit argument words after the command
//----------------------------------------------------------------------------------------------
namespace
{
struct CmdInfo
{
    uint32_t code;
    const char* name;
    int args;      // fixed argument words
    bool trailingString; // a NUL-terminated string follows, padded to a word
};

const CmdInfo kCmds[] = {
    { CMD_DLSTART, "DLSTART", 0, false },
    { CMD_SWAP, "SWAP", 0, false },
    { CMD_BGCOLOR, "BGCOLOR", 1, false },
    { CMD_FGCOLOR, "FGCOLOR", 1, false },
    { CMD_GRADIENT, "GRADIENT", 4, false },
    { CMD_TEXT, "TEXT", 2, true },
    { CMD_BUTTON, "BUTTON", 3, true },
    { CMD_KEYS, "KEYS", 3, true },
    { CMD_NUMBER, "NUMBER", 3, false },
    { CMD_PROGRESS, "PROGRESS", 4, false },
    { CMD_SLIDER, "SLIDER", 4, false },
    { CMD_SCROLLBAR, "SCROLLBAR", 4, false },
    { CMD_SETBASE, "SETBASE", 1, false },
    { CMD_MEMCPY, "MEMCPY", 3, false },
    { CMD_MEMZERO, "MEMZERO", 2, false },
    { CMD_MEMSET, "MEMSET", 3, false },
    { CMD_MEMWRITE, "MEMWRITE", 2, false },
    { CMD_APPEND, "APPEND", 2, false },
    { CMD_SETBITMAP, "SETBITMAP", 3, false },
    { CMD_SETFONT, "SETFONT", 2, false },
    { CMD_SETFONT2, "SETFONT2", 3, false },
    { CMD_LOADIDENTITY, "LOADIDENTITY", 0, false },
    { CMD_TRANSLATE, "TRANSLATE", 2, false },
    { CMD_SCALE, "SCALE", 2, false },
    { CMD_ROTATE, "ROTATE", 1, false },
    { CMD_SETMATRIX, "SETMATRIX", 0, false },
    { CMD_LOADIMAGE, "LOADIMAGE", 2, false }, // image bytes follow inline
    { CMD_GETPROPS, "GETPROPS", 3, false },
    { CMD_SNAPSHOT2, "SNAPSHOT2", 4, false },
    { CMD_GETPTR, "GETPTR", 1, false },
    { CMD_INFLATE, "INFLATE", 1, false },
};

const CmdInfo* findCmd(uint32_t w)
{
    for (const auto& c : kCmds)
        if (c.code == w) return &c;
    return nullptr;
}

const char* kDlNames[] = { "DISPLAY", "BITMAP_SOURCE", "CLEAR_COLOR_RGB", "TAG", "COLOR_RGB",
  "BITMAP_HANDLE", "CELL", "BITMAP_LAYOUT", "BITMAP_SIZE", "ALPHA_FUNC", "STENCIL_FUNC",
  "BLEND_FUNC", "STENCIL_OP", "POINT_SIZE", "LINE_WIDTH", "CLEAR_COLOR_A", "COLOR_A",
  "CLEAR_STENCIL", "CLEAR_TAG", "STENCIL_MASK", "TAG_MASK", "BITMAP_TRANSFORM_A",
  "BITMAP_TRANSFORM_B", "BITMAP_TRANSFORM_C", "BITMAP_TRANSFORM_D", "BITMAP_TRANSFORM_E",
  "BITMAP_TRANSFORM_F", "SCISSOR_XY", "SCISSOR_SIZE", "CALL", "JUMP", "BEGIN", "COLOR_MASK",
  "END", "SAVE_CONTEXT", "RESTORE_CONTEXT", "RETURN", "MACRO", "CLEAR", "VERTEX_FORMAT",
  "BITMAP_LAYOUT_H", "BITMAP_SIZE_H", "PALETTE_SOURCE", "VERTEX_TRANSLATE_X",
  "VERTEX_TRANSLATE_Y", "NOP" };

std::string describeDl(uint32_t w)
{
    char buf[96];
    const uint32_t top = w >> 30;
    if (top == 1)
        snprintf(buf, sizeof(buf), "VERTEX2F x=%d y=%d", (int)((int32_t)(w << 2) >> 17), (int)((int32_t)(w << 17) >> 17));
    else if (top == 2)
        snprintf(buf, sizeof(buf), "VERTEX2II x=%u y=%u handle=%u cell=%u", (w >> 21) & 511, (w >> 12) & 511, (w >> 7) & 31, w & 127);
    else
    {
        const unsigned op = w >> 24;
        snprintf(buf, sizeof(buf), "%s 0x%06X", op < sizeof(kDlNames) / sizeof(*kDlNames) ? kDlNames[op] : "DL?", w & 0xFFFFFF);
    }
    return buf;
}

//----------------------------------------------------------------------------------------------
// Observes the bus: kicks of the command ring, frame boundaries (CMD_SWAP), command stream dump
//----------------------------------------------------------------------------------------------
class Analyzer : public EveSpiDecoder::Listener
{
  public:
    Analyzer(bool dump) : decoder(memory, this), dump(dump) {}

    void select() { decoder.select(); bursts++; }
    void deselect() { decoder.deselect(); }
    void byte(uint8_t b) { decoder.transfer(b); bytes++; }

    // true once per frame: the swap has been handed to the co-processor
    bool takeFrameBoundary()
    {
        const bool r = frameReady;
        frameReady = false;
        return r;
    }

    uint32_t cmdWriteOffset() const { return memory.read32(kRegCmdWrite) & 0xFFF; }

    uint64_t bytes = 0, bursts = 0, kicks = 0, swaps = 0, words = 0;
    std::map<std::string, uint64_t> histogram;

  protected:
    void onWriteBurst(uint32_t start, uint32_t end) override
    {
        if (!(start <= kRegCmdWrite && kRegCmdWrite < end)) return;
        kicks++;
        parseTo(memory.read32(kRegCmdWrite) & 0xFFF);
        if (sawSwap)
        {
            sawSwap = false;
            swaps++;
            frameReady = true;
            if (dump) printf("---- frame %llu ----\n", (unsigned long long)swaps);
        }
    }

  private:
    uint32_t ringWord(uint32_t off) const { return memory.read32(kRamCmd + (off & 0xFFF)); }

    void parseTo(uint32_t end)
    {
        auto remaining = [&] { return (end - cursor) & 0xFFF; };
        while (remaining() >= 4)
        {
            const uint32_t w = ringWord(cursor);

            if (skippingImage)
            {
                const CmdInfo* c = findCmd(w);
                if (!(c && (c->code == CMD_DLSTART || c->code == CMD_SWAP || c->code == CMD_GETPROPS ||
                            c->code == CMD_SNAPSHOT2)))
                {
                    cursor = (cursor + 4) & 0xFFF;
                    continue;
                }
                skippingImage = false;
            }

            if ((w >> 8) == 0xFFFFFF)
            {
                const CmdInfo* c = findCmd(w);
                if (!c)
                {
                    if (dump) printf("  CMD 0x%08X (unknown, resync)\n", w);
                    histogram["CMD_UNKNOWN"]++;
                    cursor = end;
                    return;
                }
                int need = (c->args + 1) * 4;
                if (remaining() < (uint32_t)need) return; // arguments not written yet
                uint32_t args[4] = {};
                for (int i = 0; i < c->args && i < 4; i++) args[i] = ringWord(cursor + 4 + 4 * i);

                std::string text;
                if (c->trailingString)
                {
                    uint32_t p = cursor + need;
                    std::string s;
                    bool done = false;
                    while (!done)
                    {
                        if (remaining() < ((p - cursor) & 0xFFF) + 4) return; // string incomplete
                        for (int k = 0; k < 4 && !done; k++)
                        {
                            const char ch = (char)(ringWord(p) >> (8 * k));
                            if (ch == 0) done = true; else s.push_back(ch);
                        }
                        p += 4;
                    }
                    need = (int)((p - cursor) & 0xFFF);
                    text = s;
                }
                if (dump)
                {
                    printf("  CMD_%s", c->name);
                    for (int i = 0; i < c->args && i < 4; i++) printf(" 0x%08X", args[i]);
                    if (c->trailingString) printf(" \"%s\"", text.c_str());
                    printf("\n");
                }
                histogram[std::string("CMD_") + c->name]++;
                if (c->code == CMD_SWAP) sawSwap = true;
                if (c->code == CMD_LOADIMAGE) skippingImage = true;
                cursor = (cursor + need) & 0xFFF;
                words++;
            }
            else
            {
                if (dump) printf("  DL %s\n", describeDl(w).c_str());
                histogram["DL_word"]++;
                cursor = (cursor + 4) & 0xFFF;
            }
        }
    }

    EveMemory memory;
    EveSpiDecoder decoder;
    bool dump;
    uint32_t cursor = 0;
    bool sawSwap = false, frameReady = false, skippingImage = false;
};

//----------------------------------------------------------------------------------------------
bool writePng(const std::string& path, const EveFrame& f)
{
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return false;

    static uint32_t crcTable[256];
    if (!crcTable[1])
        for (uint32_t n = 0; n < 256; n++)
        {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crcTable[n] = c;
        }
    auto crc = [&](uint32_t c, const uint8_t* d, size_t n) {
        for (size_t i = 0; i < n; i++) c = crcTable[(c ^ d[i]) & 0xFF] ^ (c >> 8);
        return c;
    };
    auto be32 = [](uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        uint8_t len[4];
        be32(len, (uint32_t)data.size());
        fwrite(len, 1, 4, fp);
        fwrite(type, 1, 4, fp);
        if (!data.empty()) fwrite(data.data(), 1, data.size(), fp);
        uint32_t c = crc(0xFFFFFFFFu, (const uint8_t*)type, 4);
        c = crc(c, data.data(), data.size()) ^ 0xFFFFFFFFu;
        uint8_t cb[4];
        be32(cb, c);
        fwrite(cb, 1, 4, fp);
    };

    const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, fp);
    std::vector<uint8_t> ihdr(13, 0);
    be32(&ihdr[0], f.width);
    be32(&ihdr[4], f.height);
    ihdr[8] = 8;
    ihdr[9] = 2; // RGB
    chunk("IHDR", ihdr);

    // raw scanlines (filter 0) wrapped in stored deflate blocks
    std::vector<uint8_t> raw;
    raw.reserve((size_t)f.height * (f.width * 3 + 1));
    for (int y = 0; y < f.height; y++)
    {
        raw.push_back(0);
        for (int x = 0; x < f.width; x++)
        {
            const uint32_t p = f.pixels[(size_t)y * f.width + x];
            raw.push_back((p >> 16) & 0xFF);
            raw.push_back((p >> 8) & 0xFF);
            raw.push_back(p & 0xFF);
        }
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    size_t pos = 0;
    do
    {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8);
        z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    uint8_t ad[4];
    be32(ad, (b << 16) | a);
    z.insert(z.end(), ad, ad + 4);
    chunk("IDAT", z);
    chunk("IEND", {});
    fclose(fp);
    return true;
}

// Read REG_CMD_READ / REG_CMD_WRITE through the backend until the co-processor has caught up
bool waitCoproEmpty(EveDevice& dev, int timeoutMs)
{
    auto readReg16 = [&](uint32_t addr) {
        dev.select();
        dev.transfer((uint8_t)(addr >> 16));
        dev.transfer((uint8_t)(addr >> 8));
        dev.transfer((uint8_t)addr);
        dev.transfer(0);
        const uint16_t lo = dev.transfer(0);
        const uint16_t hi = dev.transfer(0);
        dev.deselect();
        return (uint16_t)(lo | (hi << 8));
    };
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end)
    {
        if (readReg16(kRegCmdRead) == readReg16(kRegCmdWrite)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

int main(int argc, char** argv)
{
    std::string trace, backend = "passive", lib = "libbt8xxemu.so", pngDir;
    bool stats = false, dump = false;
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--backend" && i + 1 < argc) backend = argv[++i];
        else if (a == "--eve-lib" && i + 1 < argc) lib = argv[++i];
        else if (a == "--png-dir" && i + 1 < argc) pngDir = argv[++i];
        else if (a == "--max-frames" && i + 1 < argc) maxFrames = atoi(argv[++i]);
        else if (a == "--stats") stats = true;
        else if (a == "--dump") dump = true;
        else if (a[0] != '-' && trace.empty()) trace = a;
        else
        {
            fprintf(stderr, "usage: %s TRACE [--backend passive|bteve] [--eve-lib PATH] [--stats] [--dump] [--png-dir DIR] [--max-frames N]\n", argv[0]);
            return 2;
        }
    }
    if (trace.empty()) { fprintf(stderr, "no trace given\n"); return 2; }

    std::vector<evetrace::Event> events;
    if (!evetrace::load(trace, events))
    {
        fprintf(stderr, "cannot read trace %s\n", trace.c_str());
        return 1;
    }

    std::unique_ptr<EveDevice> dev;
    if (backend == "bteve")
    {
        std::string err;
        dev = makeBtEmuDevice(lib, err);
        if (!dev) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    }
    else
        dev = std::make_unique<PassiveEveDevice>();

    Analyzer an(dump);
    uint64_t frames = 0, saved = 0, missing = 0;
    bool selected = false;

    for (const auto& e : events)
    {
        if (e.kind == evetrace::Event::kSelect) { dev->select(); an.select(); selected = true; }
        else if (e.kind == evetrace::Event::kDeselect)
        {
            dev->deselect();
            an.deselect();
            selected = false;
            if (an.takeFrameBoundary())
            {
                frames++;
                if (!pngDir.empty() || backend == "bteve")
                {
                    if (!waitCoproEmpty(*dev, 3000)) fprintf(stderr, "frame %llu: co-processor did not drain\n", (unsigned long long)frames);
                    EveFrame f;
                    bool got = false;
                    for (int tries = 0; tries < 300 && !got; tries++)
                    {
                        got = dev->pollFrame(f);
                        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                    if (got && !pngDir.empty())
                    {
                        char name[64];
                        snprintf(name, sizeof(name), "/frame_%04llu.png", (unsigned long long)frames);
                        if (writePng(pngDir + name, f)) saved++;
                    }
                    else if (!got) missing++;
                }
                if (maxFrames && (int)frames >= maxFrames) break;
            }
        }
        else
        {
            for (uint8_t b : e.bytes) { dev->transfer(b); an.byte(b); }
        }
    }
    (void)selected;

    if (stats || !dump)
    {
        printf("trace: %zu events, %llu bytes, %llu bursts\n", events.size(),
               (unsigned long long)an.bytes, (unsigned long long)an.bursts);
        printf("command ring kicks: %llu, frames (CMD_SWAP): %llu\n", (unsigned long long)an.kicks,
               (unsigned long long)frames);
        if (backend == "bteve")
            printf("frames captured: %llu, PNGs written: %llu, frames missing: %llu\n", (unsigned long long)(frames - missing), (unsigned long long)saved, (unsigned long long)missing);
        for (const auto& [name, n] : an.histogram)
            printf("  %-18s %llu\n", name.c_str(), (unsigned long long)n);
    }
    return 0;
}
