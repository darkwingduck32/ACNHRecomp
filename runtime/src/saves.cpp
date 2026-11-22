// Save data management for the settings screen: see saves.h.
//
// Where other programs keep an ACNH save (title id 01006F8002326000, a device save: one per console, user id zero):
//   yuzu and forks (suyu, sudachi, citron, eden, torzu): <emu>/nand/user/save/0000000000000000/<user id, 32 hex>/
//     01006F8002326000/ (user id all zeros for a device save), or the newer <emu>/nand/user/save/device/
//     01006F8002326000/0/ that these emulators prefer when present (src/core/file_sys/savedata_factory.cpp).
//   Ryujinx / Ryubing (LibHac): <Ryujinx>/bis/user/save/<save data id, 16 hex>/0/ (committed; 1/ is the working copy).
//     The id -> title map is in bis/system/save/8000000000000000/0/imkvdb.arc; we look inside each folder instead,
//     which needs no parser and finds the save even when that index is stale.
//   JKSV: sdmc:/JKSV/<title name>/<backup>/ (folders, or .zip with the files at their root).
//   Checkpoint: sdmc:/switch/Checkpoint/saves/0x01006F8002326000 <title name>/<backup>/.
// The save itself: main.dat + mainHeader.dat (the island), Villager0..7/personal.dat + personalHeader.dat (residents),
// landname.dat, photo_studio_island.dat, postbox.dat, profile.dat... (encrypted; copied as they are).
#include "saves.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <set>

#include <nx/runtime.h>

#include "platform.h"
#include "save_storage.h"
#include "save_archive.h"

namespace fs = std::filesystem;

