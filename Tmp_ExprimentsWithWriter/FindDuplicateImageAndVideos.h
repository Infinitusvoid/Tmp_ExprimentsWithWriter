// media_dupes.cpp
// C++17 tool: find duplicate images/videos AND duplicate folders (by media content).
//
// Folder duplicates definition: two directories are considered duplicates if the
// multiset of all media files under them (recursively) is identical by content
// (we use (size, full 64-bit FNV-1a hash)). File names, timestamps and layout
// do NOT matter for this "as a whole" comparison.
//
// Usage:
//   media_dupes <path> [--csv-files files.csv] [--csv-dirs dirs.csv]
//
// Exit codes: 0 ok, 1 non-fatal issues (some files unreadable), 2 fatal.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

// ----------------------- Lowercase helper -----------------------
static std::string to_lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// ----------------------- Media extensions -----------------------
static const std::set<std::string> kImageExt = {
    ".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tiff", ".tif",
    ".webp", ".heic", ".heif", ".raw", ".cr2", ".nef", ".arw"
};
static const std::set<std::string> kVideoExt = {
    ".mp4", ".m4v", ".mov", ".avi", ".mkv", ".webm", ".wmv",
    ".mpeg", ".mpg", ".mpe", ".mts", ".m2ts", ".3gp", ".flv", ".ogv"
};

static bool is_media_file(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    auto ext = to_lower(p.extension().string());
    return kImageExt.count(ext) || kVideoExt.count(ext);
}

// ----------------------- Human readable size -----------------------
static std::string human_size(std::uintmax_t bytes) {
    const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(v < 10 ? 2 : (v < 100 ? 1 : 0)) << v << " " << units[u];
    return oss.str();
}

// ----------------------- FNV-1a 64-bit -----------------------
struct FNV1a64 {
    static constexpr std::uint64_t offset = 1469598103934665603ULL;
    static constexpr std::uint64_t prime = 1099511628211ULL;

    std::uint64_t h = offset;

    void update(const unsigned char* data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            h ^= static_cast<std::uint64_t>(data[i]);
            h *= prime;
        }
    }
    void update_u64(std::uint64_t v) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
        update(b, 8);
    }
    std::uint64_t digest() const { return h; }
};

// ----------------------- File hashing -----------------------
static bool hash_file_full(const fs::path& p, std::uint64_t& out, std::string& err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { err = "cannot open"; return false; }
    FNV1a64 H;
    std::vector<unsigned char> buf(1 << 20); // 1 MiB
    while (true) {
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        std::streamsize n = f.gcount();
        if (n > 0) H.update(buf.data(), static_cast<std::size_t>(n));
        if (!f) {
            if (f.eof()) break;
            err = "read error";
            return false;
        }
    }
    out = H.digest();
    return true;
}

static bool files_equal(const fs::path& a, const fs::path& b, std::string& err) {
    std::error_code ec;
    auto sa = fs::file_size(a, ec); if (ec) { err = "filesize error A"; return false; }
    auto sb = fs::file_size(b, ec); if (ec) { err = "filesize error B"; return false; }
    if (sa != sb) return false;

    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    if (!fa || !fb) { err = "open error"; return false; }

    const std::size_t BUFSZ = 1 << 20;
    std::vector<unsigned char> ba(BUFSZ), bb(BUFSZ);
    while (true) {
        fa.read(reinterpret_cast<char*>(ba.data()), static_cast<std::streamsize>(BUFSZ));
        fb.read(reinterpret_cast<char*>(bb.data()), static_cast<std::streamsize>(BUFSZ));
        std::streamsize na = fa.gcount(), nb = fb.gcount();
        if (na != nb) return false;
        if (na == 0) break;
        if (std::memcmp(ba.data(), bb.data(), static_cast<std::size_t>(na)) != 0) return false;
        if (!fa && !fa.eof()) { err = "read error A"; return false; }
        if (!fb && !fb.eof()) { err = "read error B"; return false; }
    }
    return true;
}

// ----------------------- Subpath check -----------------------
static bool is_subpath_of(const fs::path& base, const fs::path& p) {
    auto B = base.lexically_normal();
    auto P = p.lexically_normal();
    auto bit = B.begin(), pit = P.begin();
    for (; bit != B.end() && pit != P.end(); ++bit, ++pit) {
        if (*bit != *pit) return false;
    }
    return bit == B.end();
}

// ----------------------- Data structures -----------------------
struct ErrorNote { fs::path path; std::string what; };

struct FileRec {
    fs::path path;
    std::uintmax_t size = 0;
    std::uint64_t  hash = 0;
    bool           ok = false;
};

