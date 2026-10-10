#include "leo/atmosphere/weather.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace leo::atmosphere {
namespace {
long long serial(int y,int m,int d) {
    using namespace std::chrono;
    const year_month_day date{year{y},month{static_cast<unsigned>(m)},day{static_cast<unsigned>(d)}};
    if(y<1950 || y>2099 || !date.ok()) throw std::invalid_argument("invalid weather calendar date");
    return sys_days{date}.time_since_epoch().count();
}
long long serial_doy(int y,int doy) {
    using namespace std::chrono;
    if(doy<1 || doy>(year{y}.is_leap()?366:365)) throw std::invalid_argument("invalid weather day of year");
    return serial(y,1,1)+doy-1;
}
double field(const std::string& line,std::size_t offset,std::size_t length) {
    const std::string text=line.substr(offset,length);
    if(text.find_first_not_of(' ')==std::string::npos) return std::numeric_limits<double>::quiet_NaN();
    std::size_t n=0;const double value=std::stod(text,&n);
    if(text.find_first_not_of(" \r\t",n)!=std::string::npos || !std::isfinite(value))
        throw std::invalid_argument("invalid fixed-field weather number");
    return value;
}
int integer_field(const std::string& line,std::size_t offset,std::size_t length) {
    const double v=field(line,offset,length);
    if(!std::isfinite(v)||v<0||v>9999||std::floor(v)!=v)
        throw std::invalid_argument("missing or non-integer weather calendar field");
    return static_cast<int>(v);
}
template<class Map> const typename Map::mapped_type& required(const Map& data,long long day) {
    const auto it=data.find(day);
    if(it==data.end()) throw std::runtime_error("missing required weather day "+std::to_string(day));
    return it->second;
}
void require_positive(double v) {
    if(!std::isfinite(v)||v<=0) throw std::runtime_error("missing or invalid solar driver");
}
void require_ap(double v) {
    if(!std::isfinite(v)||v<0||v>400) throw std::runtime_error("missing or invalid Ap driver");
}
void validate_utc(const UtcCalendar& utc) {
    if(!std::isfinite(utc.seconds_of_day)||!std::isfinite(utc.day_length_seconds)||
       utc.day_length_seconds<86399||utc.day_length_seconds>86401||
       utc.seconds_of_day<0||utc.seconds_of_day>=utc.day_length_seconds||
       utc.days_since_1970!=serial(utc.year,utc.month,utc.day)||
       utc.days_since_1970!=serial_doy(utc.year,utc.day_of_year))
        throw std::invalid_argument("inconsistent UTC weather query");
}
void require_proxy(const std::string& codes,unsigned proxy) {
    // Native SOLFSMY header distinguishes measured/derived/composite/corrected
    // records from explicit gap fills, means of neighbours and missing values.
    constexpr const char* allowed[]{"1","12589ABCDE","1B","189CDE"};
    if(codes.size()!=4 || std::string_view(allowed[proxy]).find(codes[proxy])==std::string_view::npos)
        throw std::runtime_error("SOLFSMY proxy is missing/interpolated or has unsupported provenance");
}
}
WeatherDataset WeatherDataset::load(const std::filesystem::path& cssi_archive,
                                   const std::filesystem::path& solfsmy,
                                   const std::filesystem::path& dtcfile) {
    WeatherDataset data;
    if(!cssi_archive.empty()) {
        std::ifstream in(cssi_archive);if(!in) throw std::invalid_argument("cannot open CSSI weather archive");
        std::string line;bool observed=false,section=false;long long previous=std::numeric_limits<long long>::min();
        while(std::getline(in,line)) {
            if(line.starts_with("BEGIN ")) {
                section=true;
                observed=line.starts_with("BEGIN OBSERVED");
                previous=std::numeric_limits<long long>::min();
                continue;
            }
            if(line.starts_with("END ")) {section=false;continue;}
            if(!section || line.empty() || line[0]=='#') continue;
            if(line.size()<130) throw std::invalid_argument("CSSI v1.2 weather row shorter than 130 columns");
            const auto day=serial(integer_field(line,0,4),integer_field(line,4,3),integer_field(line,7,3));
            if(day<=previous) throw std::invalid_argument("CSSI dates must increase within each section");
            previous=day;
            const double quality=field(line,98,2);
            RecordStatus status=observed?RecordStatus::observed:RecordStatus::predicted;
            if(observed && quality!=0) status=quality==1?RecordStatus::interpolated:RecordStatus::missing;
            CssiRecord record{field(line,112,6),field(line,118,6),field(line,78,4),{},status};
            for(unsigned i=0;i<8;++i) record.ap[i]=field(line,46+4*i,4);
            auto [it,inserted]=data.cssi_.try_emplace(day,record);
            if(!inserted && observed) {
                if(it->second.status!=RecordStatus::predicted)
                    throw std::invalid_argument("duplicate CSSI observed day");
                it->second=record;
            } // Forecast/fit overlap never replaces an observed record.
        }
        if(data.cssi_.empty()) throw std::invalid_argument("no CSSI v1.2 weather records");
    }
    if(!solfsmy.empty()) {
        std::ifstream in(solfsmy);if(!in) throw std::invalid_argument("cannot open SOLFSMY archive");
        std::string line;long long previous=std::numeric_limits<long long>::min();
        while(std::getline(in,line)) {
            if(line.empty()||line[0]=='#') continue;
            std::istringstream row(line);int y=0,doy=0;double jd=0;SolarRecord record{};
            if(!(row>>y>>doy>>jd)) throw std::invalid_argument("invalid SOLFSMY date");
            for(double& v:record.proxies) if(!(row>>v)||!std::isfinite(v)) throw std::invalid_argument("invalid SOLFSMY proxy");
            if(!(row>>record.source_codes)||record.source_codes.size()!=4) throw std::invalid_argument("invalid SOLFSMY source codes");
            std::string extra;if(row>>extra) throw std::invalid_argument("unexpected SOLFSMY trailing fields");
            const auto day=serial_doy(y,doy);
            if(day<=previous || std::abs(jd-(2440587.5+static_cast<double>(day)+.5))>1e-8)
                throw std::invalid_argument("inconsistent SOLFSMY date/JD or ordering");
            previous=day;data.solar_.emplace(day,std::move(record));
        }
        if(data.solar_.empty()) throw std::invalid_argument("no SOLFSMY records");
    }
    if(!dtcfile.empty()) {
        std::ifstream in(dtcfile);if(!in) throw std::invalid_argument("cannot open DTCFILE archive");
        std::string line;long long previous=std::numeric_limits<long long>::min();
        while(std::getline(in,line)) {
            if(line.empty()||line[0]=='#') continue;
            if(line.size()<109 || !line.starts_with("DTC ")) throw std::invalid_argument("invalid DTCFILE fixed-field row");
            const auto day=serial_doy(integer_field(line,4,4),integer_field(line,9,3));
            if(day<=previous) throw std::invalid_argument("DTC dates must be strictly increasing");
            previous=day;
            std::array<int,24> hours{};
            for(unsigned h=0;h<24;++h) {
                const double v=field(line,13+4*h,4);
                if(!std::isfinite(v)||v<0||v>2000||std::floor(v)!=v) throw std::invalid_argument("missing/invalid DTC hourly index");
                hours[h]=static_cast<int>(v);
            }
            data.dtc_.emplace(day,hours);
        }
        if(data.dtc_.empty()) throw std::invalid_argument("no DTC records");
    }
    return data;
}
NrlmsiseInput WeatherDataset::nrlmsise(const UtcCalendar& utc,bool history) const {
    validate_utc(utc);
    const auto get=[&](long long day)->const CssiRecord& {
        const auto& r=required(cssi_,day);
        if(r.status!=RecordStatus::observed) throw std::runtime_error("CSSI required record is not observed");
        return r;
    };
    const auto& current=get(utc.days_since_1970);
    const auto& previous=get(utc.days_since_1970-1);
    require_positive(previous.f107_observed);require_positive(current.f107_observed_center81);require_ap(current.ap_daily);
    NrlmsiseInput input;input.year=utc.year;input.day_of_year=utc.day_of_year;input.utc_seconds=utc.seconds_of_day;
    input.f107=previous.f107_observed;input.f107_average=current.f107_observed_center81;input.ap=current.ap_daily;
    input.ap_history.fill(std::numeric_limits<double>::quiet_NaN());
    input.ap_history[0]=current.ap_daily;
    if(history) {
        const int bin=std::min(7,static_cast<int>(utc.seconds_of_day/10800));
        const auto ap=[&](int offset) {
            int b=bin-offset;long long day=utc.days_since_1970;
            while(b<0) {b+=8;--day;}
            const double v=get(day).ap[static_cast<unsigned>(b)];require_ap(v);return v;
        };
        for(int i=0;i<4;++i) input.ap_history[static_cast<unsigned>(i)+1]=ap(i);
        input.ap_history[5]=input.ap_history[6]=0;
        for(int i=4;i<=11;++i) input.ap_history[5]+=ap(i)/8;
        for(int i=12;i<=19;++i) input.ap_history[6]+=ap(i)/8;
    }
    return input;
}
Jb2008Input WeatherDataset::jb2008(const UtcCalendar& utc) const {
    validate_utc(utc);
    const auto& fs=required(solar_,utc.days_since_1970-1);
    const auto& m=required(solar_,utc.days_since_1970-2);
    const auto& y=required(solar_,utc.days_since_1970-5);
    require_proxy(fs.source_codes,0);require_proxy(fs.source_codes,1);require_proxy(m.source_codes,2);require_proxy(y.source_codes,3);
    Jb2008Input input;input.mjd_utc=utc.mjd;
    input.f10=fs.proxies[0];input.f10_average=fs.proxies[1];input.s10=fs.proxies[2];input.s10_average=fs.proxies[3];
    input.m10=m.proxies[4];input.m10_average=m.proxies[5];input.y10=y.proxies[6];input.y10_average=y.proxies[7];
    for(double v:{input.f10,input.f10_average,input.s10,input.s10_average,input.m10,input.m10_average,input.y10,input.y10_average})
        if(!std::isfinite(v)||v<40) throw std::runtime_error("missing/invalid JB solar index");
    const auto& hours=required(dtc_,utc.days_since_1970);
    const int h=std::min(23,static_cast<int>(utc.seconds_of_day/3600));
    const int next=h==23?required(dtc_,utc.days_since_1970+1)[0]:hours[static_cast<unsigned>(h)+1];
    // Source DTCVAL interpolates hourly indices, adds 0.5 then truncates integer.
    // Stretch only the final UTC hour on a leap-second day; retain current-day records.
    const double hour_length=h==23?utc.day_length_seconds-23*3600:3600;
    const double fraction=(utc.seconds_of_day-h*3600)/hour_length;
    input.dstdtc_k=std::floor(hours[static_cast<unsigned>(h)]+fraction*(next-hours[static_cast<unsigned>(h)])+.5);
    return input;
}
}
