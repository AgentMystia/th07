#include "Rng.hpp"

namespace th07
{
DIFFABLE_STATIC(Rng, g_Rng);

u16 Rng::GetRandomU16(void)
{
    u16 a = (this->seed ^ 0x9630) - 0x6553;

    this->seed = (((a & 0xc000) >> 14) + a * 4) & 0xFFFF;
    this->generationCount++;
    return this->seed;
}

u32 Rng::GetRandomU32(void)
{
    return GetRandomU16() << 16 | GetRandomU16();
}

f32 Rng::GetRandomF32ZeroToOne(void)
{
    return (i64)GetRandomU32() / 4294967296.0f;
}
}; // namespace th07
