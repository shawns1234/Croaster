#pragma once
// Fan driver seam. The control task only needs: set a percentage, read it back,
// and know whether the fan can actually be running (mains present). Concrete
// drivers: FanDimmer (AC phase angle, RobotDyn-style), FanPwm (DC fan via LEDC).
class FanOutput {
public:
    virtual ~FanOutput() = default;
    virtual bool begin() = 0;
    virtual void set(float percent) = 0; // 0 = off, 100 = full; any task
    virtual float get() const = 0;
    // false when the driver can tell the fan has no power (AC: no zero-cross).
    // Drivers that cannot tell return true.
    virtual bool mainsDetected() const = 0;
};
