#include "battery.h"

namespace {

struct Point {
    int mv;
    int pct;
};

// Curva di scarica tipica di una LiPo a una cella, dal punto piu' alto al piu' basso.
const Point TABLE[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, {3950, 70},
    {3910, 65},  {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, {3800, 40}, {3790, 35},
    {3770, 30},  {3750, 25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},  {3300, 0},
};
constexpr int TABLE_SIZE = sizeof(TABLE) / sizeof(TABLE[0]);

}  // namespace

int battery_percent(int mv) {
    if (mv >= TABLE[0].mv) return 100;
    if (mv <= TABLE[TABLE_SIZE - 1].mv) return 0;
    for (int i = 1; i < TABLE_SIZE; i++) {
        if (mv >= TABLE[i].mv) {
            int dmv = TABLE[i - 1].mv - TABLE[i].mv;
            int dpct = TABLE[i - 1].pct - TABLE[i].pct;
            // arrotondamento al piu' vicino
            return TABLE[i].pct + ((mv - TABLE[i].mv) * dpct + dmv / 2) / dmv;
        }
    }
    return 0;
}

void BatteryAverage::add(int mv) {
    buf_[next_] = mv;
    next_ = (next_ + 1) % N;
    if (count_ < N) count_++;
}

int BatteryAverage::average_mv() const {
    if (count_ == 0) return 0;
    long sum = 0;
    for (int i = 0; i < count_; i++) sum += buf_[i];
    return static_cast<int>((sum + count_ / 2) / count_);
}
