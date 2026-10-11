#define main atmosphere_reference_main
#include "../experiment.cpp"
#undef main
#include "profile.hpp"
#include "SpiceUsr.h"
#include <barrier>
#include <ctime>
#include <sched.h>

namespace AP = atm_profile;
namespace {
inline void consume(double value) { asm volatile("" : : "g"(value) : "memory"); }
double cpu_seconds() {
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts))
        throw std::runtime_error("CPU clock failed");
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
std::string numbers(const std::vector<double> &v) {
    std::ostringstream s;
    s << std::setprecision(17) << '[';
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i)
            s << ',';
        s << v[i];
    }
    s << ']';
    return s.str();
}
std::string stats(std::vector<double> v) {
    if (v.empty())
        return "null";
    Json j;
    auto d = Distribution::of(v);
    std::sort(v.begin(), v.end());
    j.add("count", v.size());
    j.add("min", d.minimum);
    j.add("median", d.median);
    j.add("mean", d.mean);
    j.add("p95", v[std::size_t(std::ceil(.95 * v.size())) - 1]);
    j.add("p99", v[std::size_t(std::ceil(.99 * v.size())) - 1]);
    j.add("max", d.maximum);
    return j.str();
}
std::string records_json(const std::vector<AP::Record> &records) {
    Json stages;
    for (unsigned k = 0; k < AP::count; ++k) {
        std::vector<double> values, calls;
        for (const auto &r : records)
            if (r.calls[k]) {
                values.push_back(r.ns[k]);
                calls.push_back(r.calls[k]);
            }
        if (!values.empty()) {
            Json j;
            j.raw("ns", stats(values));
            j.raw("raw_ns", numbers(values));
            j.raw("calls_per_request", stats(calls));
            stages.raw(std::string(AP::names[k]), j.str());
        }
    }
    std::vector<double> it;
    for (const auto &r : records)
        it.push_back(r.iterations);
    stages.raw("geodetic_iterations", stats(it));
    return stages.str();
}
struct Options {
    std::string out;
    std::size_t samples = 2000, batches = 5;
};
std::pair<Options, Config> options(int argc, char **argv) {
    Options o;
    std::vector<char *> args{argv[0]};
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "--profile-out" || k == "--profile-samples" || k == "--profile-batches") {
            if (++i == argc)
                throw std::invalid_argument("missing profiling option value");
            if (k == "--profile-out")
                o.out = argv[i];
            else if (k == "--profile-samples")
                o.samples = static_cast<std::size_t>(positive(argv[i], "samples"));
            else
                o.batches = static_cast<std::size_t>(positive(argv[i], "batches"));
        } else
            args.push_back(argv[i]);
    }
    if (o.out.empty())
        throw std::invalid_argument("--profile-out required");
    args.push_back(nullptr);
    return {o, parse_arguments(int(args.size() - 1), args.data())};
}
struct Inputs {
    leo::atmosphere::NrlmsiseInput n;
    leo::atmosphere::Jb2008Input j;
};
Inputs prepared(Environment &env, const Config &c, leo::Epoch epoch, leo::InertialVector pos,
                bool history) {
    using namespace leo::atmosphere;
    auto rot = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, c.frame);
    Eigen::Vector3d fixed = rot * pos.as_eigen();
    auto g = geodetic(fixed);
    auto utc = leo::SpiceEphemerisProvider::utc_calendar(epoch);
    Inputs v;
    v.n = env.weather.nrlmsise(utc, history);
    v.n.latitude_deg = g.latitude_rad * 180 / std::numbers::pi;
    v.n.longitude_deg = g.longitude_rad * 180 / std::numbers::pi;
    v.n.altitude_km = g.altitude_m / 1000;
    v.n.local_solar_time_hours =
        std::fmod(utc.seconds_of_day / 3600 + v.n.longitude_deg / 15 + 24, 24);
    v.j = env.weather.jb2008(utc);
    Eigen::Vector3d sun =
        rot * env.ephemeris.geometric_state(leo::sun_naif_id, epoch).position_m.as_eigen();
    v.j.sun_ra_dec_rad = {std::atan2(sun.y(), sun.x()),
                          std::atan2(sun.z(), std::hypot(sun.x(), sun.y()))};
    v.j.position_ra_lat_alt = {g.longitude_rad,
                               std::atan2(fixed.z(), std::hypot(fixed.x(), fixed.y())),
                               g.altitude_m / 1000};
    return v;
}
std::string output(const leo::atmosphere::Result &r) {
    Json j;
    j.add("density", r.density_kg_m3);
    j.add("temperature", r.temperature_k);
    j.add("exospheric_temperature", r.exospheric_temperature_k);
    std::vector<double> n(r.number_density_m3.begin(), r.number_density_m3.end());
    j.raw("constituents", numbers(n));
    return j.str();
}
} // namespace
int main(int argc, char **argv) {
    try {
        cpu_set_t allowed{}, single{};
        if (sched_getaffinity(0, sizeof(allowed), &allowed))
            throw std::runtime_error("get affinity failed");
        CPU_ZERO(&single);
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &allowed)) {
                CPU_SET(cpu, &single);
                break;
            }
        if (sched_setaffinity(0, sizeof(single), &single))
            throw std::runtime_error("set single affinity failed");
        auto [o, c] = options(argc, argv);
        if (std::filesystem::exists(o.out))
            throw std::runtime_error("refusing existing output");
        Environment env(c);
        const auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
        const auto rot = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, c.frame);
        const Eigen::Vector3d fixed =
            leo::atmosphere::earth_fixed({std::numbers::pi / 4, std::numbers::pi / 6, 400000});
        const Eigen::Vector3d inertial = rot.transpose() * fixed;
        const leo::InertialVector pos(inertial.x(), inertial.y(), inertial.z());
        Json root;
        root.add("instrumented_build",
#ifdef ATM_INSTRUMENTED
                 true
#else
                 false
#endif
        );
        root.add("samples_per_batch", o.samples);
        root.add("batches", o.batches);
        root.add("warmup", std::size_t(256));
        root.add("percentiles", "nearest rank; median averages central pair");
        root.add("timer", "std::chrono::steady_clock; nanoseconds");
        std::vector<double> clock_pairs;
        for (unsigned i = 0; i < 20000; ++i) {
            auto a = AP::Clock::now();
            auto b = AP::Clock::now();
            clock_pairs.push_back(AP::elapsed(a, b));
        }
        root.raw("clock_pair_ns", stats(clock_pairs));
        std::vector<double> control, empty_profile;
        for (std::size_t b = 0; b < o.batches; ++b) {
            auto a = AP::Clock::now();
            for (unsigned i = 0; i < 10000; ++i)
                consume(double(i));
            control.push_back(AP::elapsed(a, AP::Clock::now()) / 10000);
            AP::Record r;
            AP::active = &r;
            a = AP::Clock::now();
            for (unsigned i = 0; i < 10000; ++i) {
                AP::Scope s(AP::provider);
                AP::Timer t;
                for (unsigned k = 1; k < AP::count; ++k)
                    t.mark(static_cast<AP::Id>(k));
            }
            empty_profile.push_back(AP::elapsed(a, AP::Clock::now()) / 10000);
            AP::active = nullptr;
        }
        root.raw("empty_call_batched_ns", stats(control));
        root.raw("empty_all_stages_instrumentation_ns", stats(empty_profile));
        Json models;
        for (auto model : {leo::atmosphere::Model::nrlmsise00, leo::atmosphere::Model::jb2008})
            for (bool history : {false, true}) {
                if (history && model == leo::atmosphere::Model::jb2008)
                    continue;
                const std::string name =
                    std::string(model_name(model)) + (history ? "_history" : "");
                leo::atmosphere::DirectAtmosphere provider(model, env.ephemeris, env.weather,
                                                           c.frame, history);
                Json entry;
                auto v = prepared(env, c, epoch, pos, history);
                auto switches = leo::atmosphere::Nrlmsise00::standard_switches();
                if (history)
                    switches[9] = -1;
                leo::atmosphere::Nrlmsise00 n(switches);
                leo::atmosphere::Jb2008 j;
                entry.raw("provider_output", output(provider.evaluate(epoch, pos)));
                entry.raw("native_output",
                          output(model == leo::atmosphere::Model::nrlmsise00 ? n.evaluate(v.n)
                                                                             : j.evaluate(v.j)));
                for (bool enabled : {false, true}) {
                    std::vector<AP::Record> records(o.batches * o.samples);
                    std::vector<double> outer, wall, cpu;
                    outer.reserve(records.size());
                    for (std::size_t b = 0; b < o.batches; ++b) {
                        AP::active = nullptr;
                        for (unsigned k = 0; k < 256; ++k)
                            consume(provider.evaluate(epoch, pos).density_kg_m3);
                        auto cb = cpu_seconds();
                        auto begin = AP::Clock::now();
                        for (std::size_t k = 0; k < o.samples; ++k) {
                            AP::active = enabled ? &records[b * o.samples + k] : nullptr;
                            auto a = AP::Clock::now();
                            auto result = provider.evaluate(epoch, pos);
                            auto z = AP::Clock::now();
                            AP::active = nullptr;
                            outer.push_back(AP::elapsed(a, z));
                            consume(result.density_kg_m3);
                        }
                        wall.push_back(AP::elapsed(begin, AP::Clock::now()) / 1e9);
                        cpu.push_back(cpu_seconds() - cb);
                    }
                    Json run;
                    run.raw("outer_ns", stats(outer));
                    run.raw("raw_outer_ns", numbers(outer));
                    run.raw("batch_wall_s", numbers(wall));
                    run.raw("batch_cpu_s", numbers(cpu));
                    if (enabled)
                        run.raw("stages", records_json(records));
                    entry.raw(enabled ? "active" : "disabled", run.str());
                }
                models.raw(name, entry.str());
            }
        root.raw("models", models.str());
        // Independent prepared-input batches: no diagnostic timers enabled.
        AP::active = nullptr;
        Json micro;
        const auto v = prepared(env, c, epoch, pos, false);
        auto u = leo::SpiceEphemerisProvider::utc_calendar(epoch);
        leo::atmosphere::Nrlmsise00 n;
        leo::atmosphere::Jb2008 j;
        std::vector<std::pair<std::string, std::function<double(unsigned)>>> ops;
        ops.emplace_back("empty", [](unsigned i) {
            consume(double(i));
            return double(i);
        });
        ops.emplace_back("input_validation", [&](unsigned) {
            asm volatile("" ::: "memory");
            return std::isfinite(epoch.et_seconds) && pos.as_eigen().allFinite();
        });
        ops.emplace_back("matrix_vector", [&](unsigned) {
            asm volatile("" ::: "memory");
            Eigen::Vector3d r = rot * pos.as_eigen();
            consume(r.y());
            consume(r.z());
            return r.x();
        });
        for (auto [lat, label] :
             std::vector<std::pair<double, std::string>>{{0, "equator"},
                                                         {.7853981633974483, "midlatitude"},
                                                         {1.5707788735023767, "near_pole"},
                                                         {std::numbers::pi / 2, "pole"}}) {
            Eigen::Vector3d r = leo::atmosphere::earth_fixed({lat, .5, 400000});
            if (label == "pole")
                r = {0, 0, leo::atmosphere::wgs84_a_m * (1 - leo::atmosphere::wgs84_f) + 400000};
            ops.emplace_back("geodetic_" + label, [r](unsigned) {
                asm volatile("" ::: "memory");
                const auto g = leo::atmosphere::geodetic(r);
                consume(g.longitude_rad);
                consume(g.latitude_rad);
                return g.altitude_m;
            });
            AP::Record record;
            AP::active = &record;
            leo::atmosphere::geodetic(r);
            AP::active = nullptr;
            Json details;
            details.add("iterations", std::size_t(record.iterations));
            details.raw("stages", records_json({record}));
            root.raw("geodetic_case_" + label, details.str());
        }
        ops.emplace_back("weather_daily", [&](unsigned) {
            auto in = env.weather.nrlmsise(u, false);
            consume(in.f107_average);
            return in.ap;
        });
        ops.emplace_back("weather_history", [&](unsigned) {
            auto in = env.weather.nrlmsise(u, true);
            for (auto x : in.ap_history)
                consume(x);
            return in.ap;
        });
        ops.emplace_back("weather_jb2008", [&](unsigned) {
            auto in = env.weather.jb2008(u);
            consume(in.f10);
            return in.dstdtc_k;
        });
        ops.emplace_back("utc_wrapper", [&](unsigned) {
            return leo::SpiceEphemerisProvider::utc_calendar(epoch).mjd;
        });
        ops.emplace_back("rotation_wrapper", [&](unsigned) {
            auto r = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, c.frame);
            return r(0, 0);
        });
        ops.emplace_back("sun_wrapper", [&](unsigned) {
            return env.ephemeris.geometric_state(leo::sun_naif_id, epoch).position_m.as_eigen().x();
        });
        ops.emplace_back("native_nrlmsise00",
                         [&](unsigned) { return n.evaluate(v.n).density_kg_m3; });
        ops.emplace_back("native_jb2008", [&](unsigned) { return j.evaluate(v.j).density_kg_m3; });
        SpiceChar action[] = "RETURN";
        reset_c();
        erract_c("SET", 0, action);
        const std::string midnight = "2003-10-30T00:00:00";
        const std::string next = "2003-10-31T00:00:00";
        ops.emplace_back("raw_pxform", [&](unsigned) {
            SpiceDouble r[3][3]{};
            pxform_c("J2000", c.frame.c_str(), epoch.et_seconds, r);
            return r[0][0];
        });
        ops.emplace_back("raw_et2utc", [&](unsigned) {
            char text[64]{};
            et2utc_c(epoch.et_seconds, "ISOC", 9, sizeof(text), text);
            consume(text[18]);
            return text[0];
        });
        ops.emplace_back("raw_str2et_first", [&](unsigned) {
            double et = 0;
            str2et_c(midnight.c_str(), &et);
            return et;
        });
        ops.emplace_back("raw_str2et_second", [&](unsigned) {
            double et = 0;
            str2et_c(next.c_str(), &et);
            return et;
        });
        ops.emplace_back("raw_spkez", [&](unsigned) {
            double s[6]{}, lt = 0;
            spkez_c(leo::sun_naif_id, epoch.et_seconds, "J2000", "NONE", leo::earth_naif_id, s,
                    &lt);
            return s[0];
        });
        ops.emplace_back("spice_error_prepare", [&](unsigned) {
            reset_c();
            erract_c("SET", 0, action);
            return failed_c();
        });
        for (auto &[name, op] : ops) {
            std::vector<double> times;
            const unsigned inner =
                name == "empty" || name == "input_validation" || name == "matrix_vector" ? 1024
                                                                                         : 32;
            for (std::size_t b = 0; b < o.batches; ++b) {
                for (unsigned k = 0; k < 256; ++k)
                    consume(op(k));
                for (unsigned sample = 0; sample < 100; ++sample) {
                    auto a = AP::Clock::now();
                    for (unsigned k = 0; k < inner; ++k)
                        consume(op(k));
                    times.push_back(AP::elapsed(a, AP::Clock::now()) / inner);
                }
            }
            if (failed_c())
                throw std::runtime_error("raw CSPICE operation failed");
            Json row;
            row.raw("ns_per_operation", stats(times));
            row.raw("raw_batch_ns_per_operation", numbers(times));
            row.add("operations_per_sample", std::size_t(inner));
            row.add("samples_per_batch", std::size_t(100));
            micro.raw(name, row.str());
        }
        root.raw("microbenchmarks", micro.str());
        if (sched_setaffinity(0, sizeof(allowed), &allowed))
            throw std::runtime_error("restore concurrency affinity failed");
        Json concurrent;
        for (auto model : {leo::atmosphere::Model::nrlmsise00, leo::atmosphere::Model::jb2008})
            for (bool changing : {false, true})
                for (unsigned threads : {1, 2, 4}) {
                    leo::atmosphere::DirectAtmosphere provider(model, env.ephemeris, env.weather,
                                                               c.frame, false);
                    std::vector<double> walls, cpus, latencies;
                    std::vector<AP::Record> all;
                    for (std::size_t batch = 0; batch < o.batches; ++batch) {
                        std::barrier ready(int(threads) + 1), start(int(threads) + 1),
                            finish(int(threads) + 1);
                        std::vector<std::vector<AP::Record>> rs(threads,
                                                                std::vector<AP::Record>(o.samples));
                        std::vector<std::vector<double>> ts(threads,
                                                            std::vector<double>(o.samples));
                        std::vector<std::thread> workers;
                        for (unsigned tid = 0; tid < threads; ++tid)
                            workers.emplace_back([&, tid] {
                                AP::active = nullptr;
                                for (unsigned i = 0; i < 256; ++i)
                                    consume(provider.evaluate(epoch, pos).density_kg_m3);
                                ready.arrive_and_wait();
                                start.arrive_and_wait();
                                for (std::size_t k = 0; k < o.samples; ++k) {
                                    AP::active = &rs[tid][k];
                                    auto a = AP::Clock::now();
                                    auto result = provider.evaluate(
                                        {epoch.et_seconds +
                                         (changing ? (double(k) * .01 + tid * .001) : 0)},
                                        pos);
                                    auto z = AP::Clock::now();
                                    AP::active = nullptr;
                                    ts[tid][k] = AP::elapsed(a, z);
                                    consume(result.density_kg_m3);
                                }
                                finish.arrive_and_wait();
                            });
                        ready.arrive_and_wait();
                        auto cb = cpu_seconds();
                        auto begin = AP::Clock::now();
                        start.arrive_and_wait();
                        finish.arrive_and_wait();
                        walls.push_back(AP::elapsed(begin, AP::Clock::now()) / 1e9);
                        cpus.push_back(cpu_seconds() - cb);
                        for (auto &t : workers)
                            t.join();
                        for (unsigned tid = 0; tid < threads; ++tid) {
                            all.insert(all.end(), rs[tid].begin(), rs[tid].end());
                            latencies.insert(latencies.end(), ts[tid].begin(), ts[tid].end());
                        }
                    }
                    Json row;
                    row.add("threads", std::size_t(threads));
                    row.add("calls_per_thread_per_batch", o.samples);
                    row.add("changing_epochs", changing);
                    row.raw("batch_wall_s", numbers(walls));
                    row.raw("batch_cpu_s", numbers(cpus));
                    std::vector<double> throughput;
                    for (auto w : walls)
                        throughput.push_back(threads * o.samples / w);
                    row.raw("throughput_calls_s", stats(throughput));
                    row.raw("latency_ns", stats(latencies));
                    row.raw("stages", records_json(all));
                    concurrent.raw(std::string(model_name(model)) +
                                       (changing ? "_changing_" : "_fixed_") +
                                       std::to_string(threads),
                                   row.str());
                }
        root.raw("concurrency", concurrent.str());
        save(o.out, root.str());
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
