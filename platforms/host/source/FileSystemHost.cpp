// POSIX implementation of FileSystem on a host directory that stands in for the SD card root.
// Behaviour follows platforms/imx/source/FileSystemImpl.cpp and platforms/imx/sdmmc/sd/SD.cpp
// (FatFs semantics: case-insensitive names, single-level mkdir, hidden files skipped, ...).
#include "HostPlatform.h"

#include "util_string.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

struct HostFile
{
    int fd = -1;
    bool writable = false;
    std::string path;
};

//----------------------------------------------------------------------------------------------
// helpers
//----------------------------------------------------------------------------------------------
static bool equalsNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

static bool isDirectory(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool pathExists(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

static bool endsWithNoCase(const std::string& name, const char* ext)
{
    const size_t n = strlen(ext);
    return name.size() >= n && equalsNoCase(name.substr(name.size() - n), ext);
}

static bool isMacMetaFile(const std::string& n)
{
    return n.size() >= 2 && n[0] == '.' && n[1] == '_';
}

static bool isMacMetaFolder(const std::string& n)
{
    return n == "__MACOSX";
}

FileSystemHost::FileSystemHost(std::string rootDir) : rootPath(std::move(rootDir))
{
    while (rootPath.size() > 1 && rootPath.back() == '/')
    {
        rootPath.pop_back();
    }
}

FileSystemHost::~FileSystemHost() = default;

void FileSystemHost::init()
{
    // an SD card always "inserted"; make sure the root directory exists
    ::mkdir(rootPath.c_str(), 0755);
}

std::string FileSystemHost::resolve(const char* cardPath) const
{
    std::string result = rootPath;
    if (cardPath == nullptr) return result;

    const char* p = cardPath;
    while (*p != '\0')
    {
        while (*p == '/') p++;
        if (*p == '\0') break;
        const char* end = p;
        while (*end != '\0' && *end != '/') end++;
        std::string comp(p, end - p);
        p = end;

        if (comp == ".") continue;
        if (comp == "..")
        {
            // never leave the card root
            size_t slash = result.find_last_of('/');
            if (result.size() > rootPath.size() && slash != std::string::npos)
            {
                result.erase(slash);
            }
            continue;
        }

        std::string exact = result + "/" + comp;
        if (pathExists(exact))
        {
            result = exact;
            continue;
        }

        // FAT is case-insensitive: look for a differently-cased match
        bool found = false;
        if (DIR* d = opendir(result.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                if (equalsNoCase(e->d_name, comp))
                {
                    result += "/";
                    result += e->d_name;
                    found = true;
                    break;
                }
            }
            closedir(d);
        }
        if (!found)
        {
            result = exact; // does not exist (yet): create/open with the requested spelling
        }
    }
    return result;
}

std::vector<FileSystemHost::Entry> FileSystemHost::readDir(const std::string& hostPath) const
{
    std::vector<Entry> entries;
    DIR* d = opendir(hostPath.c_str());
    if (d == nullptr) return entries;

    while (dirent* e = readdir(d))
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        Entry entry;
        entry.name = e->d_name;
        struct stat st;
        if (stat((hostPath + "/" + entry.name).c_str(), &st) != 0) continue;
        entry.isDir = S_ISDIR(st.st_mode);
        entry.size = entry.isDir ? 0 : (uint64_t)st.st_size;
        entry.hidden = entry.name[0] == '.' && !isMacMetaFile(entry.name);
        entries.push_back(std::move(entry));
    }
    closedir(d);

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        std::string la = a.name, lb = b.name;
        for (auto& c : la) c = (char)tolower((unsigned char)c);
        for (auto& c : lb) c = (char)tolower((unsigned char)c);
        return la < lb;
    });
    return entries;
}

//----------------------------------------------------------------------------------------------
// path operations
//----------------------------------------------------------------------------------------------
bool FileSystemHost::exists(const char* path)
{
    return pathExists(resolve(path));
}

bool FileSystemHost::rename(const char* oldPath, const char* newPath)
{
    return ::rename(resolve(oldPath).c_str(), resolve(newPath).c_str()) == 0;
}

