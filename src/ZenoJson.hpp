// ArduinoJson - https://arduinojson.org
// Copyright © 2014-2026, Benoit BLANCHON
// MIT License

#pragma once

#if __cplusplus < 201103L && (!defined(_MSC_VER) || _MSC_VER < 1910)
#  error ArduinoJson requires C++11 or newer. Configure your compiler for C++11 or downgrade ArduinoJson to 6.20.
#endif

#include "ZenoJson/Configuration.hpp"

// Include Arduino.h before stdlib.h to avoid conflict with atexit()
// https://github.com/bblanchon/ArduinoJson/pull/1693#issuecomment-1001060240
#if ARDUINOJSON_ENABLE_ARDUINO_STRING || ARDUINOJSON_ENABLE_ARDUINO_STREAM || \
    ARDUINOJSON_ENABLE_ARDUINO_PRINT ||                                       \
    (ARDUINOJSON_ENABLE_PROGMEM && defined(ARDUINO))
#  include <Arduino.h>
#endif

#if !ARDUINOJSON_DEBUG
#  ifdef __clang__
#    pragma clang system_header
#  elif defined __GNUC__
#    pragma GCC system_header
#  endif
#endif

// Remove true and false macros defined by some cores, such as Arduino Due's
// See issues #2181 and arduino/ArduinoCore-sam#50
#ifdef true
#  undef true
#endif
#ifdef false
#  undef false
#endif

#include "ZenoJson/Array/JsonArray.hpp"
#include "ZenoJson/Object/JsonObject.hpp"
#include "ZenoJson/Variant/JsonVariantConst.hpp"

#include "ZenoJson/Document/JsonDocument.hpp"

#include "ZenoJson/Array/ArrayImpl.hpp"
#include "ZenoJson/Array/ElementProxy.hpp"
#include "ZenoJson/Array/Utilities.hpp"
#include "ZenoJson/Collection/CollectionImpl.hpp"
#include "ZenoJson/Memory/ResourceManagerImpl.hpp"
#include "ZenoJson/Object/MemberProxy.hpp"
#include "ZenoJson/Object/ObjectImpl.hpp"
#include "ZenoJson/Variant/ConverterImpl.hpp"
#include "ZenoJson/Variant/JsonVariantCopier.hpp"
#include "ZenoJson/Variant/VariantCompare.hpp"
#include "ZenoJson/Variant/VariantImpl.hpp"
#include "ZenoJson/Variant/VariantRefBaseImpl.hpp"

#include "ZenoJson/Json/JsonDeserializer.hpp"
#include "ZenoJson/Json/JsonSerializer.hpp"
#include "ZenoJson/Json/PrettyJsonSerializer.hpp"
#include "ZenoJson/MsgPack/MsgPackBinary.hpp"
#include "ZenoJson/MsgPack/MsgPackDeserializer.hpp"
#include "ZenoJson/MsgPack/MsgPackExtension.hpp"
#include "ZenoJson/MsgPack/MsgPackSerializer.hpp"

#include "ZenoJson/compatibility.hpp"
