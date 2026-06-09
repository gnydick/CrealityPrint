#include "FilamentTypeRegistry.hpp"

#include "libslic3r/Utils.hpp" // resources_dir(), data_dir()

#include <algorithm>
#include <utility>
#include <vector>

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r {

FilamentTypeRegistry& FilamentTypeRegistry::instance()
{
    static FilamentTypeRegistry s_instance;
    return s_instance;
}

std::string FilamentTypeRegistry::normalize(const std::string& s)
{
    std::string n = s;
    boost::algorithm::trim(n);
    boost::algorithm::to_upper(n);
    return n;
}

void FilamentTypeRegistry::ensure_loaded() const
{
    std::call_once(m_load_flag, [this] { load(); load_custom(); });
}

void FilamentTypeRegistry::load() const
{
    namespace fs = boost::filesystem;
    using nlohmann::json;

    auto to_normalized_set = [](const std::vector<std::string>& v) {
        std::unordered_set<std::string> s;
        s.reserve(v.size());
        for (const auto& e : v)
            s.insert(normalize(e));
        return s;
    };

    // Hardcoded fallback, mirroring filament_info.json, used if the json is missing or
    // unparseable so classification/inheritance never silently disappears. Built-in bases map
    // to themselves and derived built-ins map to their base — the same explicit data as the json.
    auto set_defaults = [this] {
        m_high_temp = {"ABS", "ASA", "PC", "PA", "PA-CF", "PA-GF", "PA6-CF", "PET-CF",
                       "PPS", "PPS-CF", "PPA-GF", "PPA-CF", "ABS-GF", "ASA-AERO"};
        m_low_temp  = {"PLA", "TPU", "PLA-CF", "PLA-AERO", "PVA", "BVOH"};
        m_high_low_compatible = {"HIPS", "PETG", "PE", "PP", "EVA", "PE-CF", "PP-CF", "PP-GF", "PHA"};

        static const char* bases[] = {"PLA", "ABS", "ASA", "PETG", "PCTG", "PET", "PA", "PC",
                                      "PE", "PP", "TPU", "HIPS", "PVA", "BVOH", "PPS", "PPA", "EVA", "PHA"};
        for (const char* b : bases)
            m_builtin_base[b] = b;
        static const std::pair<const char*, const char*> derived[] = {
            {"PLA-CF", "PLA"}, {"PLA-AERO", "PLA"}, {"ABS-GF", "ABS"}, {"ASA-AERO", "ASA"},
            {"PA-CF", "PA"}, {"PA-GF", "PA"}, {"PA6", "PA"}, {"PA6-CF", "PA"}, {"PAHT", "PA"}, {"PAHT-CF", "PA"},
            {"PET-CF", "PET"}, {"PETG-CF", "PETG"}, {"PE-CF", "PE"}, {"PP-CF", "PP"}, {"PP-GF", "PP"},
            {"PC-CF", "PC"}, {"PPS-CF", "PPS"}, {"PPA-CF", "PPA"}, {"PPA-GF", "PPA"}};
        for (const auto& d : derived)
            m_builtin_base[d.first] = d.second;
    };

    try {
        const fs::path file_path = fs::path(resources_dir()) / "info" / "filament_info.json";
        boost::nowide::ifstream in(file_path.string());
        const json j = json::parse(in);

        m_high_temp           = to_normalized_set(j.at("high_temp_filament").get<std::vector<std::string>>());
        m_low_temp            = to_normalized_set(j.at("low_temp_filament").get<std::vector<std::string>>());
        m_high_low_compatible = to_normalized_set(j.at("high_low_compatible_filament").get<std::vector<std::string>>());

        // Built-in base types map to themselves (this is what marks them as built-in).
        if (j.contains("base_types"))
            for (const auto& e : j.at("base_types").get<std::vector<std::string>>()) {
                const std::string n = normalize(e);
                m_builtin_base[n] = n;
            }
        // Derived built-ins explicitly map to their base.
        if (j.contains("base_type"))
            for (const auto& kv : j.at("base_type").items())
                m_builtin_base[normalize(kv.key())] = normalize(kv.value().get<std::string>());

        if (m_builtin_base.empty())
            set_defaults();
    } catch (const std::exception& err) {
        BOOST_LOG_TRIVIAL(error) << "FilamentTypeRegistry: failed to load filament_info.json (" << err.what()
                                 << "); using built-in defaults";
        set_defaults();
    }
}

void FilamentTypeRegistry::load_custom() const
{
    namespace fs = boost::filesystem;
    using nlohmann::json;
    try {
        const fs::path file_path = fs::path(data_dir()) / "custom_filament_types.json";
        if (!fs::exists(file_path))
            return;
        boost::nowide::ifstream in(file_path.string());
        const json j = json::parse(in);
        std::lock_guard<std::mutex> lock(m_custom_mutex);
        for (const auto& kv : j.items())
            m_custom_base[normalize(kv.key())] = normalize(kv.value().get<std::string>());
    } catch (const std::exception& err) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentTypeRegistry: failed to load custom_filament_types.json ("
                                   << err.what() << "); ignoring user custom types";
    }
}

void FilamentTypeRegistry::add_custom_type(const std::string& name, const std::string& base)
{
    namespace fs = boost::filesystem;
    using nlohmann::json;

    ensure_loaded();
    const std::string n = normalize(name);
    const std::string b = normalize(base);
    if (n.empty())
        return;
    // A custom type must not shadow a built-in, and must not be its own base.
    if (m_builtin_base.count(n) != 0 || n == b)
        return;

    json out = json::object();
    {
        std::lock_guard<std::mutex> lock(m_custom_mutex);
        if (b.empty())
            m_custom_base.erase(n);
        else
            m_custom_base[n] = b;
        for (const auto& kv : m_custom_base)
            out[kv.first] = kv.second;
    }
    // Persist outside the lock (small file, infrequent).
    try {
        const fs::path file_path = fs::path(data_dir()) / "custom_filament_types.json";
        boost::nowide::ofstream o(file_path.string());
        o << out.dump(2);
    } catch (const std::exception& err) {
        BOOST_LOG_TRIVIAL(error) << "FilamentTypeRegistry: failed to persist custom_filament_types.json ("
                                 << err.what() << ")";
    }
}

FilamentTempType FilamentTypeRegistry::classify(const std::string& n) const
{
    // Order preserved from the original implementation: compatible wins over high/low.
    if (m_high_low_compatible.count(n)) return HighLowCompatible;
    if (m_high_temp.count(n))           return HighTemp;
    if (m_low_temp.count(n))            return LowTemp;
    return Undefine;
}

bool FilamentTypeRegistry::is_builtin(const std::string& filament_type) const
{
    ensure_loaded();
    return m_builtin_base.count(normalize(filament_type)) != 0;
}

std::vector<std::string> FilamentTypeRegistry::base_types() const
{
    ensure_loaded();
    std::vector<std::string> out;
    for (const auto& kv : m_builtin_base)
        if (kv.first == kv.second) // base types map to themselves
            out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

std::string FilamentTypeRegistry::base_type(const std::string& filament_type) const
{
    ensure_loaded();
    const std::string n = normalize(filament_type);
    if (n.empty())
        return {};

    // Built-in map first (immutable, lock-free); then the user-custom map.
    const auto it = m_builtin_base.find(n);
    if (it != m_builtin_base.end())
        return it->second;

    std::lock_guard<std::mutex> lock(m_custom_mutex);
    const auto cit = m_custom_base.find(n);
    if (cit != m_custom_base.end())
        return cit->second;
    return {};
}

std::string FilamentTypeRegistry::effective_type(const std::string& filament_type) const
{
    ensure_loaded();
    const std::string n = normalize(filament_type);
    // A shipped built-in (base OR derived) keeps its own identity, so built-in behavior is
    // preserved exactly. This check is lock-free.
    if (m_builtin_base.count(n) != 0)
        return n;
    // A user custom type resolves to its explicitly-chosen base; an undeclared type resolves
    // to itself (no inheritance — inheritance is never inferred from the name).
    std::lock_guard<std::mutex> lock(m_custom_mutex);
    const auto it = m_custom_base.find(n);
    return it != m_custom_base.end() ? it->second : n;
}

FilamentTempType FilamentTypeRegistry::temp_type(const std::string& filament_type) const
{
    ensure_loaded();
    const std::string n = normalize(filament_type);

    // 1) Direct classification of the type itself.
    const FilamentTempType direct = classify(n);
    if (direct != Undefine)
        return direct;

    // 2) Resolve through the explicitly-declared base type.
    const std::string base = base_type(filament_type);
    if (!base.empty() && base != n)
        return classify(base);

    return Undefine;
}

} // namespace Slic3r
