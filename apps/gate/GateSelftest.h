#pragma once

namespace smartpark{
namespace gate{

// 道闸状态机 + 离线队列自测（无网络），返回失败数。
//
// 状态机部分用 VirtualGateClock 推进虚拟时间，因此整套时序（抬杆 700ms、
// 保持 5000ms、落闸 700ms、故障告警 4000ms）在毫秒内跑完且完全确定，
// 不依赖真实等待，也不会因负载抖动超时。
int runSelftest();

} // namespace gate
} // namespace smartpark
