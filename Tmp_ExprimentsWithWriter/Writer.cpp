#include "Writer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace Writer_ {

    // ctor
    Writer::Writer(std::string indentUnit)
        : indentUnit_(std::move(indentUnit)) {
    }

    // primitives
    void Writer::append_raw(const std::string& line) { lines_.push_back(line); }
    void Writer::append(const std::string& line) { lines_.push_back(indent_prefix() + line); }
    void Writer::line(const std::string& s) { append(s); }

    // single-line with replacement
    bool Writer::line(const std::string& tmpl, const Vars& vars,
        ReplaceStats* outStats, bool require_any)
    {
        ReplaceStats st;
        std::string replaced = replace_placeholders(tmpl, vars, st);
        append(replaced);
        if (outStats) *outStats = st;
        if (!st.ok(require_any)) {
            report_replace_issue("line", tmpl, st, require_any);
            return false;
        }
        return true;
    }

    // blank lines
    void Writer::blank(size_t n) {
        while (n--) lines_.push_back("");
    }

    // comments
    void Writer::comment(const std::string& s) {
        append("// " + s);
    }

    bool Writer::comment(const std::string& tmpl, const Vars& vars,
        ReplaceStats* outStats, bool require_any)
    {
        ReplaceStats st;
        std::string replaced = replace_placeholders(tmpl, vars, st);
        append("// " + replaced);
        if (outStats) *outStats = st;
        if (!st.ok(require_any)) {
            report_replace_issue("comment", tmpl, st, require_any);
            return false;
        }
        return true;
    }

    bool Writer::comments(const std::string& tmplMultiline, const Vars& vars,
        ReplaceStats* outStats, bool require_any)
    {
        ReplaceStats agg;
        std::string cur; cur.reserve(tmplMultiline.size());

        auto flush_line = [&](std::string& s) {
            ReplaceStats st;
            std::string replaced = replace_placeholders(s, vars, st);
            append("// " + replaced);
            agg.placeholders_found += st.placeholders_found;
            agg.replacements_done += st.replacements_done;
            agg.missing_placeholders.insert(agg.missing_placeholders.end(),
                st.missing_placeholders.begin(),
                st.missing_placeholders.end());
            };

        for (size_t i = 0; i < tmplMultiline.size(); ++i) {
            char c = tmplMultiline[i];
            if (c == '\r') { if (i + 1 < tmplMultiline.size() && tmplMultiline[i + 1] == '\n') continue; }
            if (c == '\n') { flush_line(cur); cur.clear(); }
            else { cur.push_back(c); }
        }
        if (!cur.empty()) flush_line(cur);

        std::unordered_set<std::string> used;
        collect_used_placeholders(tmplMultiline, used);
        for (const auto& kv : vars) if (!used.count(kv.first)) agg.unused_keys.push_back(kv.first);

        dedupe_sort(agg.missing_placeholders);
        dedupe_sort(agg.unused_keys);

        if (outStats) *outStats = agg;
        if (!agg.ok(require_any)) {
            report_replace_issue("comments", tmplMultiline, agg, require_any);
            return false;
        }
        return true;
    }

    // multi-line content with replacement
    bool Writer::lines(const std::string& tmplMultiline, const Vars& vars,
        ReplaceStats* outStats, bool require_any)
    {
        ReplaceStats agg;
        std::string cur; cur.reserve(tmplMultiline.size());

        auto flush_line = [&](std::string& s) {
            ReplaceStats st;
            std::string replaced = replace_placeholders(s, vars, st);
            append(replaced);
            agg.placeholders_found += st.placeholders_found;
            agg.replacements_done += st.replacements_done;
            agg.missing_placeholders.insert(agg.missing_placeholders.end(),
                st.missing_placeholders.begin(),
                st.missing_placeholders.end());
            };

        for (size_t i = 0; i < tmplMultiline.size(); ++i) {
            char c = tmplMultiline[i];
            if (c == '\r') { if (i + 1 < tmplMultiline.size() && tmplMultiline[i + 1] == '\n') continue; }
            if (c == '\n') { flush_line(cur); cur.clear(); }
            else { cur.push_back(c); }
        }
        if (!cur.empty()) flush_line(cur);

        std::unordered_set<std::string> used;
        collect_used_placeholders(tmplMultiline, used);
        for (const auto& kv : vars) if (!used.count(kv.first)) agg.unused_keys.push_back(kv.first);

        dedupe_sort(agg.missing_placeholders);
        dedupe_sort(agg.unused_keys);

        if (outStats) *outStats = agg;
        if (!agg.ok(require_any)) {
            report_replace_issue("lines", tmplMultiline, agg, require_any);
            return false;
        }
        return true;
    }

    // indentation helpers
    void Writer::open(const std::string& lineWithBrace) { line(lineWithBrace); ++indentLevel_; }
    void Writer::close(const std::string& closingBrace) { if (indentLevel_ > 0) --indentLevel_; line(closingBrace); }

    // io / utils
    void Writer::print() const { write_to(std::cout); }

    void Writer::write_to(std::ostream& os) const {
        for (const auto& l : lines_) os << l << '\n';
    }

    void Writer::save(const std::filesystem::path& filepath) const {
        namespace fs = std::filesystem;
        if (filepath.has_parent_path()) fs::create_directories(filepath.parent_path());
        std::ofstream out(filepath, std::ios::binary);
        for (const auto& l : lines_) out << l << '\n';
    }

    void Writer::clear() {
        lines_.clear();
        indentLevel_ = 0;
    }

    std::string Writer::str() const {
        std::ostringstream oss;
        for (const auto& l : lines_) oss << l << '\n';
        return oss.str();
    }

    // internals
    std::string Writer::replace_placeholders(const std::string& s, const Vars& vars, ReplaceStats& st) {
        std::string out; out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ) {
            if (i + 1 < s.size() && s[i] == '$' && s[i + 1] == '{') {
                size_t end = s.find('}', i + 2);
                if (end != std::string::npos) {
                    std::string key = s.substr(i + 2, end - (i + 2));
                    ++st.placeholders_found;
                    auto it = vars.find(key);
                    if (it != vars.end()) {
                        out += it->second;
                        ++st.replacements_done;
                    }
                    else {
                        // Keep visible in output for easier debugging
                        out += "${"; out += key; out += "}";
                        st.missing_placeholders.push_back(key);
                    }
                    i = end + 1;
                    continue;
                }
            }
            out += s[i++];
        }
        return out;
    }

    void Writer::collect_used_placeholders(const std::string& s, std::unordered_set<std::string>& used) {
        for (size_t i = 0; i < s.size(); ) {
            if (i + 1 < s.size() && s[i] == '$' && s[i + 1] == '{') {
                size_t end = s.find('}', i + 2);
                if (end != std::string::npos) {
                    used.insert(s.substr(i + 2, end - (i + 2)));
                    i = end + 1; continue;
                }
            }
            ++i;
        }
    }

    void Writer::report_replace_issue(const char* fn, const std::string& /*src*/,
        const ReplaceStats& st, bool require_any)
    {
        std::cerr << "[Writer] " << fn << " replacement check FAILED\n";
        std::cerr << "  placeholders_found: " << st.placeholders_found << "\n";
        std::cerr << "  replacements_done : " << st.replacements_done << "\n";
        if (!st.missing_placeholders.empty()) {
            std::cerr << "  missing: ";
            for (auto& k : st.missing_placeholders) std::cerr << k << " ";
            std::cerr << "\n";
        }
        if (require_any && st.placeholders_found > 0 && st.replacements_done == 0) {
            std::cerr << "  Warning: placeholders present but no replacements happened (require_any=true)\n";
        }
        if (!st.unused_keys.empty()) {
            std::cerr << "  unused keys: ";
            for (auto& k : st.unused_keys) std::cerr << k << " ";
            std::cerr << "\n";
        }
    }

    void Writer::dedupe_sort(std::vector<std::string>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }

    std::string Writer::indent_prefix() const {
        std::string p; p.reserve(indentLevel_ * indentUnit_.size());
        for (int i = 0; i < indentLevel_; ++i) p += indentUnit_;
        return p;
    }

} // namespace Writer_
