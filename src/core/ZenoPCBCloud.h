#ifndef ZENOPCB_CLOUD_H
#define ZENOPCB_CLOUD_H

#include <Arduino.h>

namespace ZenoPCB
{
    // ============================================
    // Cloud endpoint XOR encoded, key = 0x5A
    // DO NOT store plaintext here.
    //
    // single-broker (, 2026-06-03): two-broker
    // isolation from dropped. Single broker
    // <mqtt-host>.<tld> for ZMG-01 + ZF-01 production firmware AND OSS
    // library users. The XOR-encoded byte array below decodes to the
    // 15-character canonical host string (verify via the python recipe
    // on the line above). -DZENOPCB_BROKER_HOST build-flag
    // escape hatch preserved (see ZenoPCB::_initMQTT for layered
    // override logic).
    //
    // To re-generate: python3 -c "k=0x5A; s='<broker>'; print([hex(ord(c)^k) for c in s])"
    // ============================================

    // Encoded broker bytes (15 chars, XOR with 0x5A)
    static const uint8_t _kBrokerEnc[] = {
        0x37, 0x2B, 0x2E, 0x2E, 0x74, 0x20, 0x3F, 0x34,
        0x35, 0x2A, 0x39, 0x38, 0x74, 0x2C, 0x34};

    static const uint16_t _kBrokerPort = 1883;
    static const uint8_t _kXorKey = 0x5A;

    /**
     * @brief Decode and return the default cloud broker address.
     *        Called once at runtime  result is not stored as a global string.
     *        volatile key prevents compiler from folding XOR at compile time.
     */
    inline String getCloudBroker()
    {
        volatile uint8_t key = _kXorKey; // volatile: force runtime XOR
        String out;
        for (size_t i = 0; i < sizeof(_kBrokerEnc); i++)
            out += (char)(_kBrokerEnc[i] ^ key);
        return out;
    }

    inline uint16_t getCloudPort()
    {
        return _kBrokerPort;
    }

} // namespace ZenoPCB

#endif // ZENOPCB_CLOUD_H
