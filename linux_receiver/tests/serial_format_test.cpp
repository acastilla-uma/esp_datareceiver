#include "serial_format.h"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    assert(isMeasurementHeader({"ax", "ay", "az", "gx"}));
    assert(isMeasurementHeader(
        {"timestamp_us", "ax_g", "ay_g", "az_g", "gx_deg_s"}));
    assert(isMeasurementHeader(
        {" timestamp_us ", " ax_g ", " ay_g ", " az_g "}));
    assert(!isMeasurementHeader({"123", "1.0", "2.0", "3.0"}));
    assert(!isMeasurementHeader({"timestamp_us", "latitude", "longitude"}));
    std::cout << "serial_format_test: OK\n";
}

