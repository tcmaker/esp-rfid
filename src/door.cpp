// door lock
// nardev -> functions in relation to opening doors with card or with door opening button, should be moved here

#include "door.h"

void debugPrint(const char* info) {
  Serial.printf("milliseconds: %lu - %s\n", millis(), info);
}

Relay::Relay() 
  : controlType(unknown)
  , actuationTime(2000)
  {}

Relay::Relay(uint8_t pin, ControlType type , unsigned long actuation_ms, unsigned long delay_ms)
  : controlPin(pin)
  , controlType(type)
  , actuationTime(actuation_ms)
  , delayTime(delay_ms)
  {}

/**
 * @brief Has the relay been configured to operate?
 * 
 * @return true if ready
 */
bool Relay::isConfigured()
{
  return controlType != 0;
}

void Relay::activate()
{
  debugPrint("Relay::activate()");
  if (override != normal)
    return;

  digitalWrite(controlPin, controlType);
  lastMillis = millis();
  if (delayTime > 0) {
    state = activating;
  } else {
    state = active;
  }
}

void Relay::deactivate()
{
  debugPrint("Relay::deactivate()");
  if (override != normal)
    return;
  
  digitalWrite(controlPin, !controlType);
  lastMillis = millis();
  if (delayTime > 0) {
    state = deactivating;
  } else {
    state = inactive;
  }
}


void Relay::hold() {
  activate();
  override = holding;
}

void Relay::lockout() {
  deactivate();
  override = lockedout;
}

void Relay::release() {
  override = normal;
}

/**
 * @brief Stop any delayed changes, reset relay state to default.
 * 
 */
void Relay::begin()
{
  override = normal;
  this->deactivate();
  state = inactive;
}

bool Relay::update() 
{
  bool acted = false;
  const unsigned long now = millis();

  switch (state)
  {
  case activating:
  case deactivating:
    if (now > lastMillis + delayTime) 
    {
      state = (state == activating) ? active : inactive;
      // if (this->chainRelay) {
      //   this->chainRelay->trigger();
      //   acted = true;
      // }
    }
    break;
  case active:
    if (override == normal && (actuationTime > 0) && (now > lastMillis + actuationTime))
    {
      deactivate();
      acted = true;
    }
    break;
  default: // inactive
    if (override == normal) {
      digitalWrite(controlPin, !controlType);
    }
    break;
  }
  return acted;
}

/**
 * @brief Trigger the relay with activation time and possibly delayed chained relay(s)
 * 
 */
void Relay::trigger()
{
#ifdef DEBUG
      Serial.printf("milliseconds: %lu - Relay::trigger()\n", millis());
#endif
  // state = 0;
  // lastMillis = millis();
  activate();

  if (this->chainRelay) 
  {
    if (millis() > lastMillis + delayTime)
    {
      this->state = active;
      if (this->chainRelay) {
        this->chainRelay->trigger();
      }
    }
  }
}


///////////////////////
//////   DOOR    /////
/////////////////////

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
 * @brief Initiate the chain of events necessary to open the door.
 * 
 */
void Door::activate()
{
#ifdef DEBUG
      Serial.printf("milliseconds: %lu - door activating\n", millis());
#endif
  if (relayOpen) // setup firing sequence
  {
    relayUnlock->chainRelay = relayOpen;
  }
  else 
  {
    relayUnlock->chainRelay = nullptr;
  }
  state = unlocked;
  lastMilli = millis();
  relayUnlock->trigger();
#ifdef DEBUG
      Serial.printf("milliseconds: %lu - door unlocked\n", millis());
#endif

}

bool Door::update()
{
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
    if (this->statusPin->read() == 1)
    {
      lastMilli = millis();
      state = tamper; //tamper
      // send alarm
      debugPrint("tamper detected");
    }
    break;
  /////////////////////////////////////////////
  case unlocked: // Door unlocked
    // this state is only set by the "activate" method
    // relayAlarm->forceOff();
    if (this->statusPin->read() == 1) // door has been opened
    {
      lastMilli = millis();
      relayUnlock->hold();
      state = open; // Door open
      debugPrint("door opened");
    }
    else
    {
      if (relayUnlock->state == Relay::OperationState::inactive) {
        lastMilli = millis();
        state = locking; // to verify door still closed after lock
        debugPrint("door relocking");
      }
      // else maintain state
    }
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