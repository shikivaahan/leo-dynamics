// Concrete MT-002-compatible process-local chunk; no IPC or public API change.
#pragma once
#include "support.hpp"
#include "propagation.hpp"

namespace cache_study {

inline std::string process_benchmark_study(const Config& c, Environment& env, const StudySettings& s) {
    if (c.case_files.empty()) throw std::invalid_argument("process benchmark needs a --case");
    if (s.begin > s.ensemble) throw std::invalid_argument("process range begins past ensemble");
    const auto count = s.count == 0 ? s.ensemble - s.begin : s.count;
    if (count > s.ensemble - s.begin) throw std::invalid_argument("process range extends past ensemble");
    const auto model = required_model(s);
    const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
    const auto labels = sample_labels(c);
    const auto spec = experiment::load(c.case_files.front());
    const auto settings = evaluator_settings_for(s);
    const auto destination = std::filesystem::path(c.out) / "trajectories.bin";
    const auto staging = std::filesystem::path(destination.string() + ".partial");
    static_assert(std::endian::native == std::endian::little, "MT-002 binary output requires little endian");
    const auto timer = Clock::now();
    DensityEvaluator evaluator(c, env, model, settings);
    std::ofstream output(staging, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create isolated trajectory output");
    output.write("MTISOB01", 8);
    const auto scalar = [&](const auto& value) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    scalar(epoch.et_seconds);
    for (std::uint64_t value : {std::uint64_t(s.ensemble), std::uint64_t(s.begin), std::uint64_t(count),
                               std::uint64_t(labels.size()), std::uint64_t(13)}) scalar(value);
    for (double label : labels) scalar(label);
    std::size_t rhs_calls = 0;
    Checksum checksum;
    for (std::size_t local = 0; local < count; ++local) {
        const auto initial = perturb_state(spec.start(), s.seed, s.begin + local, perturbation_scale(s)).state;
        CoupledRun run;
        if (s.benchmark_route == "direct") {
            AuthoritativeDensitySource source(evaluator);
            run = run_coupled(c, env, spec, initial, source, evaluator, labels, epoch, false, false);
        } else {
            CandidateDensitySource source(evaluator);
            run = run_coupled(c, env, spec, initial, source, evaluator, labels, epoch, false, false);
        }
        rhs_calls += run.rhs_calls;
        for (const auto& sample : run.trajectory.samples) {
            const auto packed = leo::detail::pack(sample.state);
            for (double component : packed) scalar(component);
            checksum.add(sample.state);
        }
    }
    output.flush();
    if (!output) throw std::runtime_error("failed isolated trajectory write");
    output.close();
    std::filesystem::rename(staging, destination);
    const double seconds = elapsed(timer);
    Json result;
    result.add("study_kind", "benchmark");
    result.add("benchmark_route", s.benchmark_route);
    result.add("global_total", s.ensemble);
    result.add("global_begin", s.begin);
    result.add("trajectory_count", count);
    result.add("sample_count", labels.size());
    result.add("trajectory_binary", destination.string());
    result.add("trajectory_checksum", checksum.str());
    result.add("wall_seconds_construction_propagation_output", seconds);
    result.add("wall_seconds_contents",
               "construction of the one cache-owning evaluator, the coupled propagations of this "
               "process's global slice, the MT-002 13-component state output and its checksum. The "
               "result JSON and every counter above are assembled after this timer closed");
    result.add("rhs_calls", rhs_calls);
    // Both routes report a counted number. The direct route counts the fresh
    // provider evaluations it actually made through the same counter the
    // candidate route uses; it is not the RHS count assumed to equal it.
    result.add("native_model_calls",
               s.benchmark_route == "direct" ? evaluator.authoritative_calls()
                                              : evaluator.counts().native_calls);
    result.add("native_model_calls_source",
               s.benchmark_route == "direct"
                   ? "counted fresh DirectAtmosphere evaluations made by this process"
                   : "counted native model evaluations, including cache construction and every "
                     "guard fallback");
    result.add("native_calls_per_rhs", rhs_calls
               ? double(s.benchmark_route == "direct" ? evaluator.authoritative_calls()
                                                       : evaluator.counts().native_calls) /
                     double(rhs_calls)
               : 0.0);
    result.add("native_calls_note",
               "a candidate route can legitimately spend more native evaluations than it has RHS "
               "calls, because a reconstruction may need several fresh nodes per query; the "
               "per-RHS figure is therefore not bounded above by one and is not a saving");
    result.raw("evaluator_statistics", evaluator.statistics());
    result.add("scope", "one process, one cache retained across its contiguous global input range; no interprocess reuse");
    result.add("end_to_end_boundary", "parent group wall includes exec, kernel/weather loading, this work, result publication and MT-002 binary merge");
    return result.str();
}

} // namespace cache_study
