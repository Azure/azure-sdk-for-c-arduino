// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "SerialLogger.h"

#define UNIX_EPOCH_START_YEAR 1900

SerialLogger::SerialLogger() { }

void SerialLogger::Info(String message)
{
  Serial.print("[INFO] ");
  Serial.println(message);
}

void SerialLogger::Error(String message)
{
  Serial.print("[ERROR] ");
  Serial.println(message);
}

SerialLogger Logger;
