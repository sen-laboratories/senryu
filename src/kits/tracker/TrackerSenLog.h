/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2024-2026 SEN Labs e.U.
 */
#pragma once

#include <syslog.h>

/**
 * @file TrackerSenLog.h
 * @brief Logging of the SEN parts of Tracker.
 *
 * Tracker is part of the Haiku tree and does not link against spdlog (which all other SEN code uses): it logs with
 * the macros of the platform. This header is the one place that decides where the messages go; errors are written to
 * the system log, where the syslog daemon's configuration filters them. Debug traces stay with Tracker's PRINT().
 */
#define ERROR(format...)	syslog(LOG_ERR, format)
