// SPDX-License-Identifier: MIT
//
// vshymanskyy/StreamDebugger — vendored from upstream into ZenoPCB IoT Library
// (https://github.com/vshymanskyy/StreamDebugger)
//
// Original Copyright (c) 2016 Volodymyr Shymanskyy.
// Licensed under MIT — see lib/ZenoPCB/src/vendor/StreamDebugger/VENDORED.md.
//
// Plan 06-2.5c (2026-06-02): class renamed from `StreamDebugger`
// to `ZenoStreamDebugger` to claim as ZenoPCB internal component.

/**
 * @file       StreamDebugger.h
 * @author     Volodymyr Shymanskyy
 * @license    This project is released under the MIT License (MIT)
 * @copyright  Copyright (c) 2016 Volodymyr Shymanskyy
 * @date       Nov 2016
 */

#ifndef StreamDebugger_h
#define StreamDebugger_h

class ZenoStreamDebugger
  : public Stream
{
  public:
    ZenoStreamDebugger(Stream& data, Stream& dump)
      : _data(data), _dump(dump)
    {}

    virtual size_t write(uint8_t ch) {
      _dump.write(ch);
      return _data.write(ch);
    }
    virtual int read() {
      int ch = _data.read();
      if (ch != -1) { _dump.write(ch); }
      return ch;
    }
    virtual int available() { return _data.available(); }
    virtual int peek()      { return _data.peek();      }
    virtual void flush()    { _data.flush();            }

    void directAccess() {
      while(true) {
        if (_data.available()) {
          _dump.write(_data.read());
        }
        if (_dump.available()) {
          _data.write(_dump.read());
        }
        delay(0);
      }
    }
  private:
    Stream& _data;
    Stream& _dump;
};

#endif
