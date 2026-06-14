#include "ZunTimer.hpp"
#include "Supervisor.hpp"

namespace th07
{

#pragma optimize("s", on)
void ZunTimer::Increment(i32 value)
{
    if ((g_Supervisor.frameBasedStuffFlags >> 5 & 1) != 0)
    {
        this->current = this->current + 1;
        this->subFrame = 0.0f;
        this->previous = -999;
    }

    if (g_Supervisor.framerateMultiplier > 0.99f)
    {
        this->current = this->current + value;
    }
    else
    {
        if (value < 0)
        {
            this->Decrement(-value);
        }
        else
        {
            this->previous = this->current;
            this->subFrame = (float)value * g_Supervisor.framerateMultiplier + this->subFrame;

            while (this->subFrame >= 1.0f)
            {
                this->current = this->current + 1;
                this->subFrame = this->subFrame - 1.0f;
            }
        }
    }

    return;
}

void ZunTimer::Decrement(i32 value)
{
    if ((g_Supervisor.frameBasedStuffFlags >> 5 & 1) != 0)
    {
        this->current = this->current - 1;
        this->subFrame = 0.0f;
        this->previous = -999;
    }

    if (g_Supervisor.framerateMultiplier > 0.99f)
    {
        this->current = this->current - value;
    }
    else
    {
        if (value < 0)
        {
            this->Increment(-value);
        }
        else
        {
            this->previous = this->current;
            this->subFrame = this->subFrame - (float)value * g_Supervisor.framerateMultiplier;

            while (this->subFrame < 0.0f)
            {
                this->current = this->current - 1;
                this->subFrame = this->subFrame + 1.0f;
            }
        }
    }

    return;
}

i32 ZunTimer::NextTick()
{
    this->Tick();
    return this->current;
}
#pragma optimize("s", off)
}; // namespace th07
