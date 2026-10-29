#include "core/almanac/almanac.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
using lume::calendar::Date;
void Expect(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
void String(std::ostream& out,std::string_view text){
    out<<'"';
    for(unsigned char ch:text){
        if(ch=='"'||ch=='\\')out<<'\\'<<static_cast<char>(ch);
        else {Expect(ch>=0x20,"no control characters");out<<static_cast<char>(ch);}
    }
    out<<'"';
}
void List(std::ostream& out,const std::vector<std::string_view>& list){
    out<<'[';bool comma=false;
    for(auto item:list){if(comma)out<<',';String(out,item);comma=true;}out<<']';
}
int main(int argc,char** argv){
    if(argc==3&&std::string_view(argv[1])=="--export"){
        std::ofstream out(std::filesystem::u8path(argv[2]),std::ios::binary);
        Expect(out.good(),"open export");
        for(Date date{1900,1,1};date<=Date{2100,12,31};date=*lume::calendar::AddDays(date,1)){
            const auto value=lume::almanac::Lookup(date);Expect(value.has_value(),"complete range");
            out<<"{\"date\":\""<<date.year<<'-'<<std::setfill('0')<<std::setw(2)<<date.month<<'-'<<std::setw(2)<<date.day<<"\",\"lunarDate\":";
            String(out,value->lunarDate);out<<",\"year\":";String(out,value->year);
            out<<",\"cycle\":";String(out,value->cycle);out<<",\"yi\":";List(out,value->yi);out<<",\"ji\":";List(out,value->ji);out<<"}\n";
        }
        Expect(out.good(),"export write");return 0;
    }
    const auto day=lume::almanac::Lookup({2026,9,26});
    Expect(day.has_value(),"current date has original almanac data");
    Expect(day->lunarDate==u8"八月十六"&&day->year==u8"丙午年(马)"&&day->cycle==u8"丁酉月 癸卯日 周六","lunar date and cycles");
    Expect(day->yi==std::vector<std::string_view>{u8"破屋",u8"坏垣",u8"求医",u8"治病",u8"馀事勿取"},"ordered original auspicious activities");
    Expect(day->ji==std::vector<std::string_view>{u8"移徙",u8"入宅"},"ordered original inauspicious activities");
    const auto leap=lume::almanac::Lookup({2023,3,22});
    Expect(leap&&leap->lunarDate==u8"闰二月初一"&&leap->cycle==u8"乙卯月 己卯日 周三","leap lunar month");
    const auto first=lume::almanac::Lookup({1900,1,1}),last=lume::almanac::Lookup({2100,12,31});
    Expect(first&&first->yi.size()==23&&last&&last->year==u8"庚申年(猴)","inclusive endpoints");
    for(auto date:{Date{1899,12,31},{2101,1,1},{2026,2,29},{2026,13,1},{0,0,0}})
        Expect(!lume::almanac::Lookup(date),"unsupported or invalid date absent");
    Expect(lume::almanac::Lookup({2000,2,29}).has_value(),"Gregorian leap day");
    std::cout<<"original almanac contract passed\n";
}
