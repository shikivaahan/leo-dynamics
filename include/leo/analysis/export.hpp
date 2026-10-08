#pragma once
#include "leo/analysis/loads.hpp"
#include "leo/analysis/orbital.hpp"
#include <filesystem>
namespace leo::analysis {
// All diagnostics are evaluated at every trajectory sample, never downsampled.
struct AnalysisResult {
    Epoch epoch;
    Earth earth;
    double mass_kg;
    EnvironmentalModels models;
    bool j2_enabled;
    std::vector<OrbitalSample> orbital;
    std::vector<ApsisEvent> apsides;
    std::vector<OrbitSummary> orbits;
    std::vector<EnvironmentalSample> environment;
    std::vector<AttitudeSample> attitude;
    std::vector<WorkSample> work;
    std::vector<OrbitWork> orbit_work;
};
// Snapshots must correspond one-for-one to trajectory times and epoch.
AnalysisResult analyze(const Trajectory&,const EarthDynamics&,std::span<const EnvironmentSnapshot>);
// Creates the directory if necessary; replaces named CSV files. Validates all
// data before opening files. I/O failures throw; a failed write is not atomic.
void export_csv(const std::filesystem::path&,const Trajectory&,const AnalysisResult&);
}