namespace nx::saves {

namespace {

constexpr const char* kTitle = "01006F8002326000";
constexpr size_t kKeepBackups = 10;

// One operation at a time: the settings screen may run them on a worker thread.
std::mutex g_lock;

fs::path u8(const std::string& s) { return fs::u8path(s); }

std::string utf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string lower(std::string s) {
    for (char& ch : s)
        if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    return s;
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

bool is_file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool ends_with_zip(const std::string& s) { return s.size() >= 4 && lower(s.substr(s.size() - 4)) == ".zip"; }

void set(std::string* out, const std::string& v) {
    if (out) *out = v;
}

fs::path user_dir() { return save_storage::user_dir(plat::data_root()); }
fs::path device_dir() { return save_storage::device_dir(plat::data_root()); }
fs::path backups_dir() { return user_dir() / "backups"; }

bool flat_save(const fs::path& d, std::string* why) { return save_storage::flat_save(d, why); }
bool copy_tree(const fs::path& src, const fs::path& dst, std::string* error) {
    return save_storage::copy_tree(src, dst, error);
}
bool non_empty_dir(const fs::path& d) { return save_storage::non_empty_dir(d); }
void recover() { save_storage::recover(plat::data_root()); }
bool swap_in(const fs::path& staging, std::string* error) {
    return save_storage::swap_in(plat::data_root(), staging, error);
}

std::string stamp(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

// A name that does not exist yet in `dir`: base, base-2, base-3... (two exports or backups in the same second).
fs::path unique_in(const fs::path& dir, const std::string& base, const std::string& ext = "") {
    std::error_code ec;
    fs::path p = dir / u8(base + ext);
    for (int n = 2; fs::exists(p, ec) && n < 1000; ++n) p = dir / u8(base + "-" + std::to_string(n) + ext);
    return p;
}

// ---- recognising a save ----

std::vector<fs::path> subdirs(const fs::path& d) {
    std::vector<fs::path> v;
    std::error_code ec;
    for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) v.push_back(it->path());
    std::sort(v.begin(), v.end());
    return v;
}

// The save in `d` itself, else the only one one or two levels down (JKSV / Checkpoint keep a folder per backup).
// A Ryujinx save folder holds 0/ (committed) and maybe 1/ (working copy): 0 wins.
bool find_root(const fs::path& d, fs::path* root, std::string* why) {
    std::string own_why;
    if (flat_save(d, &own_why)) return *root = d, true;
    if (!is_dir(d)) return set(why, own_why), false;
    std::vector<fs::path> found;
    for (const fs::path& a : subdirs(d)) {
        if (flat_save(a, nullptr)) {
            found.push_back(a);
            continue;
        }
        for (const fs::path& b : subdirs(a))
            if (flat_save(b, nullptr)) found.push_back(b);
    }
    if (found.size() == 2 && found[0].filename() == "0" && found[1].filename() == "1" &&
        found[0].parent_path() == found[1].parent_path())
        found.pop_back();
    if (found.empty()) return set(why, own_why), false;
    if (found.size() > 1)
        return set(why, std::to_string(found.size()) + " saves in this folder: choose one of them"), false;
    *root = found[0];
    return true;
}

std::vector<std::string> backups_sorted() {
    std::vector<std::string> v;
    std::error_code ec;
    for (fs::directory_iterator it(backups_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = utf8(it->path().filename());
        // YYYYmmdd-HHMMSS[-n]; ".tmp" folders are backups still being written (or abandoned by a crash).
        bool ok = it->is_directory(ec) && n.size() >= 15 && n[8] == '-' && n.find(".tmp") == std::string::npos;
        for (size_t i = 0; ok && i < 15; ++i) ok = i == 8 || (n[i] >= '0' && n[i] <= '9');
        if (ok) v.push_back(n);
    }
    // Newest first. Same-second names: "x-2" sorts after "x" and is newer, which descending order also gives,
    // but "x-10" vs "x-9" does not: compare the suffix as a number.
    auto key = [](const std::string& n) {
        return std::make_pair(n.substr(0, 15), n.size() > 16 ? std::atoi(n.c_str() + 16) : 0);
    };
    std::sort(v.begin(), v.end(), [&](const std::string& a, const std::string& b) { return key(a) > key(b); });
    return v;
}

std::string backup_locked(std::string* error) {
    fs::path dir = device_dir();
    if (!non_empty_dir(dir)) return set(error, "no save to back up"), std::string();
    std::error_code ec;
    fs::create_directories(backups_dir(), ec);
    // Same second as an existing backup: a suffix past the highest one, not the first free name (pruning may have
    // freed the bare name, and reusing it would sort the newest backup as the oldest and prune it at once).
    std::string base = stamp("%Y%m%d-%H%M%S");
    int next = 0;
    for (const std::string& n : backups_sorted())
        if (n.compare(0, base.size(), base) == 0) next = std::max(next, n.size() > 16 ? std::atoi(n.c_str() + 16) : 1);
    fs::path final_path = unique_in(backups_dir(), next ? base + "-" + std::to_string(next + 1) : base);
    fs::path tmp = u8(utf8(final_path) + ".tmp");
    fs::remove_all(tmp, ec);
    if (!copy_tree(dir, tmp, error)) {
        fs::remove_all(tmp, ec);
        return std::string();
    }
    fs::rename(tmp, final_path, ec);
    if (ec) {
        fs::remove_all(tmp, ec);
        return set(error, "cannot finish the backup: " + ec.message()), std::string();
    }
    std::string name = utf8(final_path.filename());
    std::vector<std::string> all = backups_sorted();
    for (size_t i = kKeepBackups; i < all.size(); ++i) fs::remove_all(backups_dir() / u8(all[i]), ec);
    for (fs::directory_iterator it(backups_dir(), ec), end; !ec && it != end; it.increment(ec))
        if (utf8(it->path().filename()).find(".tmp") != std::string::npos) fs::remove_all(it->path(), ec);
    log("saves: backup %s", name.c_str());
    return name;
}

// Stages `from` (folder or zip) next to device_save, backs the current save up, swaps the staged one in.
bool import_locked(const fs::path& from, std::string* error) {
    recover();
    fs::path staging = u8(utf8(device_dir()) + ".new");
    std::error_code ec;
    fs::create_directories(user_dir(), ec);
    fs::remove_all(staging, ec);
    if (is_file(from)) {
        if (!save_archive::extract(from, staging, error)) return fs::remove_all(staging, ec), false;
    } else {
        fs::path root;
        if (!find_root(from, &root, error)) return false;
        // Importing the save onto itself would copy it into its own staging copy and then delete it.
        if (fs::equivalent(root, device_dir(), ec)) return set(error, "this is the current save"), false;
        if (!copy_tree(root, staging, error)) return fs::remove_all(staging, ec), false;
    }
    if (!flat_save(staging, error)) return fs::remove_all(staging, ec), false;
    if (non_empty_dir(device_dir())) {
        std::string berr;
        if (backup_locked(&berr).empty()) {
            fs::remove_all(staging, ec);
            return set(error, "cannot back up the current save: " + berr), false;
        }
    }
    if (!swap_in(staging, error)) return fs::remove_all(staging, ec), false;
    log("saves: imported %s", utf8(from).c_str());
    return true;
}

// ---- detection ----

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

fs::path home() {
#if defined(_WIN32)
    std::string h = env("USERPROFILE");
#else
    std::string h = env("HOME");
#endif
    return h.empty() ? fs::path() : u8(h);
}

struct Scan {
    std::vector<Found> out;
    std::set<std::string> seen;  // canonical paths: a ~/.local/share symlink into a Flatpak folder is found twice
    fs::path own;                // our own device_save, never offered

    void add(const std::string& label, const fs::path& p) {
        std::error_code ec;
        fs::path c = fs::weakly_canonical(p, ec);
        if (ec) c = p;
        if (!own.empty() && fs::equivalent(c, own, ec)) return;
        if (!seen.insert(utf8(c)).second) return;
        out.push_back({label, utf8(p)});
    }
};

// <emu>/nand/user/save: the newer device/ location first (it wins in the emulator), then the zero user, then any.
void scan_yuzu(Scan& s, const fs::path& root, const std::string& label) {
    fs::path save = root / "nand" / "user" / "save";
    if (!is_dir(save)) return;
    const std::string titles[] = {kTitle, lower(kTitle)};
    for (const std::string& t : titles)
        if (flat_save(save / "device" / t / "0", nullptr)) s.add(label, save / "device" / t / "0");
    fs::path zero_user = save / "0000000000000000" / "00000000000000000000000000000000";
    for (const std::string& t : titles)
        if (flat_save(zero_user / t, nullptr)) s.add(label, zero_user / t);
    for (const fs::path& user : subdirs(save / "0000000000000000"))
        for (const std::string& t : titles)
            if (flat_save(user / t, nullptr)) s.add(label, user / t);
}

// <Ryujinx>/bis/user/save/<id>/0 (committed) or /1.
void scan_ryujinx(Scan& s, const fs::path& root, const std::string& label) {
    fs::path save = root / "bis" / "user" / "save";
    for (const fs::path& d : subdirs(save)) {
        if (d.filename().u8string().size() != 16) continue;
        if (flat_save(d / "0", nullptr))
            s.add(label, d / "0");
        else if (flat_save(d / "1", nullptr))
            s.add(label, d / "1");
    }
}

fs::file_time_type mtime(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    return ec ? fs::file_time_type::min() : t;
}

bool zip_is_save(const fs::path& zip) {
    return save_archive::validate(zip);
}

// The newest save (folder or zip) one or two levels below a JKSV / Checkpoint title folder.
void add_newest_backup(Scan& s, const fs::path& title_dir, const std::string& label) {
    fs::path best;
    fs::file_time_type best_time = fs::file_time_type::min();
    auto consider = [&](const fs::path& p) {
        std::error_code ec;
        fs::file_time_type t;
        if (is_dir(p) && flat_save(p, nullptr))
            t = mtime(p / "main.dat");
        else if (fs::is_regular_file(p, ec) && ends_with_zip(utf8(p.filename())) && zip_is_save(p))
            t = mtime(p);
        else
            return false;
        if (best.empty() || t > best_time) best = p, best_time = t;
        return true;
    };
    std::error_code ec;
    for (fs::directory_iterator it(title_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (consider(it->path()) || !is_dir(it->path())) continue;
        std::error_code e2;
        for (fs::directory_iterator jt(it->path(), e2), jend; !e2 && jt != jend; jt.increment(e2)) consider(jt->path());
    }
    if (!best.empty()) s.add(label, best);
}

// sdmc:/JKSV/<title name>/ and sdmc:/switch/Checkpoint/saves/0x<title id> <title name>/ on a mounted volume.
void scan_volume(Scan& s, const fs::path& v, const std::string& name) {
    for (const fs::path& t : subdirs(v / "JKSV")) {
        std::string n = lower(utf8(t.filename()));
        if (n.find("animal crossing") != std::string::npos || n.find(lower(kTitle)) != std::string::npos)
            add_newest_backup(s, t, "JKSV (" + name + ")");
    }
    for (const fs::path& t : subdirs(v / "switch" / "Checkpoint" / "saves"))
        if (lower(utf8(t.filename())).rfind("0x" + lower(kTitle), 0) == 0)
            add_newest_backup(s, t, "Checkpoint (" + name + ")");
}

std::vector<fs::path> volumes() {
    std::vector<fs::path> v;
#if defined(_WIN32)
    for (char d = 'D'; d <= 'Z'; ++d) {
        fs::path p = std::string(1, d) + ":\\";
        if (is_dir(p)) v.push_back(p);
    }
#elif defined(__APPLE__)
    v = subdirs("/Volumes");
#else
    std::string user = env("USER");
    if (!user.empty()) {
        for (const fs::path& p : subdirs(u8("/media/" + user))) v.push_back(p);
        for (const fs::path& p : subdirs(u8("/run/media/" + user))) v.push_back(p);
    }
    for (const char* base : {"/media", "/mnt"})
        for (const fs::path& p : subdirs(base))
            if (utf8(p.filename()) != user) v.push_back(p);
#endif
    return v;
}

struct Emu {
    const char* folder;
    const char* label;
    bool ryujinx;
};
const Emu kEmus[] = {
    {"yuzu", "Yuzu", false},       {"suyu", "Suyu", false},   {"sudachi", "Sudachi", false},
    {"citron", "Citron", false},   {"eden", "Eden", false},   {"torzu", "Torzu", false},
    {"Ryujinx", "Ryujinx", true},
};

void scan_emu_base(Scan& s, const fs::path& base, const std::string& suffix) {
    for (const Emu& e : kEmus) {
        fs::path root = base / e.folder;
        if (!is_dir(root)) continue;
        std::string label = std::string(e.label) + suffix;
        if (e.ryujinx)
            scan_ryujinx(s, root, label);
        else
            scan_yuzu(s, root, label);
    }
}

// Other copies of this project: <somewhere>/<folder with "acnh" in its name>/private/acnh/user/device_save.
void scan_recomps(Scan& s) {
    fs::path h = home();
    if (h.empty()) return;
    for (const char* sub : {"", "Downloads", "Desktop", "Documents", "Games", "Applications", "Projects"})
        for (const fs::path& d : subdirs(*sub ? h / sub : h)) {
            if (lower(utf8(d.filename())).find("acnh") == std::string::npos) continue;
            fs::path p = d / "private" / "acnh" / "user" / "device_save";
            if (flat_save(p, nullptr)) s.add("ACNHRecomp (" + utf8(d.filename()) + ")", p);
        }
}

}  // namespace

std::string save_dir() {
    try {
        return utf8(device_dir());
    } catch (...) {
        return std::string();
    }
}

bool has_save() {
    try {
        std::lock_guard lk(g_lock);
        recover();
        return flat_save(device_dir(), nullptr);
    } catch (...) {
        return false;
    }
}

std::string last_modified() {
    try {
        return save_storage::last_modified(device_dir());
    } catch (...) {
        return std::string();
    }
}

std::vector<Found> detect_emulator_saves() {
    try {
        Scan s;
        std::error_code ec;
        s.own = fs::weakly_canonical(device_dir(), ec);
        if (ec) s.own.clear();
        fs::path h = home();
#if defined(_WIN32)
        if (std::string a = env("APPDATA"); !a.empty()) scan_emu_base(s, u8(a), "");
#elif defined(__APPLE__)
        if (!h.empty()) scan_emu_base(s, h / "Library" / "Application Support", "");
#else
        // yuzu and forks use XDG_DATA_HOME, Ryujinx XDG_CONFIG_HOME (its ApplicationData folder): look in both.
        std::string data = env("XDG_DATA_HOME"), config = env("XDG_CONFIG_HOME");
        if (data.empty() && !h.empty()) data = utf8(h / ".local" / "share");
        if (config.empty() && !h.empty()) config = utf8(h / ".config");
        if (!data.empty()) scan_emu_base(s, u8(data), "");
        if (!config.empty()) scan_emu_base(s, u8(config), "");
        // Flatpak apps keep theirs in ~/.var/app/<app id>/{data,config}/ (org.yuzu_emu.yuzu, org.ryujinx.Ryujinx,
        // io.github.ryubing.Ryujinx...): every app is checked, so app ids we do not know are found too.
        if (!h.empty())
            for (const fs::path& app : subdirs(h / ".var" / "app")) {
                scan_emu_base(s, app / "data", " (Flatpak)");
                scan_emu_base(s, app / "config", " (Flatpak)");
            }
#endif
        scan_recomps(s);
        for (const fs::path& v : volumes())
            scan_volume(s, v, v.filename().empty() ? utf8(v) : utf8(v.filename()));
        return s.out;
    } catch (...) {
        return {};
    }
}

bool looks_like_acnh_save(const std::string& dir, std::string* why) {
    try {
        fs::path p = u8(dir);
        if (is_file(p)) {
            return save_archive::validate(p, why);
        }
        fs::path root;
        return find_root(p, &root, why);
    } catch (...) {
        set(why, "cannot read this folder");
        return false;
    }
}

bool import_save(const std::string& from, std::string* error) {
    try {
        std::lock_guard lk(g_lock);
        return import_locked(u8(from), error);
    } catch (...) {
        set(error, "unexpected error");
        return false;
    }
}

bool export_save(const std::string& to, std::string* written, std::string* error) {
    try {
        std::lock_guard lk(g_lock);
        recover();
        fs::path src = device_dir();
        if (!flat_save(src, error)) return set(error, "no save to export"), false;
        std::error_code ec;
        fs::path dest = u8(to);
        if (ends_with_zip(to)) {
            if (dest.has_parent_path() && !is_dir(dest.parent_path()))
                return set(error, "folder not found: " + utf8(dest.parent_path())), false;
            fs::path tmp = u8(to + ".tmp");
            if (!save_archive::write(src, tmp, error)) return fs::remove(tmp, ec), false;
            fs::remove(dest, ec);  // rename does not replace an existing file on Windows
            fs::rename(tmp, dest, ec);
            if (ec) return fs::remove(tmp, ec), set(error, "cannot write " + to), false;
        } else {
            fs::create_directories(dest, ec);
            if (!is_dir(dest)) return set(error, "cannot create " + to), false;
            fs::path final_path = unique_in(dest, "ACNH-" + stamp("%Y%m%d-%H%M%S"));
            fs::path tmp = u8(utf8(final_path) + ".partial");
            fs::remove_all(tmp, ec);
            if (!copy_tree(src, tmp, error)) return fs::remove_all(tmp, ec), false;
            fs::rename(tmp, final_path, ec);
            if (ec) return fs::remove_all(tmp, ec), set(error, "cannot finish " + utf8(final_path)), false;
            dest = final_path;
        }
        set(written, utf8(dest));
        log("saves: exported to %s", utf8(dest).c_str());
        return true;
    } catch (...) {
        set(error, "unexpected error");
        return false;
    }
}

std::string backup_now(std::string* error) {
    try {
        std::lock_guard lk(g_lock);
        recover();
        return backup_locked(error);
    } catch (...) {
        set(error, "unexpected error");
        return std::string();
    }
}

std::vector<std::string> list_backups() {
    try {
        std::lock_guard lk(g_lock);
        return backups_sorted();
    } catch (...) {
        return {};
    }
}

bool restore_backup(const std::string& name, std::string* error) {
    try {
        std::lock_guard lk(g_lock);
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos)
            return set(error, "invalid backup name"), false;
        fs::path p = backups_dir() / u8(name);
        if (!is_dir(p)) return set(error, "backup not found: " + name), false;
        // The backup is staged before the current save is backed up, so the pruning that follows that backup cannot
        // delete the one being restored.
        return import_locked(p, error);
    } catch (...) {
        set(error, "unexpected error");
        return false;
    }
}

}  // namespace nx::saves
