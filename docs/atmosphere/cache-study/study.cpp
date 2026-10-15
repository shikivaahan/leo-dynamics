// ATM-CACHE-001 study executable.
//
// One process evaluates one (study kind, model, candidate) triple and publishes
// exactly one JSON result to <--out>/study-result.json. Nothing here is
// production code and nothing here changes a production number: every reference
// density comes from the existing DirectAtmosphere with all ATM-OPT-001
// preprocessing reuse enabled, or, in the partial native-input mode, from the
// production NRLMSISE-00 adapter at explicit Earth-fixed geodetic coordinates.
//
//   sensitivity   Phase 2: density sensitivity grids, finite differences at
//                 several perturbation scales, weather and boundary structure,
//                 compact heat maps, and a separate Ap-history experiment.
//   accuracy      Phase 5: candidate reconstruction against a fresh native
//                 evaluation at the same exact physical input, on independent
//                 randomised holdouts and on local query packets.
//   check         Phase 4 correctness restrictions: bitwise identity with the
//                 direct provider, model/config/weather/kernel invalidation,
//                 backwards time, cell boundaries, eviction and cold starts.
//   propagation   Phase 6 (performance owner, propagation.hpp).
//   locality      Phase 3 (performance owner, propagation.hpp).
//   benchmark     Phase 7 (performance owner, propagation.hpp).

#include "support.hpp"
#include "propagation.hpp"
#include "process_benchmark.hpp"
#include "sensitivity.cpp"
#include "native_sensitivity.cpp"
#include "accuracy.cpp"
#include "check.cpp"

#include <iostream>

namespace cache_study {

std::string sensitivity_study(const Config& c, Environment& env, const StudySettings& s);
std::string accuracy_study(const Config& c, Environment& env, const StudySettings& s);
std::string check_study(const Config& c, Environment& env, const StudySettings& s);
std::string native_geodetic_sensitivity_study(const Config& c, Environment& env, const StudySettings& s);

namespace {

std::string dispatch(const Config& c, Environment& env, const StudySettings& s) {
    if (s.native_geodetic) {
        // The partial native-input slice covers the sensitivity investigation
        // only. Every other kind needs the provider path, and saying so is
        // better than producing a number that looks like one.
        if (s.kind != "sensitivity")
            throw std::invalid_argument(
                "--study-native-geodetic supports --study-kind sensitivity only: the candidates, the "
                "coupled runs and the benchmarks all consume inertial states produced by the "
                "Earth-orientation preprocessing, which this mode deliberately does not use");
        return native_geodetic_sensitivity_study(c, env, s);
    }
    if (s.kind == "sensitivity") return sensitivity_study(c, env, s);
    if (s.kind == "accuracy") return accuracy_study(c, env, s);
    if (s.kind == "check") return check_study(c, env, s);
    if (s.kind == "propagation") return propagation_study(c, env, s);
    if (s.kind == "locality") return locality_study(c, env, s);
    if (s.kind == "benchmark") return benchmark_study(c, env, s);
    throw std::invalid_argument("unknown --study-kind: " + s.kind);
}

} // namespace
} // namespace cache_study

int main(int argc, char** argv) {
    using namespace cache_study;
    try {
        const StudyArguments parsed = parse_study_arguments(argc, argv);
        Config c = parsed.config;
        StudySettings s = parsed.settings;

        if (!s.verification_only) {
            std::vector<std::pair<std::string, std::string>> required{
                {c.lsk, "678e32bdb5a744117a467cd9601cd6b373f0e9bc9bbde1371d5eee39600a039b"},
                {c.weather, "f3eb9e676295bbf93566a054e95d1317de1aba2e410d209d4642dbedebf05ea2"}};
            if (!s.native_geodetic) {
                required.insert(required.end(), {
                    {c.spk, "363f32e14f5255359ac32c4d38080cf28ab55564a5e16696a75f63394b666e9b"},
                    {c.pck, "14deb9ab9dd49482185cc49a9c29fe56ae65d50ab94caf3197db8694836b6f81"},
                    {c.solfsmy, s.input_set == "direct-v2"
                        ? "891a0767702d11c2cd8432b3b012d4350c4becbf86873c32073cff852155f9d7"
                        : "9bc734e66da7b5919ce6fd2b3475db82c28e500988dbb2f8d994d5ff3c074e35"},
                    {c.dtcfile, "f6485450c363e9be8e71c37e143ac768462deca77565b727ec2e96c025ab8d91"}});
            }
            for (const auto& [path, expected] : required)
                if (path.empty() || sha256_file(path) != expected)
                    throw std::invalid_argument("primary input missing or hash mismatch: " + path);
            if (!c.options.utc_reuse || !c.options.orientation_reuse || !c.options.sun_sharing)
                throw std::invalid_argument("primary study requires all exact preprocessing reuse enabled");
        }
        if (s.native_geodetic) {
            if (s.model_kind != leo::atmosphere::Model::nrlmsise00)
                throw std::invalid_argument(
                    "--study-native-geodetic is NRLMSISE-00 only; its native input is a geodetic "
                    "state plus a UTC calendar, while JB2008 additionally needs a Sun direction "
                    "obtained from the orientation-capable ephemeris path");
            if (c.weather.empty())
                throw std::invalid_argument(
                    "--study-native-geodetic still needs the pinned CSSI weather archive: its Ap and "
                    "F10.7 inputs are real records, and no index is ever synthesised");
            if (c.ap_history && !compiled_in(leo::atmosphere::Model::nrlmsise00))
                throw std::invalid_argument("--ap-history needs the NRLMSISE-00 backend");
        } else if (!compiled_in(s.model_kind)) {
            throw std::invalid_argument(std::string("--study-model ") + model_name(s.model_kind) +
                                        " is not compiled into this build");
        }

        ensure_output_directory(c);

        // The kernels must be furnished before any UTC conversion happens, so the
        // fixture epochs are resolved after the environment exists, exactly as
        // the existing driver does.
        const std::unique_ptr<Environment> environment = make_study_environment(c, s.native_geodetic);
        for (auto& fixture : c.fixture_epochs)
            fixture.epoch = leo::SpiceEphemerisProvider::epoch_from_utc(fixture.utc);
        Environment& env = *environment;
        if (s.native_geodetic) {
            // Provenance names the files this mode really loaded: the leapsecond
            // kernel (furnished once, under both the ephemeris and kernel slots)
            // and the pinned CSSI archive. No planetary ephemeris, orientation
            // kernel or JB2008 record is read.
            c.pck.clear();
            c.solfsmy.clear();
            c.dtcfile.clear();
        }

        const std::string body = dispatch(c, env, s);

        cache_study::Json document;
        document.raw("metadata", study_metadata(c, env, s));
        document.raw("result", body);
        publish_study_result(c, document.str());
        std::cout << document.str() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "atm_cache_study: " << error.what() << '\n';
        return 1;
    }
}