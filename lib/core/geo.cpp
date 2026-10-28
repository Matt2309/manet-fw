#include "geo.h"

#include <math.h>

namespace {

constexpr double EARTH_RADIUS_M = 6371000.0;
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double E7 = 1e-7;

}  // namespace

double distance_m(const packets::Position& a, const packets::Position& b) {
    double lat_a = a.lat_e7 * E7, lat_b = b.lat_e7 * E7;
    double dy = (lat_b - lat_a) * DEG_TO_RAD * EARTH_RADIUS_M;
    double dx = (b.lon_e7 - a.lon_e7) * E7 * DEG_TO_RAD * EARTH_RADIUS_M * cos(0.5 * (lat_a + lat_b) * DEG_TO_RAD);
    return sqrt(dx * dx + dy * dy);
}
