#pragma once

// dedupe_web.cpp
// Tiny C++17 web app (no external libs) that scans a filesystem subtree,
// finds duplicate image/video files AND duplicate folders (entire subtrees),
// and serves results as HTML.
//
// Endpoints:
//   GET  /        -> UI form
//   POST /scan    -> run scan for "path=..." (application/x-www-form-urlencoded)
//
// Notes:
// - Cross-platform sockets: Winsock on Windows, POSIX elsewhere.
// - Directory-duplicate criterion: recursive *media* files (images/videos) with
//   identical relative paths AND identical contents (size+hash), i.e. same structure.
// - File duplicates: confirmed by byte-for-byte compare after hashing.
// - Quick fingerprint phase to prune (first+last 64KiB + size) before full hash.
//
// Build:
//   Linux/macOS: g++ -std=gnu++17 -O2 -pthread -o dedupe_web dedupe_web.cpp
//   Windows    : cl /std:c++17 /O2 /EHsc dedupe_web.cpp ws2_32.lib

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX  // stop windows.h from defining min/max macros
#endif



#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
using SOCKET = int;
#endif

// ------------ Utility: lowercase, html escape, url decode, hex ------------
static std::string to_lower(std::string s) {
    for (auto& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

static std::string html_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() * 1.1);
    for (unsigned char c : in) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default:
            if (c < 32) { out += "&#"; out += std::to_string((int)c); out += ';'; }
            else out += static_cast<char>(c);
        }
    }
    return out;
}

static int from_hex(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return 10 + (c - 'a');
    if ('A' <= c && c <= 'F') return 10 + (c - 'A');
    return -1;
}
static std::string url_decode(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') o.push_back(' ');
        else if (s[i] == '%' && i + 2 < s.size()) {
            int hi = from_hex(s[i + 1]), lo = from_hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) { o.push_back(static_cast<char>((hi << 4) | lo)); i += 2; }
            else o.push_back(s[i]);
        }
        else o.push_back(s[i]);
    }
    return o;
}
static std::string to_hex64(std::uint64_t v) {
    std::ostringstream oss; oss << std::hex << std::setw(16) << std::setfill('0') << v;
    return oss.str();
}

static std::string human_size(std::uintmax_t bytes) {
    const char* units[] = { "B","KiB","MiB","GiB","TiB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(v < 10 ? 2 : (v < 100 ? 1 : 0)) << v << ' ' << units[u];
    return oss.str();
}

// ------------ Media extension filters ------------
static const std::set<std::string> kImageExt = {
    ".jpg",".jpeg",".png",".bmp",".gif",".tiff",".tif",".webp",".heic",".heif",".raw",".cr2",".nef",".arw"
};
static const std::set<std::string> kVideoExt = {
    ".mp4",".m4v",".mov",".avi",".mkv",".webm",".wmv",".mpeg",".mpg",".mpe",".mts",".m2ts",".3gp",".flv",".ogv"
};
static bool is_media_file(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    auto ext = to_lower(p.extension().string());
    return kImageExt.count(ext) || kVideoExt.count(ext);
}

// ------------ FNV-1a 64 ------------
struct FNV1a64 {
    static constexpr std::uint64_t offset = 1469598103934665603ULL;
    static constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t h = offset;
    void update(const unsigned char* d, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) { h ^= (std::uint64_t)d[i]; h *= prime; }
    }
    void update_u64(std::uint64_t v) {
        unsigned char b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
        update(b, 8);
    }
    std::uint64_t digest() const { return h; }
};

// ------------ File hashing ------------
static bool hash_file_full(const fs::path& p, std::uint64_t& out, std::string& err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { err = "cannot open"; return false; }
    FNV1a64 H;
    std::vector<unsigned char> buf(1 << 20); // 1 MiB
    while (true) {
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        std::streamsize n = f.gcount();
        if (n > 0) H.update(buf.data(), (std::size_t)n);
        if (!f) { if (f.eof()) break; err = "read error"; return false; }
    }
    out = H.digest();
    return true;
}

