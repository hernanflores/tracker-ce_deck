#include "TraceTee.h"

#include <cstring>

TraceTee::TraceTee(std::unique_ptr<EveDevice> dev, const std::string& path) : inner(std::move(dev))
{
    file = fopen(path.c_str(), "wb");
    if (file != nullptr) fwrite(evetrace::kMagic, 1, sizeof(evetrace::kMagic), file);
}

TraceTee::~TraceTee()
{
    if (file != nullptr)
    {
        flushRun();
        fclose(file);
    }
}

void TraceTee::flushRun()
{
    if (file == nullptr || run.empty()) return;
    const uint8_t tag = 'B';
    const uint32_t len = (uint32_t)run.size();
    fwrite(&tag, 1, 1, file);
    fwrite(&len, 4, 1, file); // little endian host assumed (x86_64 / arm64)
    fwrite(run.data(), 1, run.size(), file);
    run.clear();
}

void TraceTee::select()
{
    if (file != nullptr)
    {
        flushRun();
        fputc('S', file);
    }
    inner->select();
}

void TraceTee::deselect()
{
    if (file != nullptr)
    {
        flushRun();
        fputc('D', file);
    }
    inner->deselect();
}

uint8_t TraceTee::transfer(uint8_t mosi)
{
    if (file != nullptr) run.push_back(mosi);
    return inner->transfer(mosi);
}

bool evetrace::load(const std::string& path, std::vector<Event>& events)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, kMagic, 8) != 0)
    {
        fclose(f);
        return false;
    }
    int tag;
    while ((tag = fgetc(f)) != EOF)
    {
        Event e;
        if (tag == 'S')
            e.kind = Event::kSelect;
        else if (tag == 'D')
            e.kind = Event::kDeselect;
        else if (tag == 'B')
        {
            uint32_t len;
            if (fread(&len, 4, 1, f) != 1) break;
            e.kind = Event::kBytes;
            e.bytes.resize(len);
            if (len != 0 && fread(e.bytes.data(), 1, len, f) != len) break;
        }
        else
            break;
        events.push_back(std::move(e));
    }
    fclose(f);
    return true;
}
