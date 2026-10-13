#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace padporter
{
/** Thrown when a file cannot be read or decoded at all. */
struct ParseError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** Thrown when an export is refused (e.g. it would touch the source project). */
struct ExportError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/** Thrown when importing a project into the local library fails. */
struct ImportError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

enum class Severity
{
    Info,
    Warning,
    Error
};

inline const char* toString (Severity s)
{
    switch (s)
    {
        case Severity::Info:    return "info";
        case Severity::Warning: return "warning";
        case Severity::Error:   return "error";
    }
    return "?";
}

/** A non-fatal finding, e.g. an unknown event type or a missing field. */
struct Diagnostic
{
    Severity severity = Severity::Info;
    std::string where;   // JSON path or file the finding refers to
    std::string message;
};

/** Collects findings instead of silently swallowing unknown format variants. */
class Diagnostics
{
public:
    void add (Severity s, std::string where, std::string message)
    {
        items_.push_back ({ s, std::move (where), std::move (message) });
    }

    void info    (std::string where, std::string message) { add (Severity::Info,    std::move (where), std::move (message)); }
    void warning (std::string where, std::string message) { add (Severity::Warning, std::move (where), std::move (message)); }
    void error   (std::string where, std::string message) { add (Severity::Error,   std::move (where), std::move (message)); }

    void append (const Diagnostics& other)
    {
        items_.insert (items_.end(), other.items_.begin(), other.items_.end());
    }

    const std::vector<Diagnostic>& items() const noexcept { return items_; }
    bool empty() const noexcept { return items_.empty(); }

    std::size_t count (Severity s) const noexcept
    {
        std::size_t n = 0;
        for (const auto& d : items_)
            if (d.severity == s)
                ++n;
        return n;
    }

private:
    std::vector<Diagnostic> items_;
};
} // namespace padporter
