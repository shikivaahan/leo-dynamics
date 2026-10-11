// Supplemental lock-isolation experiments; no production source edits.
#define main atmosphere_reference_main
#include "../experiment.cpp"
#undef main
#include "profile.hpp"
#include <barrier>
#include <ctime>
namespace AP = atm_profile;
namespace {
double process_cpu() {
    timespec t{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t))
        throw std::runtime_error("CPU clock failed");
    return t.tv_sec + t.tv_nsec * 1e-9;
}
void sink(double x) { asm volatile("" ::"g"(x) : "memory"); }
std::string array_json(const std::vector<double> &v) {
    std::ostringstream o;
    o << std::setprecision(17) << '[';
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i)
            o << ',';
        o << v[i];
    }
    return o.str() + ']';
}
std::string distribution(const std::vector<double> &v) {
    if (v.empty())
        return "null";
    auto s = v;
    std::sort(s.begin(), s.end());
    Json j;
    j.add("mean", std::accumulate(s.begin(), s.end(), 0.0) / s.size());
    j.add("median", s.size() % 2 ? s[s.size() / 2] : (s[s.size() / 2 - 1] + s[s.size() / 2]) * .5);
    j.add("min", s.front());
    j.add("max", s.back());
    j.add("p95", s[std::size_t(std::ceil(.95 * s.size())) - 1]);
    j.add("p99", s[std::size_t(std::ceil(.99 * s.size())) - 1]);
    j.add("count", s.size());
    return j.str();
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::string out;
        std::vector<char *> args{argv[0]};
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--profile-out") {
                if (++i == argc)
                    throw std::invalid_argument("missing output");
                out = argv[i];
            } else
                args.push_back(argv[i]);
        }
        if (out.empty() || std::filesystem::exists(out))
            throw std::invalid_argument("new --profile-out required");
        auto c = parse_arguments(int(args.size()), args.data());
        Environment env(c);
        auto epoch = leo::SpiceEphemerisProvider::epoch_from_utc(c.epoch_utc);
        auto utc = leo::SpiceEphemerisProvider::utc_calendar(epoch);
        auto rot = leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, c.frame);
        Eigen::Vector3d fixed =
            leo::atmosphere::earth_fixed({std::numbers::pi / 4, std::numbers::pi / 6, 400000});
        Eigen::Vector3d inertial = rot.transpose() * fixed;
        InertialVector pos{inertial};
        auto g = leo::atmosphere::geodetic(fixed);
        auto ni = env.weather.nrlmsise(utc, false);
        ni.latitude_deg = g.latitude_rad * 180 / std::numbers::pi;
        ni.longitude_deg = g.longitude_rad * 180 / std::numbers::pi;
        ni.altitude_km = g.altitude_m / 1000;
        ni.local_solar_time_hours =
            std::fmod(utc.seconds_of_day / 3600 + ni.longitude_deg / 15 + 24, 24);
        auto ji = env.weather.jb2008(utc);
        Eigen::Vector3d sun =
            rot * env.ephemeris.geometric_state(leo::sun_naif_id, epoch).position_m.as_eigen();
        ji.sun_ra_dec_rad = {std::atan2(sun.y(), sun.x()),
                             std::atan2(sun.z(), std::hypot(sun.x(), sun.y()))};
        ji.position_ra_lat_alt = {g.longitude_rad,
                                  std::atan2(fixed.z(), std::hypot(fixed.x(), fixed.y())),
                                  g.altitude_m / 1000};
        leo::atmosphere::Nrlmsise00 n;
        leo::atmosphere::Jb2008 j;
        leo::atmosphere::DirectAtmosphere np(leo::atmosphere::Model::nrlmsise00, env.ephemeris,
                                             env.weather, c.frame, false),
            jp(leo::atmosphere::Model::jb2008, env.ephemeris, env.weather, c.frame, false);
        Json root;
        root.add("instrumented_build",
#ifdef ATM_INSTRUMENTED
                 true
#else
                 false
#endif
        );
        root.add("samples_per_thread", std::size_t(2000));
        root.add("batches", std::size_t(5));
        root.add("warmup", std::size_t(256));
        root.add("note", "prepared fixed inputs; 1/2/4 workers; wall excludes warmup; process CPU; "
                         "mutex intervals include acquisition overhead and wait; raw samples ns");
        Json cases;
        for (const std::string kind :
             {"native_nrlmsise00", "native_jb2008", "spice_rotation", "spice_utc", "spice_sun",
              "rhs_environment_nrlmsise00", "rhs_environment_jb2008"})
            for (unsigned threads : {1, 2, 4}) {
                auto operation = [&]() -> double {
                    if (kind == "native_nrlmsise00")
                        return n.evaluate(ni).density_kg_m3;
                    if (kind == "native_jb2008")
                        return j.evaluate(ji).density_kg_m3;
                    if (kind == "spice_rotation")
                        return leo::SpiceEphemerisProvider::earth_fixed_rotation(epoch, c.frame)(0,
                                                                                                 0);
                    if (kind == "spice_utc")
                        return leo::SpiceEphemerisProvider::utc_calendar(epoch).mjd;
                    if (kind == "spice_sun")
                        return env.ephemeris.geometric_state(leo::sun_naif_id, epoch)
                            .position_m.x();
                    auto ss = env.ephemeris.geometric_state(leo::sun_naif_id, epoch);
                    sink(ss.position_m.x());
                    return (kind == "rhs_environment_nrlmsise00" ? np : jp)
                        .evaluate(epoch, pos)
                        .density_kg_m3;
                };
                std::vector<double> walls, cpus, throughput, latencies;
                std::array<std::vector<double>, AP::count> stages;
                std::vector<std::string> hashes;
                for (unsigned batch = 0; batch < 5; ++batch) {
                    std::barrier ready(int(threads) + 1), start(int(threads) + 1),
                        finish(int(threads) + 1);
                    std::vector<std::vector<AP::Record>> rows(threads,
                                                              std::vector<AP::Record>(2000));
                    std::vector<std::vector<double>> times(threads, std::vector<double>(2000));
                    std::vector<Checksum> checks(threads);
                    std::vector<std::thread> workers;
                    for (unsigned tid = 0; tid < threads; ++tid)
                        workers.emplace_back([&, tid] {
                            for (unsigned k = 0; k < 256; ++k)
                                sink(operation());
                            ready.arrive_and_wait();
                            start.arrive_and_wait();
                            for (unsigned k = 0; k < 2000; ++k) {
                                AP::active = &rows[tid][k];
                                auto a = AP::Clock::now();
                                double result = operation();
                                auto z = AP::Clock::now();
                                AP::active = nullptr;
                                times[tid][k] = AP::elapsed(a, z);
                                checks[tid].add(result);
                                sink(result);
                            }
                            finish.arrive_and_wait();
                        });
                    ready.arrive_and_wait();
                    auto cb = process_cpu();
                    auto a = AP::Clock::now();
                    start.arrive_and_wait();
                    finish.arrive_and_wait();
                    double wall = AP::elapsed(a, AP::Clock::now()) / 1e9;
                    double cpu = process_cpu() - cb;
                    for (auto &t : workers)
                        t.join();
                    walls.push_back(wall);
                    cpus.push_back(cpu);
                    throughput.push_back(threads * 2000 / wall);
                    for (unsigned tid = 0; tid < threads; ++tid) {
                        hashes.push_back(checks[tid].str());
                        latencies.insert(latencies.end(), times[tid].begin(), times[tid].end());
                        for (auto &r : rows[tid])
                            for (unsigned id = 0; id < AP::count; ++id)
                                if (r.calls[id])
                                    stages[id].push_back(r.ns[id]);
                    }
                }
                Json row;
                row.add("threads", std::size_t(threads));
                row.raw("wall_s", array_json(walls));
                row.raw("cpu_s", array_json(cpus));
                row.raw("throughput_calls_s", distribution(throughput));
                row.raw("latency_ns", distribution(latencies));
                row.raw("raw_latency_ns", array_json(latencies));
                Json timing;
                for (unsigned id = 0; id < AP::count; ++id)
                    if (!stages[id].empty()) {
                        Json stage;
                        stage.raw("ns", distribution(stages[id]));
                        stage.raw("raw_ns", array_json(stages[id]));
                        timing.raw(std::string(AP::names[id]), stage.str());
                    }
                row.raw("stages", timing.str());
                std::ostringstream hs;
                hs << '[';
                for (std::size_t k = 0; k < hashes.size(); ++k) {
                    if (k)
                        hs << ',';
                    hs << '"' << hashes[k] << '"';
                }
                hs << ']';
                row.raw("output_hashes", hs.str());
                cases.raw(kind + "_" + std::to_string(threads), row.str());
            }
        root.raw("cases", cases.str());
        save(out, root.str());
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
