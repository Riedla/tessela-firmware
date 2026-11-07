// Pedidos HTTP(S) à Tessela API (docs/DEVICE_PROTOCOL.md §2 e §14).
#pragma once

#include <Arduino.h>

enum class ApiResult : uint8_t { Ok, NotModified, Unauthorized, NetworkError, ServerError, BadResponse };

ApiResult apiPair(const String &code, String &message);  // POST /v1/hardware/pair → token na NVS
ApiResult apiCheckToken();                              // GET /v1/hardware/me (401 = emparelhar de novo)
ApiResult apiSyncSchedule(bool force = false);          // GET /v1/hardware/me/schedule + áudios → cartão
const char *apiResultName(ApiResult r);
