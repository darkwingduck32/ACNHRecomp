// ZIP save validation and extraction shared by the runtime and launcher.
#include "save_archive.h"
#include "save_storage.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <set>
#include <vector>

#if defined(NX_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace fs = std::filesystem;
namespace nx::save_archive {
namespace {
constexpr uint64_t kMaxZipBytes = 1ull << 30;
fs::path u8(const std::string& s) { return save_storage::u8_path(s); }
std::string utf8(const fs::path& p) { return save_storage::path_utf8(p); }
void set(std::string* out, const std::string& value) { if (out) *out = value; }

// ---- zip ----

uint16_t rd16(const unsigned char* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const unsigned char* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16) | uint32_t(p[3]) << 24; }
void wr16(std::string& s, uint32_t v) { s.push_back(char(v)), s.push_back(char(v >> 8)); }
void wr32(std::string& s, uint32_t v) { wr16(s, v & 0xFFFF), wr16(s, v >> 16); }

uint32_t crc32_of(const std::string& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool read_at(std::ifstream& f, uint64_t off, size_t size, std::string& out) {
    out.resize(size);
    f.clear();
    f.seekg(std::streamoff(off));
    if (size) f.read(out.data(), std::streamsize(size));
    return bool(f);
}

struct ZipEntry {
    std::string name;  // '/' separated, checked: relative, no ".."
    uint16_t flags = 0, method = 0;
    uint32_t crc = 0, csize = 0, usize = 0, offset = 0;
};

// Rejects what could write outside the destination: absolute paths, drive letters, "..".
bool safe_name(std::string& n) {
    std::replace(n.begin(), n.end(), '\\', '/');
    if (n.empty() || n[0] == '/' || n.find(':') != std::string::npos) return false;
    size_t pos = 0;
    while (pos <= n.size()) {
        size_t slash = n.find('/', pos);
        if (slash == std::string::npos) slash = n.size();
        if (n.compare(pos, slash - pos, "..") == 0 && slash - pos == 2) return false;
        pos = slash + 1;
    }
    return true;
}

// The central directory (no zip64: a save never needs it). Directory entries and macOS's __MACOSX/ are left out.
bool zip_list(const fs::path& zip, std::vector<ZipEntry>& out, std::string* error) {
    std::ifstream f(zip, std::ios::binary);
    if (!f) return set(error, "cannot open " + utf8(zip)), false;
    f.seekg(0, std::ios::end);
    uint64_t size = uint64_t(f.tellg());
    if (size < 22) return set(error, "not a zip file"), false;
    size_t tail = size_t(std::min<uint64_t>(size, 22 + 0xFFFF));
    std::string buf;
    if (!read_at(f, size - tail, tail, buf)) return set(error, "cannot read the zip file"), false;
    const auto* b = reinterpret_cast<const unsigned char*>(buf.data());
    size_t eocd = std::string::npos;
    for (size_t i = tail - 22 + 1; i-- > 0;)
        if (rd32(b + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos) return set(error, "not a zip file"), false;
    uint16_t count = rd16(b + eocd + 10);
    uint32_t cd_size = rd32(b + eocd + 12), cd_off = rd32(b + eocd + 16);
    if (count == 0xFFFF || cd_size == 0xFFFFFFFF || cd_off == 0xFFFFFFFF)
        return set(error, "zip64 not supported"), false;
    if (uint64_t(cd_off) + cd_size > size) return set(error, "damaged zip file"), false;
    std::string cd;
    if (!read_at(f, cd_off, cd_size, cd)) return set(error, "cannot read the zip file"), false;
    const auto* c = reinterpret_cast<const unsigned char*>(cd.data());
    size_t pos = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (pos + 46 > cd.size() || rd32(c + pos) != 0x02014b50) return set(error, "damaged zip file"), false;
        ZipEntry e;
        e.flags = rd16(c + pos + 8);
        e.method = rd16(c + pos + 10);
        e.crc = rd32(c + pos + 16);
        e.csize = rd32(c + pos + 20);
        e.usize = rd32(c + pos + 24);
        size_t nlen = rd16(c + pos + 28), xlen = rd16(c + pos + 30), clen = rd16(c + pos + 32);
        e.offset = rd32(c + pos + 42);
        if (pos + 46 + nlen + xlen + clen > cd.size()) return set(error, "damaged zip file"), false;
        e.name.assign(cd, pos + 46, nlen);
        pos += 46 + nlen + xlen + clen;
        if (!safe_name(e.name)) return set(error, "unsafe path in zip: " + e.name), false;
        if (e.name.back() == '/' || e.name.rfind("__MACOSX/", 0) == 0) continue;
        out.push_back(std::move(e));
    }
    return true;
}

// The folder prefix ("" or "a/" or "a/b/") of the one save in the zip.
bool zip_find_root(const std::vector<ZipEntry>& entries, std::string* prefix, std::string* why) {
    std::set<std::string> names;
    for (const ZipEntry& e : entries) names.insert(e.name);
    std::vector<std::string> found;
    bool any_main = false;
    for (const std::string& n : names) {
        if (n.size() < 8 || n.compare(n.size() - 8, 8, "main.dat") != 0) continue;
        std::string p = n.substr(0, n.size() - 8);
        if (!p.empty() && p.back() != '/') continue;
        if (std::count(p.begin(), p.end(), '/') > 2) continue;
        any_main = true;
        if (names.count(p + "mainHeader.dat") && names.count(p + "Villager0/personal.dat")) found.push_back(p);
    }
    if (found.empty()) {
        if (!any_main) return set(why, "main.dat missing"), false;
        return set(why, "mainHeader.dat or Villager0/personal.dat missing"), false;
    }
    if (found.size() > 1) return set(why, std::to_string(found.size()) + " saves in this zip: it must hold one"), false;
    *prefix = found[0];
    return true;
}

bool zip_inflate(const std::string& in, std::string& out, std::string* error) {
#if defined(NX_HAVE_ZLIB)
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return set(error, "zlib failed"), false;
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    z.avail_in = uInt(in.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    int r = inflate(&z, Z_FINISH);
    bool ok = r == Z_STREAM_END && z.total_out == out.size();
    inflateEnd(&z);
    if (!ok) set(error, "damaged zip file (inflate)");
    return ok;
#else
    (void)in, (void)out;
    return set(error, "zip not supported in this build"), false;
#endif
}

// The entries under `prefix`, written below `dest` without it.
bool zip_extract(const fs::path& zip, const std::vector<ZipEntry>& entries, const std::string& prefix,
                 const fs::path& dest, std::string* error) {
    std::ifstream f(zip, std::ios::binary);
    if (!f) return set(error, "cannot open " + utf8(zip)), false;
    uint64_t total = 0;
    std::string head, data, plain;
    for (const ZipEntry& e : entries) {
        if (e.name.compare(0, prefix.size(), prefix) != 0) continue;
        if (e.flags & 1) return set(error, "encrypted zip entries are not supported"), false;
        if (e.method != 0 && e.method != 8) return set(error, "unsupported zip compression in " + e.name), false;
        total += e.usize;
        if (total > kMaxZipBytes) return set(error, "zip too large for a save"), false;
        if (!read_at(f, e.offset, 30, head) || rd32(reinterpret_cast<const unsigned char*>(head.data())) != 0x04034b50)
            return set(error, "damaged zip file"), false;
        const auto* h = reinterpret_cast<const unsigned char*>(head.data());
        uint64_t at = uint64_t(e.offset) + 30 + rd16(h + 26) + rd16(h + 28);
        if (!read_at(f, at, e.csize, data)) return set(error, "damaged zip file"), false;
        if (e.method == 0) {
            if (e.csize != e.usize) return set(error, "damaged zip file"), false;
            plain.swap(data);
        } else {
            plain.assign(e.usize, '\0');
            if (!zip_inflate(data, plain, error)) return false;
        }
        if (crc32_of(plain) != e.crc) return set(error, "damaged zip file (CRC mismatch in " + e.name + ")"), false;
        fs::path out = dest / u8(e.name.substr(prefix.size()));
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        std::ofstream o(out, std::ios::binary | std::ios::trunc);
        o.write(plain.data(), std::streamsize(plain.size()));
        o.close();
        if (!o) return set(error, "cannot write " + utf8(out)), false;
    }
    return true;
}

// Stored (uncompressed) entries: the save files are encrypted, deflate would gain nothing.
bool zip_write(const fs::path& src, const fs::path& zip, std::string* error) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) files.push_back(it->path());
    if (ec) return set(error, "cannot read " + utf8(src) + ": " + ec.message()), false;
    std::sort(files.begin(), files.end());

    std::tm tm{};
    std::time_t t = std::time(nullptr);
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    uint16_t dos_time = uint16_t(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
    uint16_t dos_date = uint16_t((std::max(tm.tm_year - 80, 0)) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);

    std::ofstream o(zip, std::ios::binary | std::ios::trunc);
    if (!o) return set(error, "cannot write " + utf8(zip)), false;
    std::string central, head, data;
    uint64_t offset = 0;
    for (const fs::path& p : files) {
        auto rel = p.lexically_relative(src).generic_u8string();
        std::string name(rel.begin(), rel.end());
        std::ifstream in(p, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (!in && !in.eof()) return set(error, "cannot read " + utf8(p)), false;
        if (data.size() >= 0xFFFFFFFFull || offset >= 0xFFFFFFFFull)
            return set(error, "save too large for a zip"), false;
        uint32_t crc = crc32_of(data);
        head.clear();
        wr32(head, 0x04034b50), wr16(head, 20), wr16(head, 1 << 11), wr16(head, 0);  // version, UTF-8 names, stored
        wr16(head, dos_time), wr16(head, dos_date), wr32(head, crc), wr32(head, uint32_t(data.size()));
        wr32(head, uint32_t(data.size())), wr16(head, uint32_t(name.size())), wr16(head, 0);
        wr32(central, 0x02014b50), wr16(central, 20), wr16(central, 20), wr16(central, 1 << 11), wr16(central, 0);
        wr16(central, dos_time), wr16(central, dos_date), wr32(central, crc), wr32(central, uint32_t(data.size()));
        wr32(central, uint32_t(data.size())), wr16(central, uint32_t(name.size())), wr16(central, 0), wr16(central, 0);
        wr16(central, 0), wr16(central, 0), wr32(central, 0), wr32(central, uint32_t(offset));
        central += name;
        o.write(head.data(), std::streamsize(head.size()));
        o.write(name.data(), std::streamsize(name.size()));
        o.write(data.data(), std::streamsize(data.size()));
        offset += head.size() + name.size() + data.size();
    }
    if (files.size() > 0xFFFF || offset > 0xFFFFFFFFull) return set(error, "save too large for a zip"), false;
    std::string end;
    wr32(end, 0x06054b50), wr16(end, 0), wr16(end, 0), wr16(end, uint32_t(files.size()));
    wr16(end, uint32_t(files.size())), wr32(end, uint32_t(central.size())), wr32(end, uint32_t(offset)), wr16(end, 0);
    o.write(central.data(), std::streamsize(central.size()));
    o.write(end.data(), std::streamsize(end.size()));
    o.close();
    if (!o) return set(error, "cannot write " + utf8(zip)), false;
    return true;
}

}  // namespace

bool validate(const fs::path& zip, std::string* error) {
    std::vector<ZipEntry> entries;
    std::string prefix;
    return zip_list(zip, entries, error) && zip_find_root(entries, &prefix, error);
}

bool extract(const fs::path& zip, const fs::path& dest, std::string* error) {
    std::vector<ZipEntry> entries;
    std::string prefix;
    return zip_list(zip, entries, error) && zip_find_root(entries, &prefix, error) &&
           zip_extract(zip, entries, prefix, dest, error);
}

bool write(const fs::path& src, const fs::path& zip, std::string* error) {
    return zip_write(src, zip, error);
}
}  // namespace nx::save_archive
