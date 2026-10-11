#!/usr/bin/env python3
"""Generate diagnostic copies. Unique anchors fail closed; originals never written."""
import argparse
import hashlib
import json
from pathlib import Path


def generate(root, out):
    out.mkdir(parents=True, exist_ok=True)
    provenance = {}
    def load(name):
        path = root / 'src' / name
        text = path.read_text()
        provenance[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        return '#include "profile.hpp"\nnamespace AP = atm_profile;\n' + text
    def replace(text, old, new):
        if text.count(old) != 1:
            raise RuntimeError(f'anchor must match once: {old!r}: {text.count(old)}')
        return text.replace(old, new)
    def after(text, anchor, stage):
        return replace(text, anchor, anchor + '\n    pt.mark(AP::' + stage + ');')
    def save(name, text):
        (out / Path(name).name).write_text(text)

    t = load('atmosphere/provider.cpp')
    t = replace(t, 'Result DirectAtmosphere::evaluate(Epoch epoch,InertialVector position) const {',
        'Result DirectAtmosphere::evaluate(Epoch epoch,InertialVector position) const {\n    AP::Scope whole(AP::provider); AP::Timer pt;')
    for anchor, stage in [
        ('throw std::invalid_argument("nonfinite atmosphere epoch or position");', 'validation'),
        ('const auto rotation=SpiceEphemerisProvider::earth_fixed_rotation(epoch,frame_);', 'rotation'),
        ('const Eigen::Vector3d fixed=rotation*position.as_eigen();', 'transform'),
        ('const auto g=geodetic(fixed);', 'geodesy'),
        ('const auto utc=SpiceEphemerisProvider::utc_calendar(epoch);', 'utc'),
        ('auto in=weather_.nrlmsise(utc,ap_history_);', 'weather'),
        ('in.local_solar_time_hours=std::fmod(utc.seconds_of_day/3600+in.longitude_deg/15+24,24);', 'geometry'),
        ('auto in=weather_.jb2008(utc);', 'weather')]:
        t = after(t, anchor, stage)
    # Sun acquisition has its own inclusive provider segment, separate from angular work.
    t = after(t, 'const auto sun=spice_.geometric_state(sun_naif_id,epoch).position_m;', 'spkez')
    t = after(t, 'const Eigen::Vector3d fixed_sun=rotation*sun.as_eigen();', 'sun_transform')
    t = after(t, 'in.position_ra_lat_alt={g.longitude_rad,std::atan2(fixed.z(),std::hypot(fixed.x(),fixed.y())),g.altitude_m/1000};', 'geometry')
    for old, call in [('return nrlmsise_.evaluate(in);','nrlmsise_.evaluate(in)'),('return Jb2008{}.evaluate(in);','Jb2008{}.evaluate(in)')]:
        t = replace(t, old, f'const auto result={call}; pt.mark(AP::native); return result;')
    # Avoid double-counting provider Sun segment with the primitive of the same name.
    t = replace(t, 'pt.mark(AP::spkez);', 'pt.mark(AP::sun_total);')
    save('provider.cpp', t)

    t = load('environment/spice.cpp')
    # Instrument only requested wrappers, not load/destruction/epoch construction.
    functions = [
        ('EphemerisState SpiceEphemerisProvider::geometric_state', 'Epoch SpiceEphemerisProvider::epoch_from_utc', 'sun'),
        ('UtcCalendar SpiceEphemerisProvider::utc_calendar', 'Eigen::Matrix3d SpiceEphemerisProvider::earth_fixed_rotation', 'utc'),
        ('Eigen::Matrix3d SpiceEphemerisProvider::earth_fixed_rotation', 'std::string SpiceEphemerisProvider::toolkit_version', 'rot')]
    for start, end, prefix in functions:
        a, b = t.index(start), t.index(end)
        block = t[a:b]
        opening = block.index('{')
        block = block[:opening+1] + '\n    AP::Timer pt;' + block[opening+1:]
        block = replace(block, '    std::lock_guard lock{spice_mutex};', f'    pt.mark(AP::{prefix}_validation);\n    std::lock_guard lock{{spice_mutex}};\n    pt.mark(AP::{prefix}_lock);')
        block = after(block, 'enter_spice();', prefix+'_prepare')
        if prefix == 'sun':
            block = after(block, 'spkez_c(sun_naif_id,epoch.et_seconds,"J2000","NONE",earth_naif_id,state,&light_time);', 'spkez')
            block = replace(block, '    return result;', '    pt.mark(AP::sun_extract);\n    return result;')
        elif prefix == 'rot':
            block = after(block, 'pxform_c("J2000",frame.c_str(),epoch.et_seconds,rotation);', 'pxform')
            block = replace(block, '    return result;', '    pt.mark(AP::rot_extract);\n    return result;')
        else:
            block = after(block, 'et2utc_c(epoch.et_seconds,"ISOC",9,sizeof(text),text);', 'et2utc')
            block = after(block, 'throw std::runtime_error("unsupported SPICE UTC calendar representation");', 'utc_parse')
            block = after(block, 'double start_et=0,end_et=0;', 'utc_date_format')
            block = replace(block, 'str2et_c(start,&start_et);str2et_c(end,&end_et);', 'str2et_c(start,&start_et);\n    pt.mark(AP::midnight_first);\n    str2et_c(end,&end_et);\n    pt.mark(AP::midnight_second);')
            block = after(block, 'if(length<86399 || length>86401) throw std::runtime_error("unsupported UTC day length");', 'utc_daylength')
            block = after(block, 'const int doy=static_cast<int>((today-sys_days{year{y}/January/1}).count())+1;', 'utc_doy')
            block = replace(block, '    return {y,m,d,doy,serial,sod,length,40587+static_cast<double>(serial)+sod/length};', '    const UtcCalendar result{y,m,d,doy,serial,sod,length,40587+static_cast<double>(serial)+sod/length};\n    pt.mark(AP::utc_metadata);\n    return result;')
        t = t[:a] + block + t[b:]
    save('spice.cpp', t)

    t = load('atmosphere/coordinates.cpp')
    t = replace(t, 'Geodetic geodetic(const Eigen::Vector3d& r) {', 'Geodetic geodetic(const Eigen::Vector3d& r) {\n    AP::Timer pt;')
    t = after(t, 'throw std::invalid_argument("WGS84 conversion requires finite nonzero position");', 'geo_validation')
    t = replace(t, 'if(p<1e-9) return {std::copysign(std::numbers::pi/2,r.z()),0,std::abs(r.z())-b};', 'if(p<1e-9) {const Geodetic result{std::copysign(std::numbers::pi/2,r.z()),0,std::abs(r.z())-b}; pt.mark(AP::geo_initial); return result;}')
    t = after(t, 'bool converged=false;', 'geo_initial')
    t = replace(t, 'for(unsigned i=0;i<16;++i) {', 'for(unsigned i=0;i<16;++i) {\n        if(AP::active) ++AP::active->iterations;')
    t = after(t, 'if(!converged) throw std::runtime_error("WGS84 latitude iteration did not converge");', 'geo_iteration')
    t = after(t, 'const double height=p*c+r.z()*s-n*(1-e2*s*s);', 'geo_altitude')
    t = replace(t, '    return {lat,std::atan2(r.y(),r.x()),height};', '    const Geodetic result{lat,std::atan2(r.y(),r.x()),height};\n    pt.mark(AP::geo_longitude);\n    return result;')
    save('coordinates.cpp', t)

    t = load('atmosphere/weather.cpp')
    for signature in ['NrlmsiseInput WeatherDataset::nrlmsise(const UtcCalendar& utc,bool history) const {','Jb2008Input WeatherDataset::jb2008(const UtcCalendar& utc) const {']:
        t = replace(t, signature, signature+'\n    AP::Timer pt;')
    # Two intentional matches are handled separately within each function.
    a = t.index('NrlmsiseInput WeatherDataset::nrlmsise'); b = t.index('Jb2008Input WeatherDataset::jb2008')
    n, j = t[a:b], t[b:]
    for anchor,stage in [('validate_utc(utc);','weather_validation'),('const auto& current=get(utc.days_since_1970);','cssi_current'),('const auto& previous=get(utc.days_since_1970-1);','cssi_previous'),('input.ap_history[0]=current.ap_daily;','cssi_fields')]:
        n=after(n,anchor,stage)
    n=replace(n,'    return input;','    pt.mark(AP::ap_history);\n    return input;')
    for anchor,stage in [('validate_utc(utc);','weather_validation'),('const auto& fs=required(solar_,utc.days_since_1970-1);','solar_fs'),('const auto& m=required(solar_,utc.days_since_1970-2);','solar_m'),('const auto& y=required(solar_,utc.days_since_1970-5);','solar_y'),('throw std::runtime_error("missing/invalid JB solar index");','solar_fields'),('const int next=h==23?required(dtc_,utc.days_since_1970+1)[0]:hours[static_cast<unsigned>(h)+1];','dtc_lookup')]:
        j=after(j,anchor,stage)
    j=replace(j,'    return input;','    pt.mark(AP::dtc_interpolate);\n    return input;')
    save('weather.cpp',t[:a]+n+j)

    for name,signature,construct,call_end in [
        ('nrlmsise00.cpp','Result Nrlmsise00::evaluate(const NrlmsiseInput& in) const {','    const auto wrap=', '        gtd7d(&input,&flags,&output);'),
        ('jb2008.cpp','Result Jb2008::evaluate(const Jb2008Input& in) const {','    const auto wrap=', '            &in.m10,&in.m10_average,&in.y10,&in.y10_average,&in.dstdtc_k,temp,&rho);')]:
        t=load('atmosphere/'+name)
        t=replace(t,signature,signature+'\n    AP::Timer pt;')
        t=replace(t,construct,'    pt.mark(AP::native_validation);\n'+construct)
        t=replace(t,'        std::lock_guard lock{native_mutex};','        pt.mark(AP::native_construct);\n        std::lock_guard lock{native_mutex};\n        pt.mark(AP::native_lock);')
        t=after(t,call_end,'native_execute')
        if name=='nrlmsise00.cpp':
            t=replace(t,'    return result;','    pt.mark(AP::native_output);\n    return result;')
        else:
            t=replace(t,'    return {rho,temp[1],temp[0],{},true,false};','    const Result result{rho,temp[1],temp[0],{},true,false};\n    pt.mark(AP::native_output);\n    return result;')
        save(name,t)
    (out/'source-hashes.json').write_text(json.dumps(provenance,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    args=p.parse_args();generate(args.root,args.out)
