#ifndef door_h
#define door_h

#include "Bounce2.h"
#include <inttypes.h>
#include "magicnumbers.h"
#include "relay.h"

#define RELAY_LOCK 0
#define RELAY_OPEN 1
#define RELAY_CLOSE 2
#define RELAY_ALARM 3



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