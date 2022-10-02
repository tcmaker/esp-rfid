// door lock
// nardev -> functions in relation to opening doors with card or with door opening button, should be moved here

#include "door.h"

void debugPrint(const char* info) {
  Serial.printf("milliseconds: %lu - %s\n", millis(), info);
}

Door::Door(Relay *lock, Bounce *statusPin)
  : maxOpenTime(10)
  , state(open)
  , lastMilli(millis())
{
  relays[RELAY_LOCK] = lock;
  this->statusPin = statusPin;
}

void Door::begin()
{
  if (statusPin) 
  {
    statusPin->update();
  }

  for (auto i = 0; i < MAX_NUM_RELAYS; ++i)
  {
    if (relays[i] && relays[i]->isConfigured()) 
    {
      relays[i]->begin();
    }
  }

  relayUnlock = relays[0];
  relayOpen = relays[1];
  relayClose = relays[2];
  relayAlarm = relays[3];
}

int Door::status()
{
  return state;
}

/**
 * @brief Initiate the chain of events necessary to unlock/open the door.
 * 
 */
void Door::activate()
{
#ifdef DEBUG
      Serial.printf("milliseconds: %lu - door activating\n", millis());
#endif
  state = unlocked;
  lastMilli = millis();
  relayUnlock->activate();
#ifdef DEBUG
      Serial.printf("milliseconds: %lu - door unlocked\n", millis());
#endif
}

bool Door::update()
{
  static bool trigger_open = false;

  for (auto i = 0; i < MAX_NUM_RELAYS; ++i)
  {
    if (relays[i] && relays[i]->isConfigured()) 
    {
      relays[i]->update();
    }
  }

  if (statusPin) {
    statusPin->update();
  }

  bool acted = false;

  switch (state)
  {
  case secure: // (closed and locked)
    // relayAlarm->forceOff();
    
    // exits from this state are fob scan and tamper
    // only the fob scan exit will result in triggering an open signal
    trigger_open = true;

    if (this->statusPin->read() == 1)
    {
      lastMilli = millis();
      trigger_open = false;
      state = tamper; //tamper
      // send alarm
      debugPrint("tamper detected");
    }

    // fob scan will move to "unlocked" state via activate() method
    break;
  /////////////////////////////////////////////
  case unlocked: // Door unlocked
    // this state is only set by the activate() method
    // exits are door opens or unlock relay deactivates
    if (this->statusPin->read() == 1) // door has been opened
    {
      lastMilli = millis();
      // interlock is held unlocked while door is open
      relayUnlock->hold();
      state = open; // Door open
      debugPrint("door opened");
    }
    else if (trigger_open && relayUnlock->state == Relay::OperationState::active)
    {
      // trigger open one time if transitioned from secure state
      // wait until unlock relay has fully moved from inactive -> activating -> active

      trigger_open = false;
      if (relayOpen && relayOpen->state == Relay::OperationState::inactive)
      {
        relayOpen->activate();
      }  
    }
    else if (relayUnlock->state == Relay::OperationState::inactive) { // lock relay has automatically deactivated
      lastMilli = millis();
      state = locking; // to verify door still closed after lock
      debugPrint("door relocking");
    }
    // else maintain state
    break;
  /////////////////////////////////////////////
  case locking: // intermediate state: open/unlocked to locked
    if (this->statusPin->read() == 1) // door opened before lock succeeded
    {
               // unlock door, go back to open state
      // do not reset lastMillis
      relayUnlock->hold();
      state = open;
      acted = true;
      debugPrint("door failed to relock in time -- door open");
    } else if (relayUnlock->state == Relay::OperationState::inactive) { // door succesfully locked
      state = secure;
      debugPrint("door closed and locked");
    }
    break;
  /////////////////////////////////////////////
  case open: // Door open
    if (this->statusPin->read() == 0) // door has been closed
    {
      relayUnlock->release();  // does not deactivate() in case actuationTime is still running.
      state = locking;
      acted = true;
      debugPrint("door closed -- door locking");
    }
    else if (millis() > lastMilli + this->maxOpenTime * 1000)
    {
      // relayAlarm->trigger();
      state = alarm;
      acted = true;
      debugPrint("door exceeded maxOpenTime -- alarm on");
    }
    // else maintain state
    break;
  /////////////////////////////////////////////
  case alarm: // Alarm state
    if (this->statusPin->read() == 0)
    {
      relayUnlock->release();
      relayUnlock->deactivate();
      state = unalarming; // to verify door locked from alarm state
      acted = true;
      debugPrint("door closed -- door locking");
    }
    break;
  /////////////////////////////////////////////
  case unalarming: // transitiion from alarm to secure
    if (this->statusPin->read() == 1) // door re-opened before lock succeeded
    {
      relayUnlock->hold();
      state = alarm; // back to alarm state
      acted = true;
      debugPrint("door failed to relock -- door open -- alarming");
    }
    else 
    {
      state = secure;
      // relayAlarm->forceOff();
      acted = true;
      debugPrint("door closed -- alarm off");
    }
    break;
  /////////////////////////////////////////////
  case tamper:
    if (this->statusPin->read() == 0)
    {
      // relayUnlock->release();
      // relayUnlock->deactivate();
      state = secure; // to verify door locked from alarm state
      acted = true;
      debugPrint("door closed -- door locked");
    } 
    else if (millis() > lastMilli + 10000) 
    {
      // relayAlarm->forceOn();
      relayUnlock->hold();
      state = alarm;
      acted = true;
      debugPrint("tamper alarm triggered");
    }
    break;
  /////////////////////////////////////////////
  default:
    lastMilli = millis();
    state = alarm;
    // log error
    debugPrint("Door::update(): unknown state");
    break;
  }
  return acted;
}