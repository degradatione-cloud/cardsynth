/*
 * SPDX-FileCopyrightText: 2026 Phillip
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <hal/hal.h>
#include <cstdint>

/**
 * @brief Battery - shows charge level and charging status
 *
 * Reads M5.Power directly (battery %, charging state, voltage) and
 * refreshes once a second, same periodic-update pattern as AppClock.
 */
class AppBattery : public mooncake::AppAbility {
public:
    AppBattery();
    ~AppBattery();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr uint32_t kUpdateIntervalMs = 1000;
    uint32_t _last_update_ms = 0;

    void render();
};