static bool hash_file_quick(const fs::path& p, std::uint64_t& out, std::string& err) {
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    if (ec) { err = "filesize error"; return false; }
    const std::size_t CHUNK = 64 * 1024;

    std::ifstream f(p, std::ios::binary);
    if (!f) { err = "cannot open"; return false; }

    FNV1a64 H; H.update_u64((std::uint64_t)sz);

    std::vector<unsigned char> buf((std::size_t)std::min<std::uintmax_t>(CHUNK, sz));
    if (!buf.empty()) {
        f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)buf.size());
        std::streamsize n = f.gcount();
        if (n > 0) H.update(buf.data(), (std::size_t)n);
        if (!f && !f.eof()) { err = "read head"; return false; }
    }

    if (sz > CHUNK) {
        buf.resize(CHUNK);
        f.clear(); f.seekg((std::streamoff)(sz - CHUNK), std::ios::beg);
        if (!f) { err = "seek tail"; return false; }
        f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)buf.size());
        std::streamsize n = f.gcount();
        if (n > 0) H.update(buf.data(), (std::size_t)n);
        if (!f && !f.eof()) { err = "read tail"; return false; }
    }
    out = H.digest();
    return true;
}

static bool files_equal(const fs::path& a, const fs::path& b, std::string& err) {
    std::error_code ec;
    auto sa = fs::file_size(a, ec); if (ec) { err = "filesize A"; return false; }
    auto sb = fs::file_size(b, ec); if (ec) { err = "filesize B"; return false; }
    if (sa != sb) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) { err = "open"; return false; }
    const std::size_t BUFSZ = 1 << 20;
    std::vector<unsigned char> ba(BUFSZ), bb(BUFSZ);
    while (true) {
        fa.read(reinterpret_cast<char*>(ba.data()), (std::streamsize)BUFSZ);
        fb.read(reinterpret_cast<char*>(bb.data()), (std::streamsize)BUFSZ);
        std::streamsize na = fa.gcount(), nb = fb.gcount();
        if (na != nb) return false;
        if (na == 0) break;
        if (std::memcmp(ba.data(), bb.data(), (std::size_t)na) != 0) return false;
        if (!fa && !fa.eof()) { err = "read A"; return false; }
        if (!fb && !fb.eof()) { err = "read B"; return false; }
    }
    return true;
}

// ------------ Core scanning types ------------
struct ErrorNote { fs::path path; std::string what; };

struct FileInfo {
    fs::path path;
    std::uintmax_t size = 0;
    std::uint64_t full_hash = 0;  // filled later
};

struct FileGroup { // confirmed duplicate files (>=2)
    std::uintmax_t size = 0;
    std::vector<fs::path> paths;
};

struct DirGroup { // duplicate directories (>=2)
    std::size_t file_count = 0;
    std::uint64_t dir_sig = 0;
    std::vector<fs::path> dirs;
};

// Compute duplicates and directory signatures
struct ScanResult {
    std::vector<FileGroup> file_groups;
    std::vector<DirGroup>  dir_groups;
    std::vector<ErrorNote> errors;
    std::size_t candidate_files = 0;
    std::size_t scanned_files = 0;
    std::chrono::milliseconds elapsed{ 0 };
};

