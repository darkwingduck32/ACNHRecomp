#include "mod_package.h"
#include "package_sha256.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
namespace nx::mod_package {
namespace fs = std::filesystem;
namespace {
struct Value {
  char type = 0;
  std::string text;
  std::map<std::string, Value> object;
  std::vector<Value> array;
};
// Bounded JSON reader for the package schema. Duplicate keys and ambiguous
// input are rejected.
struct Reader {
  const std::string &s;
  size_t p = 0;
  void ws() {
    while (p < s.size() &&
           (s[p] == ' ' || s[p] == '\r' || s[p] == '\n' || s[p] == '\t'))
      ++p;
  }
  char take() {
    if (p >= s.size())
      throw std::runtime_error("truncated JSON");
    return s[p++];
  }
  std::string string() {
    if (take() != '"')
      throw std::runtime_error("expected JSON string");
    std::string r;
    for (;;) {
      char c = take();
      if (c == '"')
        return r;
      if (static_cast<unsigned char>(c) < 32)
        throw std::runtime_error("control character in JSON");
      if (c == '\\') {
        c = take();
        if (c == 'n')
          c = '\n';
        else if (c == 'r')
          c = '\r';
        else if (c == 't')
          c = '\t';
        else if (c != '"' && c != '\\' && c != '/')
          throw std::runtime_error("unsupported JSON escape (use UTF-8)");
      }
      r += c;
    }
  }
  Value value(unsigned depth = 0) {
    if (depth > 8)
      throw std::runtime_error("JSON nesting limit");
    ws();
    Value v;
    char c = take();
    v.type = c;
    if (c == '"') {
      --p;
      v.text = string();
    } else if (c == '{' || c == '[') {
      ws();
      char end = c == '{' ? '}' : ']';
      if (p < s.size() && s[p] == end) {
        ++p;
        return v;
      }
      for (;;) {
        ws();
        if (c == '{') {
          auto k = string();
          ws();
          if (take() != ':')
            throw std::runtime_error("expected colon");
          if (!v.object.emplace(k, value(depth + 1)).second)
            throw std::runtime_error("duplicate JSON key");
        } else
          v.array.push_back(value(depth + 1));
        ws();
        char next = take();
        if (next == end)
          break;
        if (next != ',')
          throw std::runtime_error("expected comma");
      }
    } else if (c >= '0' && c <= '9') {
      v.type = 'n';
      v.text = c;
      while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p])))
        v.text += s[p++];
    } else
      throw std::runtime_error("unsupported JSON value");
    return v;
  }
};
std::string text(const Value &v) {
  if (v.type != '"')
    throw std::runtime_error("expected string");
  return v.text;
}
std::vector<std::string> strings(const Value &v) {
  if (v.type != '[')
    throw std::runtime_error("expected array");
  std::vector<std::string> r;
  for (auto &a : v.array)
    r.push_back(text(a));
  return r;
}
std::string bytes(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot read package file");
  return {std::istreambuf_iterator<char>(f), {}};
}
bool fail(std::string *e, const std::string &s) {
  if (e)
    *e = s;
  return false;
}
bool id_ok(const std::string &s) {
  return !s.empty() && s.size() < 80 &&
         s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") ==
             std::string::npos;
}
} // namespace
bool safe_path(const std::string &s) {
  if (s.empty() || s.front() == '/' ||
      s.find_first_of("\\:\0", 0, 3) != std::string::npos)
    return false;
  size_t p = 0;
  while (p <= s.size()) {
    auto n = s.find('/', p);
    auto part = s.substr(p, n == s.npos ? s.npos : n - p);
    if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
        part.back() == ' ')
      return false;
    if (n == s.npos)
      break;
    p = n + 1;
  }
  return true;
}
bool read(const fs::path &root, Manifest &out, std::string *error) {
  try {
    auto path = root / "manifest.json";
    if (fs::is_symlink(fs::symlink_status(path)) ||
        fs::file_size(path) > 1024 * 1024)
      return fail(error, "invalid package manifest");
    auto s = bytes(path);
    Reader r{s};
    auto v = r.value();
    r.ws();
    if (r.p != s.size() || v.type != '{')
      throw std::runtime_error("invalid JSON document");
    const auto &o = v.object;
    const std::set<std::string> keys = {"format",  "id",        "version",
                                        "runtime", "build_ids", "conflicts",
                                        "files"};
    for (auto &[k, x] : o)
      if (!keys.count(k))
        throw std::runtime_error("unknown manifest field: " + k);
    if (o.at("format").type != 'n' || o.at("format").text != "1")
      throw std::runtime_error("unsupported package format");
    Manifest m;
    m.id = text(o.at("id"));
    m.version = text(o.at("version"));
    m.runtime = text(o.at("runtime"));
    m.builds = strings(o.at("build_ids"));
    m.conflicts = strings(o.at("conflicts"));
    if (!id_ok(m.id) || m.version.empty() || m.version.size() > 80 ||
        (m.runtime != camera_capability && m.runtime != "romfs.v1") ||
        m.builds.empty())
      throw std::runtime_error(
          "unsupported package identity or runtime capability");
    for (auto &b : m.builds)
      if (b.size() != 32 || b.find_first_not_of("0123456789ABCDEF") != b.npos)
        throw std::runtime_error("invalid build ID");
    if (m.id == "ptbr" || m.id == "speedfix60")
      throw std::runtime_error("native bundled mod IDs are reserved");
    for (auto &id : m.conflicts)
      if (!id_ok(id) || id == "ptbr" || id == "speedfix60")
        throw std::runtime_error(
            "invalid conflict ID (native bundled mods cannot be superseded)");
    auto &files = o.at("files");
    if (files.type != '{' || files.object.empty())
      throw std::runtime_error("empty package inventory");
    std::set<std::string> folded;
    for (auto &[path, x] : files.object) {
      auto sum = text(x);
      auto lower = path;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return char(std::tolower(c)); });
      if (!safe_path(path) || path == "manifest.json" ||
          !folded.insert(lower).second || sum.size() != 64 ||
          sum.find_first_not_of("0123456789abcdef") != sum.npos)
        throw std::runtime_error("invalid file inventory");
      if (path.rfind("romfs/", 0) != 0 && path.rfind("profiles/", 0) != 0 &&
          path != "README.txt")
        throw std::runtime_error("unsupported package payload");
      m.files[path] = sum;
    }
    out = std::move(m);
    return true;
  } catch (const std::exception &e) {
    return fail(error, std::string("Invalid mod package: ") + e.what());
  }
}
std::string build_id(const std::string &root) {
  for (auto name : {"game.ini", "version.ini"}) {
    std::ifstream f(fs::u8path(root) / name);
    for (std::string s; std::getline(f, s);) {
      auto eq = s.find('=');
      if (eq == s.npos)
        continue;
      auto k = s.substr(0, eq);
      k.erase(std::remove_if(k.begin(), k.end(),
                             [](unsigned char c) { return std::isspace(c); }),
              k.end());
      if (k != "build_id")
        continue;
      auto b = s.substr(eq + 1);
      b.erase(std::remove_if(b.begin(), b.end(),
                             [](unsigned char c) { return std::isspace(c); }),
              b.end());
      return b;
    }
  }
  return {};
}
bool validate(const fs::path &root, const std::string &data_root, Manifest *out,
              std::string *error) {
  Manifest m;
  if (!read(root, m, error))
    return false;
  auto build = build_id(data_root);
  if (std::find(m.builds.begin(), m.builds.end(), build) == m.builds.end())
    return fail(error,
                "This package does not support the selected game's build ID.");
  try {
    uint64_t total = 0;
    size_t count = 0;
    for (const auto &e : fs::recursive_directory_iterator(root)) {
      if (e.is_symlink())
        return fail(error, "Package links are not supported.");
      if (e.is_directory())
        continue;
      if (!e.is_regular_file())
        return fail(error, "Unsupported package file type.");
      auto p = e.path().lexically_relative(root).generic_string();
      if (p == "manifest.json")
        continue;
      auto it = m.files.find(p);
      if (it == m.files.end())
        return fail(error, "Unlisted package file: " + p);
      total += e.file_size();
      if (total > (1ull << 30))
        return fail(error, "Package exceeds 1 GiB.");
      if (digest(bytes(e.path())) != it->second)
        return fail(error, "Package checksum mismatch: " + p);
      ++count;
    }
    if (count != m.files.size())
      return fail(error, "Package is missing required files.");
    if (m.runtime == camera_capability) {
      for (auto p :
           {"profiles/view_fog_shaders.txt", "profiles/tree_fruit_offsets.txt",
            "profiles/terrain_scale.txt", "profiles/parameter_edits.txt"})
        if (!m.files.count(p))
          return fail(error, std::string("Missing camera profile: ") + p);
    }
    if (out)
      *out = std::move(m);
    return true;
  } catch (const std::exception &e) {
    return fail(error, std::string("Cannot validate package: ") + e.what());
  }
}
} // namespace nx::mod_package
