#pragma once
#include "leo/atmosphere/types.hpp"
#include "leo/environment/spice.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <string>
namespace leo::atmosphere {
enum class RecordStatus { observed,interpolated,predicted,missing };
class WeatherDataset {
    struct CssiRecord {
        double f107_observed,f107_observed_center81,ap_daily;
        std::array<double,8> ap;
        RecordStatus status;
    };
    struct SolarRecord { std::array<double,8> proxies;std::string source_codes; };
    std::map<long long,CssiRecord> cssi_;
    std::map<long long,SolarRecord> solar_;
    std::map<long long,std::array<int,24>> dtc_;
public:
    // CelesTrak/CSSI v1.2 fixed-field archive (including explicit OBSERVED sections),
    // official SOLFSMY and DTCFILE. Empty paths mean dataset intentionally absent.
    static WeatherDataset load(const std::filesystem::path& cssi_archive,
                               const std::filesystem::path& solfsmy,
                               const std::filesystem::path& dtcfile);
    NrlmsiseInput nrlmsise(const UtcCalendar&,bool history=true) const;
    Jb2008Input jb2008(const UtcCalendar&) const;
};
}