static ScanResult run_scan(const fs::path& root) {
    auto t0 = std::chrono::steady_clock::now();
    ScanResult R;

    // 1) Collect media files
    std::map<std::uintmax_t, std::vector<fs::path>> by_size;
    {
        std::error_code ec;
        if (!fs::exists(root, ec)) {
            R.errors.push_back({ root, "root missing" });
            return R;
        }
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec) { R.errors.push_back({ root, "cannot iterate: " + ec.message() }); return R; }

        for (const auto& entry : it) {
            std::error_code tec;
            if (entry.is_regular_file(tec) && is_media_file(entry.path())) {
                auto sz = entry.file_size(tec);
                if (tec) {
                    R.errors.push_back({ entry.path(), "filesize error" });
                    continue;
                }
                by_size[sz].push_back(entry.path());
            }
        }
    }

    // Remove singletons by size
    for (auto it = by_size.begin(); it != by_size.end(); ) {
        if (it->second.size() < 1) it = by_size.erase(it);
        else ++it;
    }
    for (auto& kv : by_size) R.candidate_files += kv.second.size();

    // 2) Quick fingerprint -> Full hash -> Confirm file groups
    // Also collect per-file full hashes for directory signatures later.
    std::unordered_map<fs::path, FileInfo> file_infos;
    file_infos.reserve(R.candidate_files * 2);

    std::vector<FileGroup> file_groups;

    for (const auto& [sz, paths] : by_size) {
        // Quick hash bucketing
        std::unordered_map<std::uint64_t, std::vector<fs::path>> by_quick;
        for (const auto& p : paths) {
            std::uint64_t qh = 0; std::string err;
            if (!hash_file_quick(p, qh, err)) {
                R.errors.push_back({ p, "quick hash: " + err });
                continue;
            }
            by_quick[qh].push_back(p);
        }

        // Full hash
        std::unordered_map<std::uint64_t, std::vector<fs::path>> by_full;
        for (auto& kv : by_quick) {
            if (kv.second.size() < 1) continue;
            for (const auto& p : kv.second) {
                std::uint64_t fh = 0; std::string err;
                if (!hash_file_full(p, fh, err)) {
                    R.errors.push_back({ p, "full hash: " + err });
                    continue;
                }
                file_infos[p] = FileInfo{ p, sz, fh };
                ++R.scanned_files;
                by_full[fh].push_back(p);
            }
        }

        // Confirm with byte-compare and create groups
        for (auto& ff : by_full) {
            auto& vec = ff.second;
            if (vec.size() < 2) continue;

            std::vector<std::vector<fs::path>> classes;
            for (const auto& p : vec) {
                bool placed = false;
                for (auto& cls : classes) {
                    std::string err;
                    if (files_equal(p, cls.front(), err)) { cls.push_back(p); placed = true; break; }
                    else if (!err.empty()) R.errors.push_back({ p, "compare: " + err });
                }
                if (!placed) classes.push_back({ p });
            }
            for (auto& cls : classes) {
                if (cls.size() >= 2) file_groups.push_back(FileGroup{ sz, std::move(cls) });
            }
        }
    }

    // 3) Directory signatures (recursive, media files only, include relative paths)
    // Build per-directory listing of its recursive files quickly:
    // We'll walk all files once and push their (dir root candidates) on a parent-chain.
    // Implementation: simpler approach  enumerate all directories, then for each, gather files under it.
    // (OK for typical trees; easy to reason about.)
    std::vector<fs::path> all_dirs;
    {
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (!ec) {
            for (const auto& e : it) {
                std::error_code dec;
                if (e.is_directory(dec)) all_dirs.push_back(e.path());
            }
            // also include the root itself if it is a directory
            if (fs::is_directory(root, ec)) all_dirs.push_back(root);
        }
    }

    // compute per-directory signature
    std::unordered_map<std::uint64_t, std::vector<fs::path>> dir_buckets;
    for (const auto& dir : all_dirs) {
        // Gather media files under this directory (recursive)
        std::vector<std::tuple<std::string, std::uintmax_t, std::uint64_t>> entries; // (relpath,size,fullhash)
        std::error_code ec;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;

        for (const auto& e : it) {
            std::error_code fec;
            if (e.is_regular_file(fec) && is_media_file(e.path())) {
                fs::path p = e.path();
                // full hash (compute if missing; reuse file_infos if present)
                auto itfi = file_infos.find(p);
                std::uint64_t fh = 0;
                std::uintmax_t sz = 0;

                if (itfi != file_infos.end()) {
                    fh = itfi->second.full_hash; sz = itfi->second.size;
                }
                else {
                    sz = e.file_size(fec);
                    if (fec) { R.errors.push_back({ p, "fs size for dir sig" }); continue; }
                    std::string err;
                    if (!hash_file_full(p, fh, err)) { R.errors.push_back({ p, "dir sig full hash: " + err }); continue; }
                }
                fs::path rel = fs::relative(p, dir, ec);
                std::string relnorm = rel.generic_string(); // forward slashes
                entries.emplace_back(relnorm, sz, fh);
            }
        }

        if (entries.empty()) continue; // ignore empty-media dirs

        std::sort(entries.begin(), entries.end(), [](auto& A, auto& B) {
            if (std::get<0>(A) != std::get<0>(B)) return std::get<0>(A) < std::get<0>(B);
            if (std::get<1>(A) != std::get<1>(B)) return std::get<1>(A) < std::get<1>(B);
            return std::get<2>(A) < std::get<2>(B);
            });

        FNV1a64 H;
        for (auto& t : entries) {
            const std::string& rel = std::get<0>(t);
            const auto& sz = std::get<1>(t);
            const auto& fh = std::get<2>(t);
            H.update(reinterpret_cast<const unsigned char*>(rel.data()), rel.size());
            H.update_u64((std::uint64_t)sz);
            H.update_u64(fh);
        }
        std::uint64_t sig = H.digest();
        dir_buckets[sig].push_back(dir);
    }

    std::vector<DirGroup> dir_groups;
    for (auto& kv : dir_buckets) {
        if (kv.second.size() < 2) continue;
        // build a representative file count for UI (grab from first dir)
        std::size_t count = 0;
        {
            std::error_code ec;
            fs::recursive_directory_iterator it(kv.second.front(), fs::directory_options::skip_permission_denied, ec);
            if (!ec) {
                for (const auto& e : it) {
                    std::error_code fec;
                    if (e.is_regular_file(fec) && is_media_file(e.path())) ++count;
                }
            }
        }
        dir_groups.push_back(DirGroup{ count, kv.first, std::move(kv.second) });
    }

    // sort groups (files by size desc, dirs by count desc)
    std::sort(file_groups.begin(), file_groups.end(),
        [](const FileGroup& a, const FileGroup& b) {
            if (a.size != b.size) return a.size > b.size;
            return a.paths.size() > b.paths.size();
        });
    std::sort(dir_groups.begin(), dir_groups.end(),
        [](const DirGroup& a, const DirGroup& b) {
            if (a.file_count != b.file_count) return a.file_count > b.file_count;
            return a.dirs.size() > b.dirs.size();
        });

    R.file_groups = std::move(file_groups);
    R.dir_groups = std::move(dir_groups);
    auto t1 = std::chrono::steady_clock::now();
    R.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);
    return R;
}

