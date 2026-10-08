#!/usr/bin/env python3
"""Deterministic CSV fixtures exercise the real headless CLI and data boundaries."""
import csv
import importlib.util
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "plotting/plot_analysis.py"
spec = importlib.util.spec_from_file_location("plot_analysis", SCRIPT)
plot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plot)


def write_table(path, header, rows):
    with Path(path).open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def fixture(directory, surfaces=2, power=.001):
    directory = Path(directory); directory.mkdir()
    write_table(directory / "metadata.csv", ["key", "value"],
                [("schema_version", 1), ("earth_equatorial_radius_m", 6378136.3),
                 ("initial_epoch_et_seconds", 0), ("surface_count", surfaces)])
    times = [i * 100. for i in range(101)]
    rows = {name: [] for name in plot.SCHEMAS}
    radius = 7e6; mu = 3.986004418e14; rate = math.sqrt(mu / radius**3)
    for i, t in enumerate(times):
        theta = rate * t; speed = radius * rate
        for name, schema in plot.SCHEMAS.items():
            if name in ("surfaces", "per_orbit", "apsides"):
                continue
            row = dict.fromkeys(schema.split(","), 0.)
            row["time_s"] = t
            if name == "trajectory":
                row.update(x_m=radius*math.cos(theta), y_m=radius*math.sin(theta),
                           vx_m_s=-speed*math.sin(theta), vy_m_s=speed*math.cos(theta), qw=1,
                           omega_z_rad_s=.001)
            elif name == "orbital_elements":
                row.update(radius_m=radius, altitude_m=radius-6378136.3, a_m=radius, e=0, i_rad=0,
                           periapsis_radius_m=radius, apoapsis_radius_m=radius,
                           periapsis_altitude_m=radius-6378136.3, apoapsis_altitude_m=radius-6378136.3,
                           specific_energy_j_kg=-mu/(2*radius), h_z_m2_s=radius*speed,
                           true_anomaly_rad=theta%(2*math.pi), true_anomaly_unwrapped_rad=theta,
                           raan_unwrapped_rad=-1e-7*t, raan_rad=(-1e-7*t)%(2*math.pi),
                           specific_kinetic_energy_j_kg=mu/(2*radius),specific_potential_energy_j_kg=-mu/radius)
            elif name == "attitude":
                row.update(angular_speed_rad_s=.001, rotational_energy_j=.000002,
                           inertial_momentum_kg_m2_s=.004)
            elif name == "environment":
                row.update(illumination=0 if i%20<5 else 1, density_kg_m3=1e-12, drag_model=2,
                           srp_force_x_n=-1e-5, srp_torque_z_nm=1e-6, drag_force_y_n=-1e-6,
                           point_mass_x_m_s2=-mu/radius**2)
            elif name == "work":
                row.update(srp_power_w=power, cumulative_srp_work_j=power*t,
                           specific_srp_work_j=power*t/10, orbital_energy_change_j=power*t,
                           specific_energy_change_j_kg=power*t/10, srp_force_t_n=power/speed,
                           srp_power_t_w=power, cumulative_work_t_j=power*t,
                           sun_inbound_transverse_work_j=power*t/2,
                           sun_outbound_transverse_work_j=power*t/2)
            rows[name].append(list(row.values()))
        for index in range(surfaces):
            rows["surfaces"].append([t,index,1 if index==0 else -1,0,0,0 if index==0 else math.pi,
                                     1 if index==0 else 0,.5,math.pi/2])
    schema = plot.PER_ORBIT.split(",")
    for index, start, end, complete in [(0,0,6000,1),(1,6000,10000,0)]:
        row = dict.fromkeys(schema,0.)
        row.update(orbit_index=index,start_time_s=start,end_time_s=end,complete=complete,
                   period_s=end-start if complete else 0,start_a_m=radius,start_e=0,start_i_rad=0,
                   start_raan_unwrapped_rad=-start*1e-7,end_raan_unwrapped_rad=-end*1e-7,
                   delta_raan_rad=-(end-start)*1e-7,srp_work_j=power*(end-start),
                   specific_srp_work_j=power*(end-start)/10,srp_work_t_j=power*(end-start),
                   orbital_energy_change_j=power*(end-start))
        rows["per_orbit"].append(list(row.values()))
    # Circular orbits have no geometric apsides: the header-only file is valid.
    for name, schema in plot.SCHEMAS.items():
        write_table(directory / f"{name}.csv",schema.split(","),rows[name])
    return directory


