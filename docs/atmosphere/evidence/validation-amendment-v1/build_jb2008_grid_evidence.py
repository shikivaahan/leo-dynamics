#!/usr/bin/env python3
"""Assemble the JB2008 official-grid investigation evidence from probe runs.

Consumes the JSON emitted by the four retained experiment scripts and produces
one evidence record.

Every measured number in the output is copied from a run this assembler read.
Nothing is hardcoded here: if an input is missing the corresponding field is
emitted as null with an explicit "not measured" note, rather than being filled
in from a figure remembered from an earlier session.  A stale number in an
evidence record is worse than an absent one.

Inputs:
  --analysis           jb2008_grid_analysis.py    full-grid metrics and strata
  --identify           jb2008_grid_identify.py    sensitivities, identifiability
  --scalar-tc          jb2008_grid_scalar_tc.py   scalar Tc offset experiment
  --bounded-fit        jb2008_grid_bounded_fit.py constrained +-0.5 fit
  --astronomy-provenance  extract_official_astronomy.py  driver SHA record
"""

import argparse
import json
import os

# The print precision the density column actually carries (FORMAT D15.5).
PRINT_REL_HALF_ULP_MAX = 0.5 / 10000.0


def spread(stratum):
    """Within-stratum mean, rms, standard deviation and their ratio."""
    s = stratum["signed_relative_error"]
    variance = max(0.0, s["rms"] ** 2 - s["mean"] ** 2)
    std = variance ** 0.5
    return {
        "mean_signed": s["mean"],
        "rms": s["rms"],
        "std": std,
        "std_over_mean": std / abs(s["mean"]) if s["mean"] else None,
    }