// ------------ HTTP helpers ------------
struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

static bool recv_all(SOCKET s, std::string& out, size_t want) {
    out.clear(); out.reserve(want);
    while (out.size() < want) {
        char buf[4096];
        size_t need = std::min(want - out.size(), sizeof(buf));
#ifdef _WIN32
        int n = ::recv(s, buf, (int)need, 0);
#else
        ssize_t n = ::recv(s, buf, need, 0);
#endif
        if (n <= 0) return false;
        out.append(buf, buf + n);
    }
    return true;
}

static bool read_request(SOCKET s, HttpRequest& req) {
    // Read until we have headers (CRLF CRLF)
    std::string data;
    data.reserve(8192);
    char buf[4096];
    while (true) {
#ifdef _WIN32
        int n = ::recv(s, buf, (int)sizeof(buf), 0);
#else
        ssize_t n = ::recv(s, buf, sizeof(buf), 0);
#endif
        if (n <= 0) return false;
        data.append(buf, buf + n);
        auto pos = data.find("\r\n\r\n");
        if (pos != std::string::npos) {
            // parse start line + headers
            std::istringstream iss(data.substr(0, pos + 2)); // include last CRLF to ease getline
            std::string line;
            if (!std::getline(iss, line)) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();

            {
                std::istringstream sl(line);
                sl >> req.method >> req.path >> req.version;
                if (req.method.empty()) return false;
            }
            while (std::getline(iss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) break;
                auto kpos = line.find(':');
                if (kpos != std::string::npos) {
                    std::string k = line.substr(0, kpos);
                    std::string v = line.substr(kpos + 1);
                    // trim space
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                    req.headers[to_lower(k)] = v;
                }
            }

            // any bytes after headers are part of body already received
            std::string rest = data.substr(pos + 4);
            auto it = req.headers.find("content-length");
            size_t content_len = 0;
            if (it != req.headers.end()) content_len = (size_t)std::stoull(it->second);

            if (rest.size() >= content_len) {
                req.body = rest.substr(0, content_len);
            }
            else {
                std::string more;
                if (!recv_all(s, more, content_len - rest.size())) return false;
                req.body = rest + more;
            }
            return true;
        }
        if (data.size() > (1 << 20)) return false; // guard: headers too big
    }
}

static void send_all(SOCKET s, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
#ifdef _WIN32
        int n = ::send(s, data.data() + off, (int)(data.size() - off), 0);
#else
        ssize_t n = ::send(s, data.data() + off, data.size() - off, 0);
#endif
        if (n <= 0) break;
        off += (size_t)n;
    }
}

static void send_http_response(SOCKET s, const std::string& body, const std::string& content_type = "text/html; charset=utf-8", int status = 200, const char* message = "OK") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << ' ' << message << "\r\n";
    oss << "Content-Type: " << content_type << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n\r\n";
    oss << body;
    send_all(s, oss.str());
}

