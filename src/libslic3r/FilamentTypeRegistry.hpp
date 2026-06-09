#ifndef slic3r_FilamentTypeRegistry_hpp_
#define slic3r_FilamentTypeRegistry_hpp_

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "libslic3r/FDM/Filament.hpp" // Slic3r::FilamentTempType

namespace Slic3r {

// Central registry of filament-type metadata.
//
// Base-type inheritance is the key idea behind "arbitrary filament types": a custom type
// (e.g. "Galaxy") resolves its behavior through a *base type* (e.g. "PLA") rather than
// falling through to Undefine. Inheritance is ALWAYS explicit — there is no name-based
// inference. Two sources feed the base map:
//
//   * Built-ins: every shipped type is declared in resources/info/filament_info.json,
//     with base types mapping to themselves (PLA->PLA) and derived types mapping to their
//     base (PLA-CF->PLA, PA-CF->PA, ...). This map is the single source of truth for which
//     types are "built-in" and what each inherits.
//   * User customs: name->base pairs the user chose when creating a custom type, persisted
//     to data_dir()/custom_filament_types.json and merged in at load time.
//
// Lookups are normalized (trimmed + upper-cased) so casing/whitespace don't matter.
//
// The built-in map is immutable after load, so the common path (built-in types) is lock-free.
// Only user-custom lookups take a mutex, since add_custom_type() may mutate that map at runtime.
class FilamentTypeRegistry
{
public:
    static FilamentTypeRegistry& instance();

    // Temperature class for a (possibly custom) filament type; resolves via base type.
    FilamentTempType temp_type(const std::string& filament_type) const;

    // The canonical base a type derives from: looked up in the explicit built-in map first,
    // then the user-custom map. Returns "" when the type has no declared base.
    std::string base_type(const std::string& filament_type) const;

    // The key to use when matching type-specific behavior. A shipped built-in (base OR derived)
    // returns itself, so built-in behavior is preserved exactly; a user custom type returns its
    // explicitly-chosen base so it inherits behavior; an undeclared type returns itself.
    // Returns a normalized (upper-case) string.
    std::string effective_type(const std::string& filament_type) const;

    // True when the type is a shipped built-in (declared in filament_info.json).
    bool is_builtin(const std::string& filament_type) const;

    // The built-in base types (those that map to themselves), sorted. Useful for offering a
    // base-type picker when the user creates a custom type.
    std::vector<std::string> base_types() const;

    // Register (or, with an empty base, unregister) a user-defined custom type and its chosen
    // base, persisting the whole user map to data_dir()/custom_filament_types.json. Takes effect
    // immediately for subsequent lookups.
    void add_custom_type(const std::string& name, const std::string& base);

    // trim + upper-case, used for all matching. Public so callers can match consistently.
    static std::string normalize(const std::string& s);

private:
    FilamentTypeRegistry() = default;
    void ensure_loaded() const;
    void load() const;
    void load_custom() const;
    FilamentTempType classify(const std::string& normalized) const;

    mutable std::once_flag                               m_load_flag;
    mutable std::unordered_set<std::string>              m_high_temp;            // normalized
    mutable std::unordered_set<std::string>              m_low_temp;             // normalized
    mutable std::unordered_set<std::string>              m_high_low_compatible;  // normalized
    // Shipped built-ins: normalized type -> normalized base (bases map to themselves). Its
    // keyset is the set of built-in types. Immutable after load (no lock needed to read).
    mutable std::unordered_map<std::string, std::string> m_builtin_base;
    // User-defined custom types: normalized name -> normalized base. Guarded by m_custom_mutex.
    mutable std::unordered_map<std::string, std::string> m_custom_base;
    mutable std::mutex                                   m_custom_mutex;
};

} // namespace Slic3r

#endif // slic3r_FilamentTypeRegistry_hpp_