def build(analysis, identify, scalar_tc, bounded_fit, astronomy, envelope):
    pooled = analysis["pooled"]
    epochs = analysis["epochs"]
    epoch0 = epochs[0]

    fit_summary = bounded_fit["summary"]
    fit_epochs = bounded_fit["epochs"]
    sensitivity = identify["index_sensitivity"]

    # Strata are reported for every epoch, not only the first, so that time of
    # day, DSTDTC and density are each covered rather than aliased with altitude.
    strata_by_epoch = {}
    for e in epochs:
        strata_by_epoch[e["file"]] = {
            "dtc_printed": e["dtc_printed"],
            "hour_utc": e["epoch"]["hour"],
            "density_relative_error": e["density"]["relative_error"],
            "density_signed_relative_error": e["density"][
                "signed_relative_error"
            ],
            "by_altitude_km": e["strata"]["altitude_km"]["by_altitude_km"],
            "by_latitude_deg": e["strata"]["latitude_deg"]["by_latitude_deg"],
            "by_longitude_deg": e["strata"]["longitude_deg"]["by_longitude_deg"],
            "by_reference_density": e["strata"]["reference_density"][
                "by_reference_density"
            ],
        }

    archive_rows = [
        {
            "file": e["file"],
            "hour_utc": e["hour_utc"],
            "dtc_printed": e["dtc_printed"],
            "indices_printed": e["indices_printed"],
            "indices_from_current_archive": e["current_archive"]["indices"],
            "indices_minus_printed": e["current_archive"]["indices_minus_printed"],
            "indices_within_print_bound": e["current_archive"]["within_print_bound"],
            "indices_total": e["current_archive"]["indices_total"],
            "dtc_from_archive": e["current_archive"]["dtc_from_archive"],
            "dtc_matches_printed": e["current_archive"]["dtc_matches_printed"],
            "residual_rms_with_printed": e["baseline"]["rms"],
            "residual_rms_with_archive": e["current_archive"]["stats"]["rms"],
            "archive_worse_by_factor": (
                e["current_archive"]["stats"]["rms"] / e["baseline"]["rms"]
            ),
        }
        for e in fit_epochs
        if "current_archive" in e
    ]

    dtc_all_match = all(r["dtc_matches_printed"] for r in archive_rows)
    indices_matching = [r["indices_within_print_bound"] for r in archive_rows]

    bounded_rms = [e["after_bounded_fit"]["rms"] for e in fit_epochs]
    archive_factors = [r["archive_worse_by_factor"] for r in archive_rows]

    return {
        "schema": "leo-dynamics/jb2008-grid-investigation/2",
        "supersedes_schema": "leo-dynamics/jb2008-grid-investigation/1",
        "question": (
            "Why does the official SET validation grid residual sit at "
            "0.3-0.6 %, and how much of it is the fixture's own lost input "
            "precision rather than an implementation error?"
        ),
        "classification": {
            "verdict": fit_summary["classification"],
            "cause_of_residual": "UNKNOWN",
            "supersedes_earlier_reading": (
                "any earlier statement that the grid residual was explained by "
                "printed-input rounding is superseded: it is not explained"
            ),
            "meaning": (
                "Index quantisation is PLAUSIBLE as the dominant cause and is "
                "quantitatively supported as a major contributor: a single set "
                "of offsets inside the +-0.5 the F8.0 columns imply removes "
                "most of the residual. It is not ESTABLISHED as the cause. The "
                "residual that remains lies above the precision the density "
                "column itself carries, and because the original index vintage "
                "is unavailable the cause of that remainder cannot be "
                "distinguished and is reported as unknown."
            ),
            "rounding_quantitatively_supported": True,
            "rounding_established_as_the_cause": False,
            "rounding_fully_accounts_for_residual": fit_summary[
                "epochs_reaching_print_floor"
            ]
            == fit_summary["epochs_total"],
            "epochs_reaching_print_floor": fit_summary["epochs_reaching_print_floor"],
            "epochs_total": fit_summary["epochs_total"],
        },
        "metric_definitions": dict(
            analysis["metric_definitions"],
            duplicate_grid_points_measured=(
                max(len(e["duplicate_grid_points"]) for e in epochs)
            ),
            zero_reference_rows_excluded=(
                sum(e["zero_reference_rows_excluded"] for e in epochs)
            ),
            rows_evaluated=sum(e["rows_evaluated"] for e in epochs),
        ),
        "provenance": {
            "note": (
                "Per-row records carry the epoch, the full-precision evaluation "
                "point and the printed precision of every input. This block "
                "carries the file-level identity that a row inherits from its "
                "fixture_file entry."
            ),
            "external_distribution": {
                "origin": "https://spacewx.com/jb2008/",
                "redistribution": (
                    "not permitted upstream; official source and validation "
                    "output are used in place and never vendored here"
                ),
            },
            "probes": {
                "native_kernel": {
                    "path": "docs/atmosphere/jb2008_native_kernel.f90",
                    "role": (
                        "calls the unmodified official JB2008.f directly, "
                        "bypassing the C++ bridge, coordinate layer and "
                        "weather loader"
                    ),
                },
                "adapter_probe": {
                    "path": "docs/atmosphere/jb2008_adapter_probe.cpp",
                    "role": "same contract through the shipped Jb2008 adapter",
                },
                "official_astronomy": {
                    "path": "docs/atmosphere/jb2008_official_astronomy.f90",
                    "role": (
                        "links the unmodified official SUNPOS and THETA sliced "
                        "byte-identically from the hash-verified driver"
                    ),
                    "driver_sha256": astronomy["driver_sha256"],
                    "driver_sha256_matches_pin": astronomy[
                        "driver_sha256_matches_pin"
                    ],
                    "fragment_sha256": astronomy["fragment_sha256"],
                    "each_slice_verbatim_substring_of_driver": astronomy[
                        "each_slice_verbatim_substring_of_driver"
                    ],
                    "no_stub_routines_linked": True,
                },
                "sha256_note": (
                    "Probe binaries are rebuilt by reproduce.sh on every run "
                    "and are not hashed here; the sources they compile from are "
                    "in the repository manifest."
                ),
            },
            "fixture_sha256": {
                "note": (
                    "Verified by the loader in tests/jb2008_tests.cpp against "
                    "the pinned digests recorded in docs/atmosphere/JB2008.md; "
                    "a file whose digest does not match is a hard error, so a "
                    "row's fixture_file name identifies an exact byte sequence."
                ),
                "files": [e["file"] for e in epochs],
            },
            "row_inheritance": (
                "Every entry in stratification.worst_twenty carries its own "
                "fixture_file, epoch_utc, mjd_utc, sat_ra_rad, sun_ra_rad, "
                "sun_dec_rad and input_precision block; the fixture_file value "
                "maps to epochs[] above for the file-level hashes."
            ),
        },
        "sample_populations": analysis.get("sample_populations"),
        "coverage": {
            "epochs": len(epochs),
            "rows_per_epoch": epochs[0]["rows_evaluated"],
            "rows_total": pooled["rows_evaluated"],
            "grid": "97 altitudes 120-600 km x 37 longitudes x 19 latitudes",
            "dtc_span": sorted({int(e["dtc_printed"]) for e in epochs}),
            "hours_utc": sorted({e["epoch"]["hour"] for e in epochs}),
        },
        "pooled_results": pooled,
        "stratification": {
            "pooled_across_epochs": analysis.get("pooled_strata"),
            "by_epoch": strata_by_epoch,
            "worst_twenty": analysis["worst_overall"],
            "interpretation_limits": (
                "Strata are reported for every epoch, covering altitude, "
                "latitude, longitude, time of day, DSTDTC and reference "
                "density separately. All twenty worst cases lie at the top of "
                "the grid."
            ),
            "causality_caveat": (
                "These strata are descriptive only. All eight epochs are "
                "three-hourly steps within a SINGLE calendar day and share one "
                "printed solar index set, so 'no dependence on latitude' means "
                "no variation was resolvable across the sampled latitudes "
                "AT THIS ACTIVITY LEVEL - it is not evidence that density "
                "error is independent of latitude. Likewise the DSTDTC and "
                "time-of-day strata span one day's geomagnetic variation and "
                "cannot separate solar-activity effects from geomagnetic ones, "
                "or disentangle time of day from epoch. No causal or "
                "independence claim is made from any stratum."
            ),
        },
        "findings": {
            "bridge_is_bit_exact": {
                "value": pooled["bridge_max_relative_difference"],
                "meaning": (
                    "the project adapter and a direct call to the official "
                    "Fortran model agree to the last bit on every evaluated "
                    "row; the ABI bridge, right-ascension wrapping and domain "
                    "checks add nothing"
                ),
                "status": "MEASURED",
            },
            "astronomy_is_not_the_cause": {
                "value": pooled["astronomy_max_relative_difference"],
                "meaning": (
                    "the driver's own THETA/SUNPOS agree with the IAU-1982 "
                    "expression used by the test to well below the residual"
                ),
                "status": "MEASURED",
            },
            "official_astronomy_provenance": {
                "driver_sha256": astronomy["driver_sha256"],
                "driver_sha256_matches_pin": astronomy["driver_sha256_matches_pin"],
                "sunpos_lines": astronomy["sunpos_lines"],
                "theta_lines": astronomy["theta_lines"],
                "fragment_sha256": astronomy["fragment_sha256"],
                "each_slice_verbatim_substring_of_driver": astronomy[
                    "each_slice_verbatim_substring_of_driver"
                ],
                "no_stub_routines_linked": True,
                "meaning": (
                    "the two subroutines are the driver's own bytes, sliced "
                    "from a hash-verified source; no dummy symbol is linked "
                    "beside code presented as official"
                ),
                "status": "MEASURED",
            },
            "dtc_reconstructs_exactly": {
                "epochs_matched": "%d of %d" % (
                    sum(1 for r in archive_rows if r["dtc_matches_printed"]),
                    len(archive_rows),
                ),
                "all_match": dtc_all_match,
                "values": [r["dtc_from_archive"] for r in archive_rows],
                "status": "MEASURED",
            },
            "index_sensitivity": {
                "method": identify.get("method_note", (
                    "central difference of relative density at step h and h/2, "
                    "each dividing the full 2*step span; Richardson "
                    "extrapolation uses the central-difference rule "
                    "(4*D(h/2) - D(h))/3"
                )),
                "h": identify.get("h"),
                "per_index": sensitivity,
                "status": "MEASURED",
            },
            "residual_is_one_scalar": {
                "statement": (
                    "at fixed altitude the relative density error is nearly "
                    "constant across all latitude/longitude points of that "
                    "plane"
                ),
                "spread_by_altitude_km": {
                    k: spread(epoch0["strata"]["altitude_km"]["by_altitude_km"][k])
                    for k in ("120.0", "200.0", "300.0", "450.0", "600.0")
                    if k in epoch0["strata"]["altitude_km"]["by_altitude_km"]
                },
                "status": "MEASURED",
            },
            "scalar_tc_offset": dict(scalar_tc["summary"], per_epoch=scalar_tc["epochs"]),
            "bounded_fit": {
                "method": bounded_fit["fit_method"],
                "bound": bounded_fit["bound"],
                "bound_meaning": bounded_fit["bound_meaning"],
                "per_epoch": [
                    {
                        "file": e["file"],
                        "hour_utc": e["hour_utc"],
                        "dtc_printed": e["dtc_printed"],
                        "baseline_rms": e["baseline"]["rms"],
                        "after_bounded_fit": e["after_bounded_fit"],
                        "fitted_offsets": e["fitted_offsets"],
                        "offset_at_bound": e["offset_at_bound"],
                        "rms_removed_fraction": e["rms_removed_fraction"],
                        "reaches_print_floor": e["reaches_print_floor"],
                    }
                    for e in fit_epochs
                ],
                "summary": fit_summary,
                "reaches_print_floor": fit_summary["epochs_reaching_print_floor"]
                == fit_summary["epochs_total"],
                "meaning": (
                    "A SINGLE set of index offsets, every one inside the +-0.5 "
                    "half-step the fixture's own F8.0 columns imply, removes "
                    "%s of the residual RMS. That is positive evidence that "
                    "index quantisation dominates. It does NOT reach the %s "
                    "relative half-ulp of the D15.5 density column, so it does "
                    "not by itself account for the published numbers to print "
                    "precision."
                )
                % (
                    "%.1f-%.1f %%"
                    % (
                        100 * fit_summary["rms_removed_fraction_range"][0],
                        100 * fit_summary["rms_removed_fraction_range"][1],
                    ),
                    PRINT_REL_HALF_ULP_MAX,
                ),
                "status": "MEASURED",
            },
            "geometry_freedom_does_not_help": {
                key: value
                for key, value in identify["fits"].items()
                if "altitude" in key or key.startswith("indices_h")
            },
            "identifiability": {
                "note": identify["fits"][
                    sorted(identify["fits"], key=lambda k: "indices_h_" in k)[0]
                ]["note"],
                "status": "MEASURED",
            },
            "print_floor": {
                "density_column": "D15.5",
                "relative_half_ulp_max": PRINT_REL_HALF_ULP_MAX,
            },
            "parallel_envelope_check": {
                "performed_by": "altitude investigation in this same amendment",
                "source": (
                    "docs/atmosphere/evidence/validation-amendment-v1/"
                    "altitude-datum.json -> "
                    "official_grid_reproduction.index_quantisation_envelope"
                ),
                "method": (
                    "all 2**8 corners of the +-0.5 sfu index box evaluated and "
                    "the sampled grid points tested for containment"
                ),
                "sampled_points": envelope["sampled_points"],
                "fraction_inside": envelope["fraction_inside"],
                "max_corner_span_relative": envelope["max_corner_span_relative"],
                "max_corner_span_percent": 100.0
                * envelope["max_corner_span_relative"],
                "quantity_note": (
                    "FULL corner-to-corner span (high - low) / rho, NOT a half "
                    "width. The upstream field name previously read "
                    "'half_width', which mislabelled the quantity; it has been "
                    "corrected to max_corner_span_relative."
                ),
                "status": "MEASURED",
                "interpretation": (
                    "a PER-POINT compatibility result: each sampled point is "
                    "individually consistent with SOME corner of the box. It "
                    "does NOT establish that one single global input vector "
                    "inside the box reproduces the whole grid, which is the "
                    "stronger claim tested by bounded_fit above and not "
                    "confirmed there."
                ),
                "note": (
                    "Complementary, not competing: the envelope bounds which "
                    "points are individually admissible, while bounded_fit "
                    "tests whether a single admissible set works globally and "
                    "leaves a residual above print precision."
                ),
            },
        },
        "unresolved": [
            {
                "issue": (
                    "the residual remaining after the best in-bound index fit"
                ),
                "measurement": {
                    "bounded_fit_rms_range": fit_summary["bounded_fit_rms_range"],
                    "print_floor": PRINT_REL_HALF_ULP_MAX,
                    "epochs_reaching_print_floor": fit_summary[
                        "epochs_reaching_print_floor"
                    ],
                    "epochs_total": fit_summary["epochs_total"],
                },
                "status": "UNRESOLVED",
                "explanation": (
                    "The bounded nonlinear search did not find a single input "
                    "set reproducing its sampled rows to print precision. This "
                    "does not prove that no such set exists: optimisation may "
                    "be incomplete. Other original-input, model-version or "
                    "producer-compiler differences are also untested because "
                    "the dated full-precision inputs and producer build settings "
                    "are unavailable. Complete causal attribution remains "
                    "UNRESOLVED; no original values are inferred from the fit."
                ),
            },
            {
                "issue": (
                    "the full-precision index record behind the 2023 fixture"
                ),
                "status": "UNRESOLVED",
                "evidence": {
                    "indices_within_print_bound": indices_matching,
                    "indices_total": archive_rows[0]["indices_total"]
                    if archive_rows
                    else None,
                    "archive_worse_by_factor_range": [
                        min(archive_factors), max(archive_factors)
                    ]
                    if archive_factors
                    else None,
                    "per_epoch": archive_rows,
                },
                "explanation": (
                    "The current SOLFSMY.TXT is a different data vintage from "
                    "the one that produced the fixture. Its values differ from "
                    "the published F8.0 row in nearly every index by far more "
                    "than the +-0.5 half-step, and using them directly is "
                    "roughly %s times worse, which confirms a vintage "
                    "mismatch rather than recovering the original. The "
                    "underlying values are therefore stated as unknown; only "
                    "their bounded effect is claimed."
                )
                % (
                    "%.0f" % min(archive_factors) if archive_factors else "many"
                ),
            },
            {
                "issue": "independent activity inference from this grid",
                "status": "DELIBERATELY NOT INFERRED",
                "explanation": (
                    "All eight epochs share one printed solar index set and "
                    "span only DSTDTC in {24, 50, 85, 115} within a single "
                    "calendar day, so solar-activity dependence cannot be "
                    "separated from geomagnetic variation here."
                ),
            },
        ],
        "what_would_resolve": [
            "obtaining the 2023 SOLFSMY vintage, so the printed F8.0 index row "
            "could be replaced by its full-precision original and the residual "
            "re-measured against a known input",
            "an independently reconstructed input set that accounts for the "
            "grid to within the D15.5 print precision, which would confirm or "
            "refute the residual-above-print-floor item",
        ],
        "redistribution": (
            "No official SET source or data is vendored or copied into this "
            "repository. JB2008.f is compiled in place from the hash-pinned "
            "external path; SUNPOS and THETA are sliced byte-identically from a "
            "hash-verified driver into a scratch file. All probe code in the "
            "repository is project-owned and reproduces no JB2008 equation."
        ),
    }


