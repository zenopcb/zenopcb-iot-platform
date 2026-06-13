#ifndef ZENOPCB_ZENO_JSON_H
#define ZENOPCB_ZENO_JSON_H

// ZenoPCB JSON facade — wraps vendored ArduinoJson (renamed to ZenoJson namespace
// to avoid conflict with any external ArduinoJson the user may have installed).
//
// Provides type aliases under namespace ZenoPCB::Json so library code (and future
// Plan 01-08 wrapper classes) can refer to `ZenoPCB::Json::Document` etc. without
// depending on the upstream namespace name.
//
// Original ArduinoJson library: Copyright (c) Benoit Blanchon. MIT.
// See lib/ZenoPCB/src/vendor/ArduinoJson/LICENSE.md

#include "../ZenoJson.h"

namespace ZenoPCB {
namespace Json {
    using Document = ::ZenoJson::JsonDocument;
    using Object = ::ZenoJson::JsonObject;
    using ObjectConst = ::ZenoJson::JsonObjectConst;
    using Array = ::ZenoJson::JsonArray;
    using ArrayConst = ::ZenoJson::JsonArrayConst;
    using Variant = ::ZenoJson::JsonVariant;
    using VariantConst = ::ZenoJson::JsonVariantConst;
    using String = ::ZenoJson::JsonString;
} // namespace Json
} // namespace ZenoPCB

#endif // ZENOPCB_ZENO_JSON_H
