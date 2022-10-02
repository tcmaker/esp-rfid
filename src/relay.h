#ifndef relay_h
#define relay_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#endif


// #include "Bounce2.h"
#include <inttypes.h>
// #include "magicnumbers.h"

class Relay 
{
public:
    uint8_t controlPin;

    enum ControlType {
        unknown = 255,
        activeLow = 0,
        activeHigh = 1
    };

    ControlType controlType;

    // actuationTime > 0 implies momentary
    unsigned long actuationTime;

    unsigned long lastMillis;

    // delayTime > 0 allows for tracking mechanical delays
    unsigned long delayTime;

    Relay *chainRelay;

    enum ChainType {
        none,
        activation,
        deactivation,
        both,
        both_same_order
    };

    enum OperationState {
        active,
        activating,
        inactive,
        deactivating
    };

    OperationState state;

    enum OverrideState {
        normal,
        holding,
        lockedout
    };

    OverrideState override;

/**
 * @brief Construct a new Relay object
 * 
 */
    Relay();
    Relay(uint8_t pin, ControlType type = activeLow, unsigned long actuation_ms = 2000, unsigned long delay_ms = 0);
    //Relay(uint8_t pin, ControlType type, unsigned long actuation_ms, unsigned long delay_ms);
    // Relay(uint8_t pin, int type, unsigned long actuationTime);

    void chain(const Relay *nextRelay, ChainType chaining);

    void begin();

    // OperationState status();

    void activate();

    void deactivate();

    void hold();
    void lockout();
    void release();

    bool update();

    bool isConfigured();

private:
    void debugPrint(const char* info);
};

#endif