/**
 * @file ZenoMultiConnectProvider.h
 * @brief Multi-network provider with priority-based failover
 *
 * Aggregates multiple ZenoNetworkProvider instances and automatically
 * switches to the highest-priority connected provider.
 *
 * Providers are tried in the order they are added (first = highest priority).
 * When the active provider disconnects, it fails over to the next available one.
 *
 * @note No special build flags required. This provider only uses the
 *       abstract ZenoNetworkProvider interface.
 *
 * @author ZenoPCB Team
 */

#pragma once

#include <vector>
#include "../core/ZenoNetworkProvider.h"
#include "../core/ZenoPCBTypes.h"
#include "../core/ZenoPCBDebug.h"

namespace ZenoPCB
{

    /**
     * @brief Multi-network provider with automatic failover
     *
     * Usage:
     * @code
     * ZenoEthernetProvider ethProvider(5, 26);
     * Zeno4GProvider cellProvider(17, 16, 4);
     *
     * ZenoMultiConnectProvider multiProvider;
     * multiProvider.addProvider(&ethProvider);  // Priority 1 (highest)
     * multiProvider.addProvider(&cellProvider); // Priority 2
     *
     * zeno.setNetworkProvider(&multiProvider).begin();
     * @endcode
     */
    class ZenoMultiConnectProvider : public ZenoNetworkProvider
    {
    public:
        ZenoMultiConnectProvider()
            : _activeProvider(nullptr), _activeIndex(-1) {}

        /**
         * @brief Add a network provider (first added = highest priority)
         * @param provider Pointer to a network provider (must outlive this object)
         */
        void addProvider(ZenoNetworkProvider *provider)
        {
            if (provider)
            {
                _providers.push_back(provider);
            }
        }

        bool begin(const DeviceConfig &config) override
        {
            ZENO_LOG("MULTI", "Initializing %d provider(s)...", (int)_providers.size());

            bool anyOK = false;
            for (size_t i = 0; i < _providers.size(); i++)
            {
                ZENO_LOG("MULTI", "[%d] Initializing %s...", (int)i, _providers[i]->getName());
                bool ok = _providers[i]->begin(config);
                ZENO_LOG("MULTI", "[%d] %s → %s", (int)i, _providers[i]->getName(),
                         ok ? "OK" : "FAILED");
                if (ok)
                    anyOK = true;
            }

            // Find first connected provider
            _selectActiveProvider();

            return anyOK;
        }

        void loop() override
        {
            // Run loop() on ALL providers (so they can maintain connections)
            for (auto *p : _providers)
            {
                p->loop();
            }

            // Re-evaluate active provider
            _selectActiveProvider();
        }

        bool isConnected() const override
        {
            return _activeProvider && _activeProvider->isConnected();
        }

        Client *getClient() override
        {
            return _activeProvider ? _activeProvider->getClient() : nullptr;
        }

        Client *getOTAClient() override
        {
            // Delegate to active provider's OTA client
            return _activeProvider ? _activeProvider->getOTAClient() : nullptr;
        }

        String getLocalIP() const override
        {
            return _activeProvider ? _activeProvider->getLocalIP() : "0.0.0.0";
        }

        const char *getName() const override
        {
            if (_activeProvider)
            {
                return _activeProvider->getName();
            }
            return "MultiConnect";
        }

        /**
         * @brief Get the currently active provider (or nullptr)
         */
        ZenoNetworkProvider *getActiveProvider() const
        {
            return _activeProvider;
        }

        /**
         * @brief Get number of registered providers
         */
        size_t getProviderCount() const
        {
            return _providers.size();
        }

    private:
        void _selectActiveProvider()
        {
            // Find highest-priority connected provider
            ZenoNetworkProvider *newActive = nullptr;
            int newIndex = -1;

            for (size_t i = 0; i < _providers.size(); i++)
            {
                if (_providers[i]->isConnected())
                {
                    newActive = _providers[i];
                    newIndex = (int)i;
                    break; // First connected = highest priority
                }
            }

            // Log switchover
            if (newActive != _activeProvider)
            {
                if (_activeProvider && newActive)
                {
                    ZENO_LOG("MULTI", "Failover: %s → %s (IP: %s)",
                             _activeProvider->getName(),
                             newActive->getName(),
                             newActive->getLocalIP().c_str());
                }
                else if (newActive)
                {
                    ZENO_LOG("MULTI", "Active: %s (IP: %s)",
                             newActive->getName(),
                             newActive->getLocalIP().c_str());
                }
                else if (_activeProvider)
                {
                    ZENO_LOG("MULTI", "All providers disconnected");
                }
                _activeProvider = newActive;
                _activeIndex = newIndex;
            }
        }

        std::vector<ZenoNetworkProvider *> _providers;
        ZenoNetworkProvider *_activeProvider;
        int _activeIndex;
    };

} // namespace ZenoPCB
