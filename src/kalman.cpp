#include "kalman.hpp"
#include <stdio.h>

KalmanFilter::KalmanFilter(double position, bool video)
{
    estimate_position = position;
    estimate_velocity = 0.0;
    has_velocity = false;

    if (!video)
    {
        // normal settings used for tracking X-Y coordinates
        p_xx = 0.1;
        p_xv = 0.1;
        p_vv = 0.1;
        position_variance = 0.01;
        velocity_variance = 0.01;
        r = 100000.0;
    }
    else
    {
        // special setting used by video streaming
        p_xx = 0.1 * position;
        p_xv = 1.0;
        p_vv = 1.0;
        position_variance = 0.1 * position;
        velocity_variance = 0.01 * position / 1000.0;
        r = 0.01 * position;
    }
}

void KalmanFilter::reset(double position)
{
    estimate_position = position;
    estimate_velocity = 0.0;
    has_velocity = false;
}

void KalmanFilter::update(double position, double deltat)
{
    if (deltat <= 0.0)
        return;

    if (!has_velocity)
    {
        estimate_velocity = (position - estimate_position) / deltat;
        estimate_position = position;
        has_velocity = true;
    }
    else
    {
        //printf("position_variance: %f, velocity_variance: %f\n", position_variance, velocity_variance);
        //Temporal update (predictive)
        estimate_position += estimate_velocity * deltat;

        // Update covariance
        p_xx += deltat * (2.0 * p_xv + deltat * p_vv);
        p_xv += deltat * p_vv;

        p_xx += deltat * position_variance;
        p_vv += deltat * velocity_variance;

        // Observational update (reactive)
        double vi = 1.0 / (p_xx + r);
        double kx = p_xx * vi;
        double kv = p_xv * vi;

        estimate_position += (position - estimate_position) * kx;
        estimate_velocity += (position - estimate_position) * kv;

        p_xx *= 1.0 - kx;
        p_xv *= 1.0 - kx;
        p_vv -= kv * p_xv;
    }
}

double KalmanFilter::predict(double position, double deltat)
{
#if DEBUG
    printf("position: %f, velocity: %f, deltat: %f\n", position, estimate_velocity, deltat);
#endif

    return position + estimate_velocity * deltat;
}