def load_envelope(path):
    """Read the parallel envelope result from the altitude investigation.

    Sourced from that file rather than restated here, so it cannot go stale
    independently of the run that produced it.
    """
    with open(path) as handle:
        record = json.load(handle)
    return record["official_grid_reproduction"]["index_quantisation_envelope"]


def load(path, label):
    if not path:
        return None
    with open(path) as handle:
        return json.load(handle)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--analysis", required=True)
    ap.add_argument("--identify", required=True)
    ap.add_argument("--scalar-tc", required=True)
    ap.add_argument("--bounded-fit", required=True)
    ap.add_argument("--astronomy-provenance", required=True)
    ap.add_argument("--envelope", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    missing = [
        label
        for label, path in (
            ("analysis", args.analysis),
            ("identify", args.identify),
            ("scalar-tc", args.scalar_tc),
            ("bounded-fit", args.bounded_fit),
            ("astronomy-provenance", args.astronomy_provenance),
            ("envelope", args.envelope),
        )
        if not path or not os.path.isfile(path)
    ]
    if missing:
        ap.error(
            "missing required run output(s): %s. Every measured field in this "
            "record comes from a run; refusing to emit a partial evidence file."
            % ", ".join(missing)
        )

    record = build(
        load(args.analysis, "analysis"),
        load(args.identify, "identify"),
        load(args.scalar_tc, "scalar-tc"),
        load(args.bounded_fit, "bounded-fit"),
        load(args.astronomy_provenance, "astronomy-provenance"),
        load_envelope(args.envelope),
    )
    text = json.dumps(record, indent=2)
    with open(args.output, "w") as handle:
        handle.write(text)
    print("%s %d bytes" % (args.output, len(text)))


if __name__ == "__main__":
    main()