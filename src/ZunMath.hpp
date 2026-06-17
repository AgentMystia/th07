#pragma once
#include "diffbuild.hpp"
#include "inttypes.hpp"
#include <Windows.h>
#include <d3dx8math.h>
#include <math.h>

struct ZunVec2
{
    f32 x;
    f32 y;

    f32 VectorLength()
    {
        return sqrt(this->x * this->x + this->y * this->y);
    }

    f64 VectorLengthF64()
    {
        return (f64)this->VectorLength();
    }

    D3DXVECTOR2 *AsD3dXVec()
    {
        return (D3DXVECTOR2 *)this;
    }
};
ZUN_ASSERT_SIZE(ZunVec2, 0x8);

struct ZunVec3
{
    f32 x;
    f32 y;
    f32 z;

    D3DXVECTOR3 *AsD3dXVec()
    {
        return (D3DXVECTOR3 *)this;
    }

    static void SetVecCorners(ZunVec3 *topLeftCorner, ZunVec3 *bottomRightCorner, const D3DXVECTOR3 *centerPosition,
                              const D3DXVECTOR3 *size)
    {
        topLeftCorner->x = centerPosition->x - size->x / 2.0f;
        topLeftCorner->y = centerPosition->y - size->y / 2.0f;
        bottomRightCorner->x = size->x / 2.0f + centerPosition->x;
        bottomRightCorner->y = size->y / 2.0f + centerPosition->y;
    }
};
ZUN_ASSERT_SIZE(ZunVec3, 0xC);

#define ZUN_MIN(x, y) ((x) > (y) ? (y) : (x))
#define ZUN_PI ((f32)(3.14159265358979323846))
#define ZUN_2PI ((f32)(ZUN_PI * 2.0f))

#define RADIANS(degrees) ((degrees * ZUN_PI / 180.0f))

// Compute sine and cosine of `in` simultaneously. orig used the x87 fsincos
// instruction; the th07 reimplementation uses the standard C library which is
// portable. (No current caller in the project — kept available for future use.)
#define sincos(in, out_sine, out_cosine)                                                                                 \
    {                                                                                                                    \
        *(out_sine) = sinf(in);                                                                                          \
        *(out_cosine) = cosf(in);                                                                                        \
    }

// Writes sine and cosine of `angle` into the provided out pointers.
void __inline fsincos_wrapper(f32 *out_sine, f32 *out_cosine, f32 angle)
{
    *out_sine = sinf(angle);
    *out_cosine = cosf(angle);
}

// Writes a unit-vector-scaled velocity: (cos*mult, sin*mult, 0) into out_vel.
void __inline sincosmul(D3DXVECTOR3 *out_vel, f32 input, f32 multiplier)
{
    out_vel->x = cosf(input) * multiplier;
    out_vel->y = sinf(input) * multiplier;
}

f32 __inline invertf(f32 x)
{
    return 1.f / x;
}

// Round to the nearest integer (banker's rounding semantics, matching the x87
// frndint default rounding mode of round-to-nearest-even).
f32 __inline rintf(f32 float_in)
{
    return floorf(float_in + 0.5f);
}
