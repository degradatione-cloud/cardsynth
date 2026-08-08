/*
 * SPDX-FileCopyrightText: 2026 Phillip
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_battery.h"
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <M5Unified.hpp>
#include <cstdio>

using namespace mooncake;

AppBattery::AppBattery()
{
    setAppInfo().name = "Battery";
}

AppBattery::~AppBattery() {}

void AppBattery::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    render();
}

void AppBattery::onRunning()
{
    if (GetHAL().millis() - _last_update_ms > kUpdateIntervalMs) {
        render();
        _last_update_ms = GetHAL().millis();
    }

    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppBattery::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppBattery::render()
{
    auto& canvas = GetHAL().canvas;
    const int W = canvas.width();
    const int H = canvas.height();

    int32_t level              = M5.Power.getBatteryLevel();
    m5::Power_Class::is_charging_t charging = M5.Power.isCharging();
    int16_t mv                  = M5.Power.getBatteryVoltage();

    canvas.fillScreen(THEME_COLOR_BG);

    canvas.setTextSize(1);
    canvas.setCursor(0, 0);
    canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    canvas.print("Battery");

    canvas.setTextSize(3);
    uint16_t levelColor = level < 20 ? TFT_RED : (level < 50 ? TFT_YELLOW : TFT_GREEN);
    canvas.setTextColor(levelColor, THEME_COLOR_BG);
    char pctBuf[8];
    snprintf(pctBuf, sizeof(pctBuf), "%d%%", static_cast<int>(level));
    canvas.drawCenterString(pctBuf, W / 2, H / 2 - 10);

    canvas.setTextSize(1);
    const char* statusText;
    uint16_t statusColor;
    switch (charging) {
        case m5::Power_Class::is_charging:
            statusText  = "CHARGING";
            statusColor = TFT_GREEN;
            break;
        case m5::Power_Class::is_discharging:
            statusText  = "ON BATTERY";
            statusColor = TFT_WHITE;
            break;
        default:
            statusText  = "UNKNOWN";
            statusColor = TFT_DARKGREY;
            break;
    }
    canvas.setTextColor(statusColor, THEME_COLOR_BG);
    canvas.drawCenterString(statusText, W / 2, H / 2 + 22);

    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    char voltBuf[24];
    if (mv > 0) {
        snprintf(voltBuf, sizeof(voltBuf), "%.2fV", mv / 1000.0f);
    } else {
        snprintf(voltBuf, sizeof(voltBuf), "-- V");
    }
    canvas.drawCenterString(voltBuf, W / 2, H - 10);

    GetHAL().pushCanvas();
}
