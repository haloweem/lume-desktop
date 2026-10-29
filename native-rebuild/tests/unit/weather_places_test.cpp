#include "core/weather/weather_places.h"
#include "vendor/nlohmann/json.hpp"
#include <cstdlib>
#include <iostream>
using namespace lume::weather;
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--simplify") {
        std::string line;
        while (std::getline(std::cin, line))
            std::cout << nlohmann::json(SimplifyWeatherPlaceName(nlohmann::json::parse(line).get<std::string>())).dump() << '\n';
        return 0;
    }
    for (const auto& pair : {std::pair{u8"蘇州市", u8"苏州市"}, {u8"臺北市", u8"台北市"},
        {u8"重慶市", u8"重庆市"}, {u8"乾縣", u8"乾县"}, {u8"昆山",u8"昆山"},
        {u8"拉薩市",u8"拉萨市"}, {u8"香港特別行政區",u8"香港特别行政区"},
        {u8"體感溫度",u8"体感温度"}, {u8"陰轉多雲",u8"阴转多云"}}) {
        if (SimplifyWeatherPlaceName(pair.first) != pair.second) {
            std::cerr << "FAIL: original place normalization " << pair.first << '\n';
            return 1;
        }
    }
    std::cout << "Simplified Chinese weather normalization passed\n";
}