// ------------ HTML rendering ------------
static std::string render_home(const std::string& msg = "") {
    std::ostringstream h;
    h << "<!doctype html><html><head><meta charset='utf-8'>"
        << "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        << "<title>Duplicate Media Finder</title>"
        << "<style>"
        << "body{font:16px/1.4 system-ui,Segoe UI,Roboto,Arial,sans-serif;max-width:1100px;margin:2rem auto;padding:0 1rem;color:#eee;background:#0b0d12}"
        << "h1{font-weight:700;letter-spacing:.3px}"
        << ".card{background:#141823;border:1px solid #272c3a;border-radius:14px;padding:16px;margin:16px 0;box-shadow:0 2px 10px rgba(0,0,0,.3)}"
        << "label{display:block;margin:.4rem 0 .2rem;color:#b8c0d4}"
        << "input[type=text]{width:100%;padding:.6rem .8rem;border-radius:10px;border:1px solid #2a3144;background:#0f1320;color:#e8eefc}"
        << "button{cursor:pointer;padding:.6rem 1rem;border-radius:10px;border:1px solid #3b4258;background:#2a3144;color:#e8eefc}"
        << "button:hover{filter:brightness(1.1)}"
        << "details{margin:.3rem 0}"
        << "summary{cursor:pointer;color:#b9d0ff}"
        << "code{background:#0f1320;padding:.1rem .25rem;border-radius:6px;border:1px solid #272c3a}"
        << ".muted{color:#8a93a8}"
        << ".group{border-left:3px solid #3b78ff;padding-left:10px;margin:10px 0}"
        << ".cnt{display:inline-block;background:#1b2233;border:1px solid #2d3650;padding:.1rem .4rem;border-radius:8px;margin-left:.4rem;font-size:.85em;color:#b8c0d4}"
        << "</style></head><body>";
    h << "<h1>Duplicate Media Finder</h1>";
    h << "<div class='card'><form method='POST' action='/scan'>"
        << "<label for='path'>Root folder to scan</label>"
        << "<input id='path' name='path' type='text' placeholder='e.g. C:\\\\Media or /home/me/Videos' required>"
        << "<div style='margin-top:12px'><button type='submit'>Scan</button>"
        << " <span class='muted'>Scans images & videos. Folders are compared by recursive media content & structure.</span></div>"
        << "</form></div>";
    if (!msg.empty()) {
        h << "<div class='card'><b>Status:</b> " << html_escape(msg) << "</div>";
    }
    h << "<div class='muted'>Runs locally on 127.0.0.1:8080  No files are modified.</div>";
    h << "</body></html>";
    return h.str();
}

static std::string render_results(const fs::path& root, const ScanResult& R) {
    std::ostringstream h;
    h << "<!doctype html><html><head><meta charset='utf-8'>"
        << "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        << "<title>Scan Results</title>"
        << "<style>"
        << "body{font:16px/1.4 system-ui,Segoe UI,Roboto,Arial,sans-serif;max-width:1100px;margin:2rem auto;padding:0 1rem;color:#eee;background:#0b0d12}"
        << ".card{background:#141823;border:1px solid #272c3a;border-radius:14px;padding:16px;margin:16px 0;box-shadow:0 2px 10px rgba(0,0,0,.3)}"
        << "h1,h2{margin:.2rem 0 .6rem 0}"
        << "code{background:#0f1320;padding:.1rem .25rem;border-radius:6px;border:1px solid #272c3a}"
        << "details{margin:.5rem 0}"
        << "summary{cursor:pointer;color:#b9d0ff}"
        << ".muted{color:#8a93a8}"
        << ".group{border-left:3px solid #3b78ff;padding-left:10px;margin:10px 0}"
        << ".cnt{display:inline-block;background:#1b2233;border:1px solid #2d3650;padding:.1rem .4rem;border-radius:8px;margin-left:.4rem;font-size:.85em;color:#b8c0d4}"
        << "a.btn{display:inline-block;margin-right:.6rem;color:#e8eefc;text-decoration:none;border:1px solid #3b4258;background:#2a3144;padding:.4rem .7rem;border-radius:10px}"
        << "a.btn:hover{filter:brightness(1.1)}"
        << "</style></head><body>";

    h << "<div class='card'><h1>Scan Results</h1>"
        << "<div><b>Root:</b> <code>" << html_escape(root.string()) << "</code></div>"
        << "<div><b>Elapsed:</b> " << (R.elapsed.count() / 1000.0) << "s</div>"
        << "<div><b>Candidate files:</b> " << R.candidate_files
        << " &nbsp; <b>Hashed:</b> " << R.scanned_files << "</div>"
        << "<div style='margin-top:8px'><a class='btn' href='/'>New scan</a></div>"
        << "</div>";

    // Directory groups
    h << "<div class='card'><h2>Duplicate Folders</h2>";
    if (R.dir_groups.empty()) {
        h << "<div class='muted'>None found.</div>";
    }
    else {
        std::size_t gid = 0;
        for (const auto& G : R.dir_groups) {
            ++gid;
            h << "<div class='group'><b>Group " << gid << "</b>"
                << " <span class='cnt'>files: " << G.file_count << "</span>"
                << " <span class='cnt'>dirs: " << G.dirs.size() << "</span>"
                << " <span class='cnt'>sig: 0x" << to_hex64(G.dir_sig) << "</span>";
            for (const auto& d : G.dirs) {
                h << "<div> <code>" << html_escape(d.string()) << "</code></div>";
            }
            h << "</div>";
        }
    }
    h << "</div>";

    // File groups
    h << "<div class='card'><h2>Duplicate Files</h2>";
    if (R.file_groups.empty()) {
        h << "<div class='muted'>None found.</div>";
    }
    else {
        std::size_t gid = 0;
        for (const auto& G : R.file_groups) {
            ++gid;
            h << "<details class='group'><summary><b>Group " << gid << "</b> "
                << "<span class='cnt'>" << G.paths.size() << " files</span> "
                << "<span class='cnt'>" << human_size(G.size) << "</span></summary>";
            for (const auto& p : G.paths) {
                h << "<div> <code>" << html_escape(p.string()) << "</code></div>";
            }
            h << "</details>";
        }
    }
    h << "</div>";

    if (!R.errors.empty()) {
        h << "<div class='card'><h2>Notes</h2>";
        for (const auto& e : R.errors) {
            h << "<div> <code>" << html_escape(e.path.string()) << "</code>  "
                << html_escape(e.what) << "</div>";
        }
        h << "</div>";
    }

    h << "<div class='muted'>No files were changed. Folder duplicates require identical structure and media content.</div>";
    h << "</body></html>";
    return h.str();
}

