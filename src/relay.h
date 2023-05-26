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


    // delayTime > 0 allows for tracking mechanical delays
    unsigned long delayTime;

    unsigned long lastMillis;

    // Relay *chainRelay;

    // enum ChainType {
    //     none,
    //     activation,
    //     deactivation,
    //     both,
    //     both_same_order
    // };

    enum OperationState {
        active,
        activating,
        inactive,
        deactivating
    };

    static const char* OperationState_Label[4];

    OperationState state;

    enum OverrideState {
        normal,
        holding,
        lockedout
    };

    static const char* OverrideState_Label[3];

    OverrideState override;

/**
 * @brief Construct a new Relay object
 * 
 */
    explicit
    Relay(uint8_t pin, 
          ControlType type = activeLow,
          unsigned long actuation_ms = 2000, 
          unsigned long delay_ms = 0);

    void begin();

    void (*onStateChangeCB)(OperationState operation, OverrideState override) = nullptr;

    // OperationState status();

    void activate(bool report = true);
    void deactivate(bool report = true);

    void hold();
    void lockout();
    void release();

    bool update();

    bool isConfigured();
};

#endif