struct DirStats {
    std::vector<std::pair<std::uintmax_t, std::uint64_t>> items; // (size, hash) multiset
    std::uintmax_t total_bytes = 0;
    std::size_t    file_count = 0;
    std::uint64_t  digest = 0; // computed from items (order-independent)
};

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cerr << "Usage: media_dupes <path> [--csv-files files.csv] [--csv-dirs dirs.csv]\n";
            return 2;
        }

        fs::path input = argv[1];
        std::string csv_files, csv_dirs;

        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--csv-files" && i + 1 < argc)      csv_files = argv[++i];
            else if (a == "--csv-dirs" && i + 1 < argc)  csv_dirs = argv[++i];
            else {
                std::cerr << "Unknown/invalid option: " << a << "\n";
                return 2;
            }
        }

        std::error_code ec;
        if (!fs::exists(input, ec)) {
            std::cerr << "Path does not exist: " << input << "\n";
            return 2;
        }

        // If a single file is passed, scan its parent directory.
        fs::path root = fs::is_directory(input, ec) ? input : input.parent_path();
        if (root.empty()) root = fs::current_path();

        root = root.lexically_normal();

        // 1) Gather all media files under root and compute full hashes.
        std::vector<ErrorNote> errors;
        std::vector<FileRec> files;
        files.reserve(1024);

        fs::recursive_directory_iterator it(root,
            fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            std::cerr << "Failed to iterate: " << ec.message() << "\n";
            return 2;
        }

        for (const auto& entry : it) {
            std::error_code tec;
            if (!entry.is_regular_file(tec)) continue;
            const auto& p = entry.path();
            if (!is_media_file(p)) continue;

            FileRec fr;
            fr.path = p;
            fr.size = entry.file_size(tec);
            if (tec) {
                errors.push_back({ p, "filesize error" });
                continue;
            }
            std::string herr;
            fr.ok = hash_file_full(p, fr.hash, herr);
            if (!fr.ok) errors.push_back({ p, "hash: " + herr });
            files.push_back(std::move(fr));
        }

        if (files.empty()) {
            std::cout << "No media files found under: " << root << "\n";
            return errors.empty() ? 0 : 1;
        }

        // 2) FILE DUPLICATES: group by (size, hash), and confirm by byte-compare.
        using Key = std::pair<std::uintmax_t, std::uint64_t>;
        struct KeyHash {
            std::size_t operator()(const Key& k) const noexcept {
                return std::hash<std::uint64_t>{}((k.first << 13) ^ k.second);
            }
        };

        std::unordered_map<Key, std::vector<const FileRec*>, KeyHash> fileBuckets;
        for (const auto& fr : files) {
            if (!fr.ok) continue;
            fileBuckets[{fr.size, fr.hash}].push_back(&fr);
        }

        struct FileGroup { std::uintmax_t size; std::vector<fs::path> paths; };
        std::vector<FileGroup> fileGroups;

        for (auto& kv : fileBuckets) {
            auto& vec = kv.second;
            if (vec.size() < 2) continue;
            // extra-safe: confirm via byte-compare against first
            std::vector<fs::path> confirmed;
            confirmed.push_back(vec.front()->path);
            for (size_t i = 1; i < vec.size(); ++i) {
                std::string err;
                if (files_equal(vec[i]->path, vec.front()->path, err)) {
                    confirmed.push_back(vec[i]->path);
                }
                else if (!err.empty()) {
                    errors.push_back({ vec[i]->path, "compare: " + err });
                }
            }
            if (confirmed.size() >= 2) {
                std::sort(confirmed.begin(), confirmed.end());
                fileGroups.push_back(FileGroup{ kv.first.first, std::move(confirmed) });
            }
        }

        // 3) DIRECTORY DUPLICATES: build a multiset (size,hash) for each directory.
        // For each file, add its (size,hash) to every ancestor directory within root.
        std::map<fs::path, DirStats> dirStats; // ordered for pretty output

        for (const auto& fr : files) {
            if (!fr.ok) continue;
            fs::path dir = fr.path.parent_path().lexically_normal();
            while (!dir.empty() && is_subpath_of(root, dir)) {
                auto& ds = dirStats[dir];
                ds.items.emplace_back(fr.size, fr.hash);
                ds.total_bytes += fr.size;
                ds.file_count += 1;
                if (dir == dir.root_path()) break; // safety (shouldn’t happen within root)
                dir = dir.parent_path().lexically_normal();
            }
        }

        // Compute digest per directory: order-independent by sorting items first.
        for (auto& kv : dirStats) {
            auto& ds = kv.second;
            if (ds.file_count == 0) continue;
            std::sort(ds.items.begin(), ds.items.end(),
                [](auto& a, auto& b) {
                    if (a.first != b.first) return a.first < b.first;
                    return a.second < b.second;
                });
            FNV1a64 H;
            H.update_u64(static_cast<std::uint64_t>(ds.file_count));
            H.update_u64(static_cast<std::uint64_t>(ds.total_bytes));
            for (auto& pr : ds.items) {
                H.update_u64(static_cast<std::uint64_t>(pr.first));
                H.update_u64(pr.second);
            }
            ds.digest = H.digest();
        }

        // Group directories by digest (only those with at least one media file).
        std::unordered_map<std::uint64_t, std::vector<fs::path>> dirBuckets;
        for (auto& kv : dirStats) {
            const auto& dir = kv.first;
            const auto& ds = kv.second;
            if (ds.file_count == 0) continue;
            dirBuckets[ds.digest].push_back(dir);
        }

        struct DirGroup {
            std::size_t file_count = 0;
            std::uintmax_t total_bytes = 0;
            std::vector<fs::path> dirs;
        };
        std::vector<DirGroup> dirGroups;

        for (auto& kv : dirBuckets) {
            auto& list = kv.second;
            if (list.size() < 2) continue; // not duplicates
            // Choose reference directory to verify equality of content multisets.
            // (Digest already tells us they match; this step is belts-and-suspenders.)
            // We’ll trust digest here to avoid O(N^2) comparisons on large sets.
            // If you want absolute confirmation, you could compare 'items' vectors.
            // Build group meta from the first directory’s stats:
            auto& ds0 = dirStats[list.front()];
            DirGroup g;
            g.file_count = ds0.file_count;
            g.total_bytes = ds0.total_bytes;
            std::sort(list.begin(), list.end());
            g.dirs = std::move(list);
            dirGroups.push_back(std::move(g));
        }

        // 4) Pretty print results
        std::cout << "=== Media duplicates report ===\n";
        std::cout << "Root: " << root << "\n";
        std::cout << "\n";

        if (fileGroups.empty()) {
            std::cout << "[Files] No duplicate media files.\n\n";
        }
        else {
            // sort larger groups first
            std::sort(fileGroups.begin(), fileGroups.end(),
                [](const FileGroup& a, const FileGroup& b) {
                    if (a.paths.size() != b.paths.size()) return a.paths.size() > b.paths.size();
                    if (a.size != b.size) return a.size > b.size;
                    return a.paths.front() < b.paths.front();
                });
            std::cout << "[Files] Duplicate groups: " << fileGroups.size() << "\n\n";
            std::size_t gid = 0;
            for (const auto& g : fileGroups) {
                std::cout << "File Group " << (++gid) << " • "
                    << human_size(g.size) << " (" << g.size << " bytes)"
                    << " • count=" << g.paths.size() << "\n";
                for (const auto& p : g.paths) {
                    std::cout << "  - " << p.string() << "\n";
                }
                std::cout << "\n";
            }
        }

        if (dirGroups.empty()) {
            std::cout << "[Folders] No duplicate folders (by media content).\n\n";
        }
        else {
            // sort by total bytes then by file count
            std::sort(dirGroups.begin(), dirGroups.end(),
                [](const DirGroup& a, const DirGroup& b) {
                    if (a.total_bytes != b.total_bytes) return a.total_bytes > b.total_bytes;
                    if (a.file_count != b.file_count)  return a.file_count > b.file_count;
                    return a.dirs.front() < b.dirs.front();
                });
            std::cout << "[Folders] Duplicate groups: " << dirGroups.size() << "\n\n";
            std::size_t gid = 0;
            for (const auto& g : dirGroups) {
                std::cout << "Folder Group " << (++gid) << " • files=" << g.file_count
                    << " • total=" << human_size(g.total_bytes) << " (" << g.total_bytes << " bytes)\n";
                for (const auto& d : g.dirs) {
                    std::cout << "  - " << d.string() << "\n";
                }
                std::cout << "\n";
            }
        }

        // 5) CSVs (optional)
        if (!csv_files.empty()) {
            std::ofstream f(csv_files);
            if (!f) {
                std::cerr << "Failed to write file CSV: " << csv_files << "\n";
            }
            else {
                f << "group_id,file_size_bytes,file_path\n";
                std::size_t gid = 0;
                for (const auto& g : fileGroups) {
                    ++gid;
                    for (const auto& p : g.paths) {
                        f << gid << "," << g.size << ",\"" << p.string() << "\"\n";
                    }
                }
                std::cout << "File CSV saved: " << csv_files << "\n";
            }
        }

        if (!csv_dirs.empty()) {
            std::ofstream f(csv_dirs);
            if (!f) {
                std::cerr << "Failed to write dir CSV: " << csv_dirs << "\n";
            }
            else {
                f << "group_id,files_count,total_bytes,dir_path\n";
                std::size_t gid = 0;
                for (const auto& g : dirGroups) {
                    ++gid;
                    for (const auto& d : g.dirs) {
                        f << gid << "," << g.file_count << "," << g.total_bytes
                            << ",\"" << d.string() << "\"\n";
                    }
                }
                std::cout << "Dir CSV saved: " << csv_dirs << "\n";
            }
        }

        // 6) Error notes
        if (!errors.empty()) {
            std::cout << "Notes (" << errors.size() << "):\n";
            for (const auto& e : errors) {
                std::cout << "  * " << e.path.string() << " — " << e.what << "\n";
            }
            return 1; // non-fatal issues occurred
        }

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 2;
    }
}
