#ifndef door_h
#define door_h

#include "Bounce2.h"
#include <inttypes.h>
#include "magicnumbers.h"

#define RELAY_LOCK 0
#define RELAY_OPEN 1
#define RELAY_CLOSE 2
#define RELAY_ALARM 3

// #include <Arduino.h>

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

    void trigger();

    void activate();

    void deactivate();

    void hold();
    void lockout();
    void release();

    bool update();

    bool isConfigured();
};

class Door
{
    public:
    /**
     * @brief Construct a new Door object
     * 
     */
        Door(Relay *lock = 0, Bounce *statusPin = 0);
        // Door(Bounce *statusPin);
        // Door(Relay *lock);

    /**
     * @brief ESP Bounce pin used to read the status of the door "pressed" indicates door is closed.
     * 
     */
        Bounce *statusPin;

    /**
     * @brief The maximum time the door can be open before sending an alarm message and/or trying to close the door.
     * 
     */
        unsigned long maxOpenTime;
        //int actionOpenTime;

        enum DoorState {
            secure,
            unlocked,
            open,
            tamper,
            locking,
            alarm,
            unalarming
        };
    protected:
        DoorState state;
        unsigned long lastMilli;

    public:
        Relay *relays[MAX_NUM_RELAYS] = {nullptr};

        Relay *relayUnlock;
        Relay *relayOpen;
        Relay *relayClose;
        Relay *relayAlarm;

        // void unlock();
        // void open();
        // void close();
        // void lock(bool force = false);

        void begin();



    /**
     * @brief Triggers the sequence of events necessary to open the door. 
     * 
     */
        void activate();


    /**
     * @brief Triggers the sequence of events necessary to close the door.
     * 
     */
        void deactivate();

    /**
     * @brief Runs the door statemachine, sends alarms, triggers relays
     * 
     * @return true when the state machine changed a relay state
     * @return false when the state machine did not physically act
     */
        bool update();

    /**
     * @brief 
     * 
     * @return int representing current state machine state
     */
        int status();
};
#endif