// ------------ Server loop ------------
static void handle_client(SOCKET cs) {
    HttpRequest req;
    if (!read_request(cs, req)) {
        send_http_response(cs, "<h1>400 Bad Request</h1>", "text/html; charset=utf-8", 400, "Bad Request");
#ifdef _WIN32
        closesocket(cs);
#else
        close(cs);
#endif
        return;
    }

    if (req.method == "GET" && req.path == "/") {
        auto body = render_home();
        send_http_response(cs, body);
    }
    else if (req.method == "POST" && req.path == "/scan") {
        // parse x-www-form-urlencoded
        std::string path;
        {
            // naive form parser (path=...)
            auto kvpos = req.body.find("path=");
            if (kvpos != std::string::npos) {
                std::string v = req.body.substr(kvpos + 5);
                // if multiple fields, cut at &
                auto amp = v.find('&'); if (amp != std::string::npos) v = v.substr(0, amp);
                path = url_decode(v);
            }
        }
        if (path.empty()) {
            auto body = render_home("Please provide a path.");
            send_http_response(cs, body);
        }
        else {
            ScanResult R = run_scan(fs::path(path));
            auto body = render_results(path, R);
            send_http_response(cs, body);
        }
    }
    else {
        send_http_response(cs, "<h1>404 Not Found</h1>", "text/html; charset=utf-8", 404, "Not Found");
    }

#ifdef _WIN32
    closesocket(cs);
#else
    close(cs);
#endif
}

int main() {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        std::cerr << "socket() failed\n"; return 1;
    }

    int yes = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed on 127.0.0.1:8080\n";
#ifdef _WIN32
        closesocket(s); WSACleanup();
#else
        close(s);
#endif
        return 1;
    }

    if (::listen(s, 10) == SOCKET_ERROR) {
        std::cerr << "listen() failed\n";
#ifdef _WIN32
        closesocket(s); WSACleanup();
#else
        close(s);
#endif
        return 1;
    }

    std::cout << "Server running at http://127.0.0.1:8080\n";
    std::cout << "Press Ctrl+C to stop.\n";

    while (true) {
        sockaddr_in cli{}; socklen_t len = sizeof(cli);
        SOCKET cs = ::accept(s, (sockaddr*)&cli, &len);
        if (cs == INVALID_SOCKET) continue;
        // Handle each connection in its own thread (simple & safe)
        std::thread(handle_client, cs).detach();
    }

#ifdef _WIN32
    closesocket(s); WSACleanup();
#else
    close(s);
#endif
    return 0;
}
