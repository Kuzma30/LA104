// PC test of the PID regulator core (Generator/Pid/CorePid.h)
//   g++ -Wall -Isource tests/pid_test.cpp && ./a.out
#include <stdio.h>
#include <math.h>
#include "Generator/Pid/CorePid.h"

static int failures = 0;
static void Check(const char* name, bool ok, const char* detail = "")
{
    printf("%-34s %s %s\n", name, ok ? "OK  " : "FAIL", detail);
    failures += !ok;
}

// first order plant y' = (gain*u - y)/tau, measured with a delay of 'lag' steps
struct Plant
{
    float tau, gain, y{0};
    float hist[64]{};
    int lag, pos{0};
    Plant(float t, float g, int l) : tau(t), gain(g), lag(l) {}
    float Step(float u, float dt)
    {
        y += (gain*u - y) * dt / (tau + dt);
        hist[pos++ & 63] = y;
        return hist[(pos - 1 - lag) & 63];
    }
};

struct Result { float overshoot, settle, final; };

static Result Run(float target, float kp, float ki, float kd, float tau, float gain, int lag,
    float seconds, float dt = 0.02f, float targetChangeAt = -1, float target2 = 0)
{
    CPid pid;
    pid.Reset(0);
    Plant plant(tau, gain, lag);
    float meas = 0, peak = 0, settle = -1;
    int steps = (int)(seconds / dt);
    for (int i = 0; i < steps; i++)
    {
        float t = i * dt;
        if (targetChangeAt >= 0 && t >= targetChangeAt)
            target = target2;
        float u = pid.Step(target, meas, dt, kp, ki, kd, 0, 2.25f);
        meas = plant.Step(u, dt);
        if (meas > peak) peak = meas;
        if (fabsf(meas - target) > 0.02f * target)
            settle = -1;
        else if (settle < 0)
            settle = t;
    }
    return { (peak - target) / target * 100, settle, meas };
}

int main()
{
    char buf[128];

    // defaults on the simulated plant of the device (tau 0.5 s)
    Result r = Run(1.0f, 0.8f, 2.0f, 0.0f, 0.5f, 1.0f, 0, 10);
    snprintf(buf, sizeof(buf), "overshoot %.1f %%, settles %.2f s, final %.4f V", r.overshoot, r.settle, r.final);
    Check("defaults reach the set point", fabsf(r.final - 1.0f) < 0.005f && r.settle > 0 && r.settle < 5, buf);

    // RC filter on Wave Out -> CH1 with a measurement delay of 2 steps
    r = Run(1.5f, 0.5f, 10.0f, 0.0f, 0.1f, 1.0f, 2, 10);
    snprintf(buf, sizeof(buf), "overshoot %.1f %%, settles %.2f s", r.overshoot, r.settle);
    Check("RC filter with delay", fabsf(r.final - 1.5f) < 0.01f, buf);

    // derivative on the measurement: changing the set point does not kick the output
    {
        CPid pid;
        pid.Reset(0);
        float meas = 0.5f;
        pid.Step(0.5f, meas, 0.02f, 0.2f, 1.0f, 1.0f, 0, 2.25f);
        float before = pid.m_fOutput;
        pid.Step(1.5f, meas, 0.02f, 0.2f, 1.0f, 1.0f, 0, 2.25f);
        float jump = pid.m_fOutput - before;
        snprintf(buf, sizeof(buf), "jump %.3f V (P part 0.2 V + I)", jump);
        Check("no derivative kick", jump < 0.3f, buf);
    }

    // anti-windup: unreachable set point for 10 s (plant gain 0.5 -> max 1.125 V),
    // then a reachable one; without anti-windup the integral would hold the
    // output at the limit for a long time
    r = Run(2.0f, 0.2f, 5.0f, 0.0f, 0.5f, 0.5f, 0, 14, 0.02f, 10.0f, 0.5f);
    snprintf(buf, sizeof(buf), "final %.4f V", r.final);
    Check("recovers after saturation", fabsf(r.final - 0.5f) < 0.01f, buf);
    {
        CPid pid;
        pid.Reset(0);
        Plant plant(0.5f, 0.5f, 0);
        float meas = 0;
        for (int i = 0; i < 500; i++)        // 10 s saturated
            meas = plant.Step(pid.Step(2.0f, meas, 0.02f, 0.2f, 5.0f, 0, 0, 2.25f), 0.02f);
        float t = 0;
        for (int i = 0; i < 500 && meas > 0.55f; i++, t += 0.02f)
            meas = plant.Step(pid.Step(0.5f, meas, 0.02f, 0.2f, 5.0f, 0, 0, 2.25f), 0.02f);
        snprintf(buf, sizeof(buf), "%.2f s to go from saturation to 0.55 V", t);
        Check("fast exit from saturation", t < 1.5f, buf);
    }

    // changing Ki while running does not make a jump (integral kept in volts)
    {
        CPid pid;
        pid.Reset(0);
        float meas = 1.0f;
        for (int i = 0; i < 100; i++)
            pid.Step(1.0f, meas, 0.02f, 0.2f, 5.0f, 0, 0, 2.25f);
        pid.Step(1.0f, 0.9f, 0.02f, 0.2f, 5.0f, 0, 0, 2.25f);
        float a = pid.m_fOutput;
        pid.Step(1.0f, 0.9f, 0.02f, 0.2f, 50.0f, 0, 0, 2.25f);
        float b = pid.m_fOutput;
        snprintf(buf, sizeof(buf), "%.3f -> %.3f V", a, b);
        Check("bumpless Ki change", fabsf(b - a) < 0.15f, buf);
    }

    // output limits
    {
        CPid pid;
        pid.Reset(0);
        float u = pid.Step(10.0f, 0, 0.02f, 100.0f, 0, 0, 0, 2.25f);
        float v = pid.Step(-10.0f, 0, 0.02f, 100.0f, 0, 0, 0, 2.25f);
        Check("output clamped to 0..2.25 V", u == 2.25f && v == 0.0f);
    }

    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures != 0;
}
