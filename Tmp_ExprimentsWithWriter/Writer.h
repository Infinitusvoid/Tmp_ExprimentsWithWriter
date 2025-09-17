#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <ostream>
#include <format>     // C++20

namespace Writer_ {

    class Writer {
    public:
        using Vars = std::unordered_map<std::string, std::string>;

        struct ReplaceStats {
            size_t placeholders_found = 0;
            size_t replacements_done = 0;
            std::vector<std::string> missing_placeholders; // ${...} with no provided value
            std::vector<std::string> unused_keys;          // provided vars not used
            bool ok(bool require_any) const {
                if (!missing_placeholders.empty()) return false;
                if (require_any && replacements_done == 0 && placeholders_found > 0) return false;
                return true;
            }
        };

        explicit Writer(std::string indentUnit = "    ");

        // Append primitives
        void append_raw(const std::string& line);
        void append(const std::string& line);
        void line(const std::string& s);

        // Single-line with placeholder replacement
        bool line(const std::string& tmpl, const Vars& vars,
            ReplaceStats* outStats = nullptr, bool require_any = true);

        // Blank line(s)
        void blank(size_t n = 1);

        // Comments
        void comment(const std::string& s); // single-line, no replacement
        bool comment(const std::string& tmpl, const Vars& vars,
            ReplaceStats* outStats = nullptr, bool require_any = true);
        bool comments(const std::string& tmplMultiline, const Vars& vars,
            ReplaceStats* outStats = nullptr, bool require_any = true);

        // Multi-line content (CR/LF safe)
        bool lines(const std::string& tmplMultiline, const Vars& vars,
            ReplaceStats* outStats = nullptr, bool require_any = true);

        // Indentation helpers
        void open(const std::string& lineWithBrace = "{");
        void close(const std::string& closingBrace = "}");

        // Utilities
        void print() const;
        void write_to(std::ostream& os) const;
        void save(const std::filesystem::path& filepath) const;
        void clear();
        std::string str() const;
        size_t size()  const { return lines_.size(); }
        bool   empty() const { return lines_.empty(); }

        // printf-style but type-safe using std::format
        template <class... Args>
        void linef(std::string_view fmt, Args&&... args) {
            append(std::vformat(fmt, std::make_format_args(std::forward<Args>(args)...)));
        }

        // RAII indentation scope (alternative to open/close if you don’t need braces)
        class Indent {
        public:
            explicit Indent(Writer& w) : w_(w) { ++w_.indentLevel_; }
            ~Indent() { if (w_.indentLevel_ > 0) --w_.indentLevel_; }
            Indent(const Indent&) = delete; Indent& operator=(const Indent&) = delete;
        private:
            Writer& w_;
        };

    private:
        // Core replacement
        static std::string replace_placeholders(const std::string& s, const Vars& vars, ReplaceStats& st);
        static void collect_used_placeholders(const std::string& s, std::unordered_set<std::string>& used);
        static void report_replace_issue(const char* fn, const std::string& src,
            const ReplaceStats& st, bool require_any);
        static void dedupe_sort(std::vector<std::string>& v);

        std::string indent_prefix() const;

        std::vector<std::string> lines_;
        int indentLevel_ = 0;
        std::string indentUnit_;
    };

} // namespace Writer_
