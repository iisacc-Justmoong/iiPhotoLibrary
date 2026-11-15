#include "PhotoInformationFormatter.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace iiPhotoLibrary::presentation::detail {
namespace {
bool digits(std::string_view text,std::size_t at,std::size_t count,int& value){
    if(at>text.size()||count>text.size()-at)return false;
    value=0;
    for(std::size_t i=0;i<count;++i){
        const auto c=text[at+i];if(c<'0'||c>'9')return false;
        value=value*10+(c-'0');
    }
    return true;
}
}

std::string formatField(std::string_view pattern,std::string_view value){
    std::string result(pattern);
    constexpr std::string_view marker="{value}";
    for(std::size_t at=0;(at=result.find(marker,at))!=std::string::npos;at+=value.size())
        result.replace(at,marker.size(),value);
    return result;
}

std::string formatSize(std::uint64_t bytes,std::string_view bytesPattern,
        std::string_view kilobytesPattern,std::string_view megabytesPattern,std::string_view gigabytesPattern){
    constexpr std::uint64_t kib=1024,mib=kib*kib,gib=mib*kib;
    if(bytes<kib)return formatField(bytesPattern,std::to_string(bytes));
    const auto divisor=bytes< mib?kib:bytes<gib?mib:gib;
    const auto pattern=bytes<mib?kilobytesPattern:bytes<gib?megabytesPattern:gigabytesPattern;
    std::ostringstream number;number.imbue(std::locale::classic());
    number<<std::fixed<<std::setprecision(1)<<(static_cast<double>(bytes)/static_cast<double>(divisor));
    return formatField(pattern,number.str());
}

std::string formatTimestamp(std::string_view timestamp){
    const auto unchanged=[&]{return std::string(timestamp);};
    int year=0,month=0,day=0,hour=0,minute=0,second=0;
    if(timestamp.size()<16||timestamp[4]!='-'||timestamp[7]!='-'||
       (timestamp[10]!='T'&&timestamp[10]!='t'&&timestamp[10]!=' ')||timestamp[13]!=':'||
       !digits(timestamp,0,4,year)||!digits(timestamp,5,2,month)||!digits(timestamp,8,2,day)||
       !digits(timestamp,11,2,hour)||!digits(timestamp,14,2,minute))return unchanged();
    const auto date=std::chrono::year{year}/std::chrono::month{static_cast<unsigned>(month)}/
        std::chrono::day{static_cast<unsigned>(day)};
    if(year==0||!date.ok()||hour>23||minute>59)return unchanged();
    std::size_t at=16;
    if(at<timestamp.size()&&timestamp[at]==':'){
        if(!digits(timestamp,at+1,2,second)||second>59)return unchanged();
        at+=3;
        if(at<timestamp.size()&&timestamp[at]=='.'){
            const auto first=++at;
            while(at<timestamp.size()&&timestamp[at]>='0'&&timestamp[at]<='9')++at;
            if(at==first)return unchanged();
        }
    }
    if(at==timestamp.size())
        return std::string(timestamp.substr(0,10))+" "+std::string(timestamp.substr(11,5));
    int offsetMinutes=0;
    if(timestamp[at]=='Z'||timestamp[at]=='z'){
        if(++at!=timestamp.size())return unchanged();
    }else if(timestamp[at]=='+'||timestamp[at]=='-'){
        const auto sign=timestamp[at++]=='+'?1:-1;
        int offsetHour=0,offsetMinute=0;
        if(!digits(timestamp,at,2,offsetHour))return unchanged();
        at+=2;if(at<timestamp.size()&&timestamp[at]==':')++at;
        if(!digits(timestamp,at,2,offsetMinute)||at+2!=timestamp.size()||offsetHour>23||offsetMinute>59)
            return unchanged();
        offsetMinutes=sign*(offsetHour*60+offsetMinute);
    }else return unchanged();

    const auto instant=std::chrono::sys_days{date}+std::chrono::hours{hour}+
        std::chrono::minutes{minute-offsetMinutes}+std::chrono::seconds{second};
    const auto epoch=std::chrono::duration_cast<std::chrono::seconds>(instant.time_since_epoch()).count();
    if(!std::in_range<std::time_t>(epoch))return unchanged();
    const auto time=static_cast<std::time_t>(epoch);
    std::tm local{};
#if defined(_WIN32)
    if(localtime_s(&local,&time)!=0)return unchanged();
#else
    if(!localtime_r(&time,&local))return unchanged();
#endif
    char result[32]{};
    if(!std::strftime(result,sizeof(result),"%Y-%m-%d %H:%M",&local))return unchanged();
    return result;
}
}