bool FileSystemHost::mkdir(uint8_t, const char* path, bool)
{
    // FatFs f_mkdir: one level only, existing directory is not an error
    if (::mkdir(resolve(path).c_str(), 0755) == 0) return true;
    return errno == EEXIST && isDirectory(resolve(path));
}

bool FileSystemHost::remove(const char* path)
{
    const std::string p = resolve(path);
    if (isDirectory(p)) return ::rmdir(p.c_str()) == 0;
    return ::unlink(p.c_str()) == 0;
}

static bool deleteTree(FileSystemHost& fs, const std::string& hostPath, bool removeSelf)
{
    for (const auto& e : fs.readDir(hostPath))
    {
        const std::string child = hostPath + "/" + e.name;
        if (e.isDir)
        {
            if (!deleteTree(fs, child, true)) return false;
        }
        else if (::unlink(child.c_str()) != 0)
        {
            return false;
        }
    }
    return !removeSelf || ::rmdir(hostPath.c_str()) == 0;
}

bool FileSystemHost::removeDirWithFiles(const char* path)
{
    const std::string p = resolve(path);
    if (!isDirectory(p)) return false;
    return deleteTree(*this, p, true);
}

bool FileSystemHost::removeOnlyDirFiles(const char* path)
{
    const std::string p = resolve(path);
    if (!isDirectory(p)) return false;
    return deleteTree(*this, p, false);
}

