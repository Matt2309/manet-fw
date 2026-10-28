#include "kalman.h"

#include "config.h"

namespace {

// Predizione su dt secondi: x = F x, P = F P F^T + Q.
LinkEstimate predict(const LinkEstimate& x, double dt) {
    constexpr double sa2 = KALMAN_SIGMA_A * KALMAN_SIGMA_A;
    double dt2 = dt * dt;
    LinkEstimate y;
    y.r = x.r + dt * x.s;
    y.s = x.s;
    y.p00 = x.p00 + 2.0 * dt * x.p01 + dt2 * x.p11 + sa2 * dt2 * dt / 3.0;
    y.p01 = x.p01 + dt * x.p11 + sa2 * dt2 / 2.0;
    y.p11 = x.p11 + sa2 * dt;
    return y;
}

}  // namespace

void LinkFilter::update(uint32_t t_ms, double z) {
    if (!started_ || t_ms - t_last_ > KALMAN_REINIT_GAP_MS) {
        x_ = LinkEstimate{z, 0.0, KALMAN_R, 0.0, KALMAN_INITIAL_SLOPE_STD * KALMAN_INITIAL_SLOPE_STD};
        started_ = true;
    } else {
        LinkEstimate p = predict(x_, (t_ms - t_last_) / 1000.0);
        double innovation = z - p.r;
        double innovation_var = p.p00 + KALMAN_R;
        double k0 = p.p00 / innovation_var;
        double k1 = p.p01 / innovation_var;
        // Forma di Joseph, come nel simulatore.
        double a = 1.0 - k0;
        x_.r = p.r + k0 * innovation;
        x_.s = p.s + k1 * innovation;
        x_.p00 = a * a * p.p00 + KALMAN_R * k0 * k0;
        x_.p01 = a * (p.p01 - k1 * p.p00) + KALMAN_R * k0 * k1;
        x_.p11 = k1 * k1 * p.p00 - 2.0 * k1 * p.p01 + p.p11 + KALMAN_R * k1 * k1;
    }
    t_last_ = t_ms;
}

LinkEstimate LinkFilter::predicted(uint32_t now_ms) const { return predict(x_, (now_ms - t_last_) / 1000.0); }
