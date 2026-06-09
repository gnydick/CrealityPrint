#include <catch2/catch.hpp>

#include "libslic3r/FilamentTypeRegistry.hpp"

using namespace Slic3r;

// These tests pass whether the registry loads resources/info/filament_info.json or falls back to
// its built-in defaults: every built-in referenced here is present (with the same classification
// and base) in both. Inheritance is ALWAYS explicit — there is no name-based inference — so the
// built-in map is the single source of truth and custom types must be registered explicitly.
//
// Custom-type names used here (GALAXY, MYHOTSTUFF) are kept disjoint from the undeclared names
// asserted to have no base (PLA-Galaxy, PLATINUM, PCTG-X, ...), so the persisted custom-types file
// and the process-wide singleton can never cross-contaminate these assertions.

TEST_CASE("FilamentTypeRegistry classifies known built-in types", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.temp_type("PLA")  == LowTemp);
    REQUIRE(r.temp_type("ABS")  == HighTemp);
    REQUIRE(r.temp_type("PETG") == HighLowCompatible);
    REQUIRE(r.is_builtin("PLA"));
    REQUIRE(r.is_builtin("PETG"));
}

TEST_CASE("FilamentTypeRegistry built-in base resolution is explicit", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    // Bases map to themselves.
    REQUIRE(r.base_type("PLA") == "PLA");
    REQUIRE(r.base_type("ABS") == "ABS");
    REQUIRE(r.base_type("PA")  == "PA");
    // Derived built-ins map to their declared base.
    REQUIRE(r.base_type("PLA-CF")  == "PLA");
    REQUIRE(r.base_type("PA-CF")   == "PA");
    REQUIRE(r.base_type("PETG-CF") == "PETG");
    REQUIRE(r.base_type("PA6")     == "PA");   // explicit, even though "PA6" has no separator
    REQUIRE(r.is_builtin("PLA-CF"));
    REQUIRE(r.is_builtin("PA6"));
}

TEST_CASE("FilamentTypeRegistry effective_type keeps built-ins (incl. derived) as themselves", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.effective_type("PLA")    == "PLA");
    REQUIRE(r.effective_type("PETG")   == "PETG");
    // Derived built-ins are NOT collapsed, so their distinct per-type behavior is preserved
    // (this is what lets Model::getThermalLength route through effective_type: PA-CF != PA).
    REQUIRE(r.effective_type("PLA-CF") == "PLA-CF");
    REQUIRE(r.effective_type("PA-CF")  == "PA-CF");
    REQUIRE(r.effective_type("PET-CF") == "PET-CF");
    REQUIRE(r.effective_type("PC")     == "PC");
    REQUIRE(r.effective_type("TPU")    == "TPU");
}

TEST_CASE("FilamentTypeRegistry does not infer a base from the name", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    // An undeclared type inherits nothing, regardless of how its name reads. There is no
    // separator-guarded prefix matching anymore — only explicit declarations count.
    REQUIRE(r.base_type("PLA-Galaxy").empty());
    REQUIRE(r.temp_type("PLA-Galaxy") == Undefine);
    REQUIRE(r.effective_type("PLA-Galaxy") == "PLA-GALAXY"); // resolves to itself (normalized)
    REQUIRE_FALSE(r.is_builtin("PLA-Galaxy"));

    REQUIRE(r.base_type("PLATINUM").empty());
    REQUIRE(r.temp_type("PLATINUM") == Undefine);
    // "PCTG-X" used to be inferred to PCTG; with explicit-only it does not.
    REQUIRE(r.base_type("PCTG-X").empty());
    REQUIRE(r.effective_type("PCTG-X") == "PCTG-X");
}

TEST_CASE("FilamentTypeRegistry normalizes case and whitespace", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.temp_type("  pla ")   == LowTemp);   // trimmed + upper
    REQUIRE(r.base_type("pla-cf")   == "PLA");
    REQUIRE(r.effective_type("Pla") == "PLA");
}

TEST_CASE("FilamentTypeRegistry: explicit custom types inherit their chosen base", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    // A custom type with no recognizable prefix still inherits, because the base is explicit.
    r.add_custom_type("Galaxy", "PLA");
    REQUIRE(r.base_type("Galaxy")      == "PLA");
    REQUIRE(r.temp_type("Galaxy")      == LowTemp);   // inherits PLA
    REQUIRE(r.effective_type("Galaxy") == "PLA");     // collapses to base for behavior matching
    REQUIRE_FALSE(r.is_builtin("Galaxy"));            // still not a built-in

    r.add_custom_type("MyHotStuff", "ABS");
    REQUIRE(r.temp_type("MyHotStuff")      == HighTemp);
    REQUIRE(r.effective_type("MyHotStuff") == "ABS");

    // A custom type may not shadow a built-in: the registration is ignored.
    r.add_custom_type("PLA", "ABS");
    REQUIRE(r.effective_type("PLA") == "PLA");
    REQUIRE(r.temp_type("PLA")      == LowTemp);
}

TEST_CASE("FilamentTypeRegistry: unknown and empty map to Undefine", "[FilamentTypeRegistry]")
{
    auto& r = FilamentTypeRegistry::instance();
    REQUIRE(r.temp_type("ZZZ-Unobtanium") == Undefine);
    REQUIRE(r.temp_type("") == Undefine);
    REQUIRE(r.base_type("").empty());
}
