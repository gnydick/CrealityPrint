#include "Filament.hpp"
#include "libslic3r/FilamentTypeRegistry.hpp"

#include "libslic3r/Utils.hpp"
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/regex.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

using namespace nlohmann;
namespace fs = boost::filesystem;

namespace creality
{
    Slic3r::FilamentTempType Filament::get_filament_temp_type(const std::string& filament_type)
    {
        // Delegated to the central registry, which normalizes the name (trim + case) and
        // resolves unknown/custom types through their base type instead of returning
        // Undefine. See Slic3r::FilamentTypeRegistry.
        return Slic3r::FilamentTypeRegistry::instance().temp_type(filament_type);
    }

    int Filament::get_hrc_by_nozzle_type(const Slic3r::NozzleType&type)
    {
        static std::map<std::string, int>nozzle_type_to_hrc;
        if (nozzle_type_to_hrc.empty()) {
            fs::path file_path = fs::path(Slic3r::resources_dir()) / "info" / "nozzle_info.json";
            boost::nowide::ifstream in(file_path.string());
            //std::ifstream in(file_path.string());
            json j;
            try {
                j = json::parse(in);
                in.close();
                for (const auto& elem : j["nozzle_hrc"].items())
                    nozzle_type_to_hrc[elem.key()] = elem.value();
            }
            catch (const json::parse_error& err) {
                in.close();
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << file_path.string() << " got a nlohmann::detail::parse_error, reason = " << err.what();
                nozzle_type_to_hrc = {
                    {"hardened_steel",55},
                    {"stainless_steel",20},
                    {"brass",2},
                    {"undefine",0}
                };
            }
        }
        auto iter = nozzle_type_to_hrc.find(Slic3r::NozzleTypeEumnToStr[type]);
        if (iter != nozzle_type_to_hrc.end())
            return iter->second;
        //0 represents undefine
        return 0;
    }

    bool Filament::check_multi_filaments_compatibility(const std::vector<std::string>& filament_types)
    {
        bool has_high_temperature_filament = false;
        bool has_low_temperature_filament = false;

        for (const auto& type : filament_types) {
            if (get_filament_temp_type(type) == Slic3r::FilamentTempType::HighTemp)
                has_high_temperature_filament = true;
            else if (get_filament_temp_type(type) == Slic3r::FilamentTempType::LowTemp)
                has_low_temperature_filament = true;
        }

        if (has_high_temperature_filament && has_low_temperature_filament)
            return false;

        return true;
    }

    bool Filament::is_filaments_compatible(const std::vector<int>& filament_types)
    {
        bool has_high_temperature_filament = false;
        bool has_low_temperature_filament = false;

        for (const auto& type : filament_types) {
            if (type == Slic3r::FilamentTempType::HighTemp)
                has_high_temperature_filament = true;
            else if (type == Slic3r::FilamentTempType::LowTemp)
                has_low_temperature_filament = true;
        }

        if (has_high_temperature_filament && has_low_temperature_filament)
            return false;

        return true;
    }

    int Filament::get_compatible_filament_type(const std::set<int>& filament_types)
    {
        bool has_high_temperature_filament = false;
        bool has_low_temperature_filament = false;

        for (const auto& type : filament_types) {
            if (type == Slic3r::FilamentTempType::HighTemp)
                has_high_temperature_filament = true;
            else if (type == Slic3r::FilamentTempType::LowTemp)
                has_low_temperature_filament = true;
        }

        if (has_high_temperature_filament && has_low_temperature_filament)
            return Slic3r::HighLowCompatible;
        else if (has_high_temperature_filament)
            return Slic3r::HighTemp;
        else if (has_low_temperature_filament)
            return Slic3r::LowTemp;
        return Slic3r::HighLowCompatible;
    }
}