size_t FileSystemHost::getFileSize(const char* path)
{
    struct stat st;
    if (stat(resolve(path).c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return 0;
    return (size_t)st.st_size;
}

//----------------------------------------------------------------------------------------------
// files
//----------------------------------------------------------------------------------------------
size_t FileSystemHost::getFileSize(FileHandle fileHandle)
{
    HostFile* f = (HostFile*)fileHandle;
    if (f == nullptr || f->fd < 0) return 0;
    struct stat st;
    return fstat(f->fd, &st) == 0 ? (size_t)st.st_size : 0;
}

size_t FileSystemHost::getFileAvailable(FileHandle fileHandle)
{
    HostFile* f = (HostFile*)fileHandle;
    if (f == nullptr || f->fd < 0) return 0;
    const off_t pos = lseek(f->fd, 0, SEEK_CUR);
    const size_t size = getFileSize(fileHandle);
    return pos >= 0 && (size_t)pos < size ? size - (size_t)pos : 0;
}

std::tuple<bool, FileSystem::FileHandle> FileSystemHost::openFile(const char* filepath,
                                                                  FileOpenMode openMode,
                                                                  FileHandle* fileHandle)
{
    HostFile* file;
    if (fileHandle == nullptr || *fileHandle == nullptr)
    {
        file = new HostFile;
        if (fileHandle != nullptr) *fileHandle = (FileHandle)file;
    }
    else
    {
        file = (HostFile*)*fileHandle;
        if (file->fd >= 0)
        {
            ::close(file->fd);
            file->fd = -1;
        }
    }

    int flags = O_RDONLY;
    switch (openMode)
    {
        case kWriteOpen:
            flags = O_RDWR | O_CREAT;
            break;
        case kWriteAppend:
            flags = O_RDWR | O_CREAT;
            break;
        case kWriteCreateNew:
            flags = O_RDWR | O_CREAT | O_TRUNC;
            break;
        default:
            break;
    }

    const std::string hostPath = resolve(filepath);
    bool ok = !isDirectory(hostPath);
    if (ok)
    {
        file->fd = ::open(hostPath.c_str(), flags, 0644);
        ok = file->fd >= 0;
    }
    if (ok)
    {
        file->writable = openMode != kRead;
        file->path = hostPath;
        if (openMode == kWriteAppend) lseek(file->fd, 0, SEEK_END);
    }
    else if (fileHandle == nullptr)
    {
        delete file;
        file = nullptr;
    }
    return std::make_tuple(ok, (FileHandle)file);
}

int32_t FileSystemHost::readFile(void* destBuf, size_t bufLen, FileHandle file)
{
    HostFile* f = (HostFile*)file;
    if (f == nullptr || f->fd < 0) return -1;
    return (int32_t)::read(f->fd, destBuf, bufLen);
}

int32_t FileSystemHost::writeFile(const void* data, size_t dataSize, FileHandle fileHandle)
{
    HostFile* f = (HostFile*)fileHandle;
    if (f == nullptr || f->fd < 0 || !f->writable) return -1;
    return (int32_t)::write(f->fd, data, dataSize);
}

bool FileSystemHost::seekFileCur(int offset, FileHandle fileHandle)
{
    HostFile* f = (HostFile*)fileHandle;
    if (f == nullptr || f->fd < 0) return false;
    if (offset == 0) return true;
    const off_t cur = lseek(f->fd, 0, SEEK_CUR);
    return cur >= 0 && seekFile((uint32_t)std::max<int64_t>(0, (int64_t)cur + offset), fileHandle);
}

bool FileSystemHost::seekFile(uint32_t pos, FileHandle fileHandle)
{
    HostFile* f = (HostFile*)fileHandle;
    if (f == nullptr || f->fd < 0) return false;
    // FatFs: read-only files are clamped to their size, writable files grow
    if (!f->writable)
    {
        pos = std::min<uint32_t>(pos, (uint32_t)getFileSize(fileHandle));
    }
    return lseek(f->fd, (off_t)pos, SEEK_SET) >= 0;
}

void FileSystemHost::closeFile(FileHandle file, bool deleteOnClose)
{
    HostFile* f = (HostFile*)file;
    if (f == nullptr) return;
    if (f->fd >= 0)
    {
        ::close(f->fd);
        f->fd = -1;
    }
    f->path.clear();
    if (deleteOnClose) delete f;
}

void FileSystemHost::endFile(FileHandle file)
{
    HostFile* f = (HostFile*)file;
    if (f == nullptr || f->fd < 0) return;
    const off_t pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos >= 0) (void)!ftruncate(f->fd, pos);
}

bool FileSystemHost::isFileOpen(FileHandle file)
{
    HostFile* f = (HostFile*)file;
    return f != nullptr && f->fd >= 0;
}

//----------------------------------------------------------------------------------------------
// listings (port of SdDir)
//----------------------------------------------------------------------------------------------
static bool filterEntry(const FileSystemHost::Entry& e,
                        FileSystem::enListFileFilter filter,
                        size_t dirPathLen)
{
    using F = FileSystem;
    if (e.hidden) return false;
    if (e.isDir && filter != F::enListFileFilter_folder && filter != F::enListFileFilter_all)
        return false;

    const std::string& n = e.name;
    switch (filter)
    {
        case F::enListFileFilter_all:
            return true;
        case F::enListFileFilter_folder:
            return e.isDir && !isMacMetaFolder(n);
        case F::enListFileFilter_wave_inst:
        case F::enListFileFilter_wave:
        {
            if (n.size() < 5) return false;
            bool ok = endsWithNoCase(n, ".wav");
            if (!ok && filter == F::enListFileFilter_wave_inst) ok = endsWithNoCase(n, ".pti");
            if (!ok) return false;
            if (dirPathLen + n.size() > 253) return false;
            return !isMacMetaFile(n);
        }
        case F::enListFileFilter_firmware:
        {
            constexpr char kStart[] = "TrackerMini";
            if (n.size() < strlen(kStart) + 4) return false;
            if (n.compare(n.size() - 4, 4, ".ptf") != 0) return false;
            if (n.compare(0, strlen(kStart), kStart) != 0) return false;
            return !isMacMetaFile(n);
        }
        case F::enListFileFilter_mod:
        {
            std::string low = n;
            for (auto& c : low) c = (char)tolower((unsigned char)c);
            return low.find(".mod") != std::string::npos || low.find(".it") != std::string::npos;
        }
        case F::enListFileFilter_game:
        {
            if (isMacMetaFile(n)) return false;
            std::string low = n;
            for (auto& c : low) c = (char)tolower((unsigned char)c);
            return low.find(".nes") != std::string::npos;
        }
    }
    return false;
}

static std::string cardDirPath(const char* path)
{
    // what SdDir keeps as dir_path (only its length matters for the filters)
    return path ? path : "";
}

int FileSystemHost::createFilesList(const char* path,
                                    uint8_t start_line,
                                    char** list,
                                    uint8_t list_length,
                                    uint16_t max_used_memory,
                                    enListFileFilter filter,
                                    uint16_t skip_count,
                                    uint16_t max_files_in_subdir,
                                    enListFileFilter folderSizeFilter)
{
    const std::string hostDir = resolve(path);
    if (!isDirectory(hostDir)) return kListError;
    const size_t dirPathLen = cardDirPath(path).size();

    for (uint8_t i = 0; i < list_length; i++)
    {
        if (list[i] != nullptr)
        {
            delete[] list[i];
            list[i] = nullptr;
        }
    }

    uint8_t n = start_line;
    if (max_used_memory < 255) return n;

    uint16_t memory_used = 0;
    for (const Entry& e : readDir(hostDir))
    {
        if (n >= list_length) break;
        if (memory_used > max_used_memory - 255) break;
        if (!filterEntry(e, filter, dirPathLen)) continue;
        if (skip_count > 0)
        {
            skip_count--;
            continue;
        }

        uint16_t len = (uint16_t)e.name.size() + (e.isDir ? 2 : 1);
        list[n] = new char[len];
        memory_used += len;

        if (e.isDir)
        {
            snprintf(list[n], len, "/%s", e.name.c_str());

            int16_t files_count = 0;
            for (const Entry& sub : readDir(hostDir + "/" + e.name))
            {
                if (filterEntry(sub, folderSizeFilter, dirPathLen + 1 + e.name.size())) files_count++;
            }

            if (max_files_in_subdir != 0 && files_count > max_files_in_subdir)
            {
                uint16_t offset = 0;
                len += 4 + 2 * 5;
                while (files_count > 0 && n < list_length - 1)
                {
                    if (list[n] != nullptr)
                    {
                        delete[] list[n];
                        memory_used -= len;
                    }
                    list[n] = new char[len];
                    memory_used += len;
                    snprintf(list[n],
                             len,
                             "/%s (%d-%d)",
                             e.name.c_str(),
                             offset + 1,
                             offset + max_files_in_subdir);
                    files_count -= max_files_in_subdir;
                    n++;
                    offset += max_files_in_subdir;
                }
                n--;
            }
        }
        else
        {
            strcpy(list[n], e.name.c_str());
        }
        n++;
    }
    return n;
}

int FileSystemHost::createInstrumentsList(const char* path,
                                          char** list,
                                          uint8_t list_length,
                                          uint32_t* sizeOut)
{
    const std::string hostDir = resolve(path);
    if (!isDirectory(hostDir)) return kListError;

    uint8_t found = 0;
    for (const Entry& e : readDir(hostDir))
    {
        if (found >= list_length) break;
        if (e.hidden || e.isDir) continue;

        const std::string& name = e.name;
        const int nameLen = (int)name.size();
        if (nameLen < 5 || !endsWithNoCase(name, ".pti")) continue;

        const char* p = name.c_str();
        int idx1, idx2 = -1;
        if (*p >= '0' && *p <= '9')
            idx1 = *p - '0';
        else
            continue;
        p++;
        if (*p >= '0' && *p <= '9')
        {
            idx2 = *p - '0';
            p++;
        }
        if (*p != ' ') continue;
        p++;
        if (idx2 >= 0) idx1 = idx1 * 10 + idx2;
        if (idx1 < 1 || idx1 > 48) continue;
        idx1--;
        if (list[idx1] != nullptr) continue;

        const int len = nameLen - (int)(p - name.c_str()) - 4;
        list[idx1] = new char[len + 1];
        strncpy(list[idx1], p, len);
        list[idx1][len] = 0;

        *sizeOut += (uint32_t)e.size;
        found++;
    }
    return found;
}

int FileSystemHost::countContainedFiles(const char* path)
{
    const std::string hostDir = resolve(path);
    const size_t dirPathLen = cardDirPath(path).size();
    uint8_t n = 0;
    for (const Entry& e : readDir(hostDir))
    {
        if (e.hidden) continue;
        if (!e.isDir)
        {
            if (e.name.size() < 5 || !endsWithNoCase(e.name, ".wav")) continue;
            if (dirPathLen + e.name.size() > 253) continue;
        }
        n++;
    }
    return n;
}

void FileSystemHost::listDirContents(const char* dirpath,
                                     DirContentType contentType,
                                     std::function<bool(const char* name)> nameFilter,
                                     char* namesBuffer,
                                     int namesBufferSize,
                                     int maxNameLen,
                                     int& count,
                                     char** fileList)
{
    const std::string hostDir = resolve(dirpath);
    if (!isDirectory(hostDir))
    {
        count = 0;
        return;
    }

    int listPos = 0;
    for (const Entry& e : readDir(hostDir))
    {
        if (listPos >= count) break;
        if (e.hidden) continue;

        int nameLen;
        if (contentType == kFolders)
        {
            if (!e.isDir || isMacMetaFolder(e.name)) continue;
            if (nameFilter != nullptr && !nameFilter(e.name.c_str())) continue;
            nameLen = (int)e.name.size() + 2;
        }
        else
        {
            if (e.isDir || isMacMetaFile(e.name)) continue;
            if (nameFilter != nullptr && !nameFilter(e.name.c_str())) continue;
            nameLen = (int)e.name.size() + 1;
        }

        if (nameLen > namesBufferSize) break;
        if (nameLen > maxNameLen) continue;

        fileList[listPos] = namesBuffer;
        if (e.isDir)
        {
            *namesBuffer = '/';
            namesBuffer++;
        }
        polystrncpy(namesBuffer, e.name.c_str(), maxNameLen + 1);
        namesBuffer += nameLen;
        namesBufferSize -= nameLen;
        listPos++;
    }
    count = listPos;

    std::sort(fileList, fileList + count, [](const char* a, const char* b) {
        return compareStrings(a, b);
    });
}

int FileSystemHost::readNbFiles(const char* romPath)
{
    int total = 0;
    for (const Entry& e : readDir(resolve(romPath)))
    {
        (void)e;
        total++;
    }
    return total;
}

bool FileSystemHost::nesDir_open(const char* path)
{
    nesEntries = readDir(resolve(path));
    nesPos = 0;
    return isDirectory(resolve(path));
}

bool FileSystemHost::nesDir_readItem(char* path, uint8_t* isDir, uint8_t* isHid, uint32_t* size)
{
    if (nesPos >= nesEntries.size()) return false;
    const Entry& e = nesEntries[nesPos++];
    *isDir = e.isDir ? 1 : 0;
    *isHid = e.hidden ? 1 : 0;
    if (size != nullptr) *size = (uint32_t)e.size;
    strcpy(path, e.name.c_str());
    return true;
}

void FileSystemHost::nesDir_close()
{
    nesEntries.clear();
    nesPos = 0;
}

//----------------------------------------------------------------------------------------------
// ConfigManagerHost (same logic as platforms/imx/source/ConfigManagerImpl.cpp)
//----------------------------------------------------------------------------------------------
bool ConfigManagerHost::readData(int address, void* data, int size)
{
    if (fileSystem == nullptr || !fileSystem->exists(kConfigFilePath)) return false;

    auto [isOpen, file] = fileSystem->openFile(kConfigFilePath);
    if (!isOpen) return false;

    fileSystem->seekFile(address, file);
    fileSystem->readFile(data, size, file);
    fileSystem->closeFile(file);
    return true;
}

void ConfigManagerHost::writeData(int address, void* data, int size)
{
    if (fileSystem == nullptr) return;

    auto [isOpen, file] = fileSystem->openFile(kConfigFilePath, FileSystem::FileOpenMode::kWriteOpen);
    if (!isOpen) return;

    fileSystem->seekFile(address, file);
    fileSystem->writeFile(data, size, file);
    fileSystem->closeFile(file);
}
