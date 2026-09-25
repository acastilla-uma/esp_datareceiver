#include "serial_format.h"

#include <algorithm>
#include <cctype>

namespace {

std::string normalized(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
                    return std::isspace(c);
                }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool isAxis(const std::string& field, const std::string& axis) {
    const std::string value = normalized(field);
    return value == axis || value == axis + "_g";
}

}  // namespace

bool isMeasurementHeader(const std::vector<std::string>& fields) {
    if (fields.size() < 3) {
        return false;
    }

    std::size_t accelerationOffset = 0;
    if (normalized(fields[0]) == "timestamp_us") {
        accelerationOffset = 1;
    }
    return fields.size() >= accelerationOffset + 3 &&
           isAxis(fields[accelerationOffset], "ax") &&
           isAxis(fields[accelerationOffset + 1], "ay") &&
           isAxis(fields[accelerationOffset + 2], "az");
}

