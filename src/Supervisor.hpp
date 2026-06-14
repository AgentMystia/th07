#pragma once

// Supervisor.hpp - 骨架（stub）
//
// Supervisor 是大模块（~20 函数：TickTimer/OnUpdate/OnDraw/LoadConfig/音频/输入等），
// 完整反编译待后续。本骨架只声明 ZunTimer/utils 等当前模块用到的字段 + g_Supervisor 全局，
// 让依赖链编译通过。字段顺序/偏移未与 th07 实际对齐 —— objdiff ZunTimer 等依赖 Supervisor
// 字段偏移的函数会因偏移差异失败，待 Supervisor 完整反编译后校正。

#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
struct Supervisor
{
    // 骨架字段（占位，真实偏移待完整反编译）
    u8 padding[0x100]; // 占位让结构体有一定大小，真实字段布局待补
    f32 effectiveFramerateMultiplier;
    f32 framerateMultiplier;
};

DIFFABLE_EXTERN(Supervisor, g_Supervisor);
}; // namespace th07
