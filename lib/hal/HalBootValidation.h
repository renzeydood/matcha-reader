#pragma once

namespace HalBootValidation {
// True when this application still needs its first successful normal startup.
bool begin();
// Call only after normal initialization and a completed Home-screen render.
bool confirmHealthyBoot();
}  // namespace HalBootValidation
