#pragma once

class KalmanFilter
{
    // Constructor / Destructor
public:
    KalmanFilter() : KalmanFilter(0.0){};
    KalmanFilter(double position);
    ~KalmanFilter(){};

    // methods
public:
    void reset(double position);
    void update(double position, double deltat);
    double predict(double position, double deltat);

    // internal state variables
private:
    double estimate_position;
    double estimate_velocity;
    double p_xx;
    double p_xv;
    double p_vv;
    double position_variance;
    double velocity_variance;
    double r;
    bool has_velocity;
};