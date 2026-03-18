// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top

#pragma once

#include <cmath>

namespace utils {

/** One-step blend toward target: smoothed = b·target + (1−b)·previous. b = 1 − exp(−dt/τ), first-order low-pass with time constant τ (seconds), frame-rate independent for real dt. */
inline float exponential_smooth_blend(float dt_sec, float tau_sec) {
    if (!(tau_sec > 0.0f) || !(dt_sec > 0.0f)) {
        return 0.0f;
    }
    return 1.0f - std::exp(-dt_sec / tau_sec);
}

/** Single step of first-order smoothing from previous toward target. */
inline float exponential_smooth_toward(float previous, float target, float dt_sec, float tau_sec) {
    const float b = exponential_smooth_blend(dt_sec, tau_sec);
    return (b * target) + ((1.0f - b) * previous);
}

/**
 * Time constant τ such that one discrete step per 1/steps_per_sec seconds with fixed step_alpha
 * matches continuous exp decay: step_alpha = 1 − exp(−1/(steps_per_sec·τ)).
 */
inline float first_order_tau_for_step_alpha(float step_alpha, float steps_per_sec) {
    if (!(step_alpha > 0.0f) || !(step_alpha < 1.0f) || !(steps_per_sec > 0.0f)) {
        return 0.325f;
    }
    return -1.0f / (steps_per_sec * std::log(1.0f - step_alpha));
}

} // namespace utils