class PlotSmoke(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="leo-plots-")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def cli(self, *args, success=True):
        env = dict(os.environ); env.pop("DISPLAY", None); env["MPLBACKEND"]="Agg"
        result = subprocess.run([sys.executable,str(SCRIPT),*[str(arg) for arg in args]],
                                capture_output=True,text=True,env=env,timeout=180)
        if success:
            self.assertEqual(result.returncode,0,result.stderr)
        else:
            self.assertNotEqual(result.returncode,0)
            self.assertIn("error:",result.stderr)
        return result

    def test_all_plots_render_png_svg_headlessly(self):
        data = fixture(self.root/"run"); output=self.root/"plots"
        self.cli(data,"--output",output,"--format","both")
        names = {"orbit_3d","altitude_vs_time","orbital_elements","orbital_angles_unwrapped",
                 "apsides_evolution","attitude_quaternion","attitude_angular_velocity",
                 "attitude_rotational_state","attitude_relative_euler","environment_illumination_density",
                 "environment_drag","environment_total_acceleration","srp_force","srp_power",
                 "srp_force_rtn","srp_cumulative_work","srp_transverse_work",
                 "environment_force_contributions","environment_torque_contributions","gravity_acceleration",
                 "orbital_energy_vs_srp_work","surface_incidence","surface_projected_area",
                 "per_orbit_elements","srp_work_per_orbit","orbital_energy"}
        self.assertEqual({path.name for path in output.iterdir()},
                         {name+"."+extension for name in names for extension in ("png","svg")})
        for name in names:
            self.assertEqual((output/(name+".png")).read_bytes()[:8],b"\x89PNG\r\n\x1a\n")
            self.assertIn("<svg",(output/(name+".svg")).read_text())

    def test_zero_surfaces_and_single_sample_exports_are_valid(self):
        data=fixture(self.root/"run",surfaces=0)
        for name in ("trajectory","orbital_elements","attitude","environment","work"):
            path=data/(name+".csv")
            with path.open(newline="") as handle: rows=list(csv.reader(handle))
            write_table(path,rows[0],rows[1:2])
        write_table(data/"per_orbit.csv",plot.PER_ORBIT.split(","),[])
        self.cli(data,"--output",self.root/"plots","--only","orbit_3d")
        self.assertTrue((self.root/"plots/orbit_3d.png").is_file())

    def test_malformed_and_truncated_csv_fail_with_file_context(self):
        data=fixture(self.root/"run")
        path=data/"work.csv"
        with path.open("a") as handle: handle.write("not,a,complete,row\n")
        result=self.cli(data,"--output",self.root/"bad",success=False)
        self.assertIn("work.csv",result.stderr)
        data2=fixture(self.root/"short")
        path=data2/"attitude.csv"
        with path.open(newline="") as handle: rows=list(csv.reader(handle))
        write_table(path,rows[0],rows[1:-1])
        result=self.cli(data2,"--output",self.root/"bad",success=False)
        self.assertIn("attitude.csv",result.stderr)

    def test_surface_missing_duplicate_and_invalid_indices_are_rejected(self):
        for mode in ("missing","duplicate","fractional"):
            data=fixture(self.root/mode); path=data/"surfaces.csv"
            with path.open(newline="") as handle: rows=list(csv.reader(handle))
            if mode=="missing": rows.pop(3)
            elif mode=="duplicate": rows.insert(3,rows[1])
            else: rows[1][1]=".5"
            write_table(path,rows[0],rows[1:])
            result=self.cli(data,"--output",self.root/"bad",success=False)
            self.assertIn("surfaces.csv",result.stderr)

    def test_sweep_reduction_uses_actual_times_first_maximum_and_noon_offset(self):
        for i in range(4):
            run=self.root/f"run{i}"; run.mkdir()
            write_table(run/"orbital_elements.csv",["time_s","a_m"],[[0,7e6],[7.25,7e6+100+i],[99.5,7e6+100+i],[200.1,7e6]])
        manifest=self.root/"manifest.csv"
        write_table(manifest,["data_dir","raan_deg","pitch_deg"],
                    [["run0",0,0],["run1",90,0],["run2",0,45],["run3",90,45]])
        summary=self.root/"summary.csv"
        self.cli("reduce",manifest,"--output",summary,"--sun-ra-deg",90)
        with summary.open(newline="") as handle: rows=list(csv.DictReader(handle))
        self.assertEqual([float(row["time_to_max_s"]) for row in rows],[7.25]*4)
        self.assertEqual([float(row["max_delta_a_m"]) for row in rows],[100,101,102,103])
        self.assertEqual([float(row["ltan_h"]) for row in rows],[6,12,6,12])
        self.cli("heatmap",summary,"--x","ltan_h","--y","pitch_deg","--value","max_delta_a_m","--output",self.root/"gain.svg")
        self.assertIn("<svg",(self.root/"gain.svg").read_text())
        write_table(manifest,["data_dir","raan_deg","pitch_deg","ltan_h"],[["run0",0,0,23]])
        self.cli("reduce",manifest,"--output",summary)
        with summary.open(newline="") as handle: row=next(csv.DictReader(handle))
        self.assertEqual(float(row["ltan_h"]),23)

    def test_heatmap_holes_duplicates_and_log_boundaries(self):
        table=self.root/"sweep.csv"
        write_table(table,["x","y","value"],[[0,0,1],[1,0,2],[0,1,3]])
        args=("heatmap",table,"--x","x","--y","y","--value","value","--output",self.root/"grid.png")
        self.cli(*args,success=False)
        self.cli(*args,"--allow-missing")
        write_table(table,["x","y","value"],[[0,0,1],[0,0,2]])
        self.cli(*args,success=False)
        write_table(table,["x","y","value"],[[0,0,0]])
        self.cli(*args,"--log-scale",success=False)

    def test_comparison_and_supplied_series_commands(self):
        one=fixture(self.root/"on"); two=fixture(self.root/"off",power=0)
        self.cli("compare",one,two,"--output",self.root/"compare")
        self.assertTrue((self.root/"compare/compare_cumulative_srp_work.png").is_file())
        self.assertTrue((self.root/"compare/compare_raan.png").is_file())
        supplied=self.root/"thermal.csv"
        write_table(supplied,["time_s","temperature_k"],[[0,300],[1,301],[2,299]])
        self.cli("series",supplied,"--x","time_s","--y","temperature_k","--output",self.root/"thermal.png")
        self.assertEqual((self.root/"thermal.png").read_bytes()[:8],b"\x89PNG\r\n\x1a\n")


if __name__=="__main__":
    unittest.main()
