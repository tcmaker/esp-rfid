#ifndef door_h
#define door_h

#include "Bounce2.h"
#include <inttypes.h>
#include "magicnumbers.h"
#include "relay.h"

// #define RELAY_LOCK 0
// #define RELAY_INDICATOR 1
// #define RELAY_ALARM 2
// #define RELAY_OPEN 3
// #define RELAY_CLOSE 4
// #define RELAY_COUNT 5

class BounceWithCB : public Bounce 
{
public:
    void (*onStateChangeCB)(bool newState) = nullptr;
    bool update();
    void begin();
};


class Door
{
public:
    /**
     * @brief Construct a new Door object
     *
     */
    explicit Door(Bounce *statusPin,
                  Relay *lock = nullptr,
                  Relay *indicator = nullptr,
                  Relay *alarm = nullptr,
                  Relay *open = nullptr,
                  Relay *close = nullptr);

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
    // int actionOpenTime;

    enum DoorState
    {
        secure,
        unlocked,
        open,
        tamper,
        locking,
        alarm,
        unalarming
        // start
    };

protected:
    DoorState state;
    unsigned long lastMilli;

public:
    struct relay_set_t
    {
        Relay *unlock;    // unlock relay releases interlock when activated
        Relay *indicator; // indicator relay powers green light/external buttons when activated
        Relay *open;      // actuates the open button when activated
        Relay *close;     // actuates the open button when activated
        Relay *alarm;     // actuates powers the alarm siren when activated
    } relays;

    // Relay *relays[RELAY_COUNT] = {nullptr};

    // Relay *relayUnlock;
    // Relay *relayInd;
    // Relay *relayOpen;
    // Relay *relayClose;
    // Relay *relayAlarm;

    // void (*activateCallBack)();
    // void (*doorStateCallBack)(DoorState state);
    void (*tamperCallBack)(bool on) = nullptr;
    void (*alarmCallBack)(bool on) = nullptr;
    // void unlock();
    // void open();
    // void close();
    // void lock(bool force = false);

    void begin();

    void activate();
    void deactivate();
    bool update();
    DoorState status();
};
#endif