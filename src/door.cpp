// door lock
// nardev -> functions in relation to opening doors with card or with door opening button, should be moved here

#include "door.h"

Relay::Relay() 
  : controlType(0)
  , actuationTime(2000)
  {}

Relay::Relay(uint8_t pin, int type)
  : controlPin(pin)
  , controlType(type)
  , actuationTime(2000)
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

void Relay::forceOff()
{
#ifdef DEBUG
      Serial.printf("milliseconds: %lu - Relay::forceOff()\n", millis());
#endif
  digitalWrite(controlPin, 0);
}

void Relay::forceOn()
{
#ifdef DEBUG
      Serial.printf("milliseconds: %lu - Relay::forceOn()\n", millis());
#endif
  digitalWrite(controlPin, 1);
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
  state = 0;
  lastMillis = millis();
  this->forceOff();

  if (this->chainRelay) 
  {
    if (this->delayTime == 0)
    {
      this->chainRelay->trigger();
    }
    else 
    {
      state |= 2; // indicate that update handle chain trigger
    }
  }

  if (this->actuationTime == 0)
  {
    this->forceOn();
  }
  else 
  {
    state |= 1; //indicate that update should handle actuation time
  }
}

/**
 * @brief Stop any delayed changes, reset relay state to default.
 * 
 */
void Relay::begin()
{
  state = 0;
  this->forceOn();
}

bool Relay::update() 
{
  bool acted = false;
  if (state & 1) 
  {
    if (millis() > lastMillis + actuationTime) 
    {
      this->forceOn();
      state &= ~1;
      acted = true;
    }
  }
  if (state & 2)
  {
    if (millis() > lastMillis + delayTime)
    {
      this->chainRelay->trigger();
      state &= ~2;
      acted = true;
    }
  }
  return acted;
}


///////////////////////
//////   DOOR    /////
/////////////////////

Door::Door(Relay *lock = 0, Bounce *statusPin = 0)
{
  state = 0;
  lastMilli = 0;  
  
  if (lock)
  {
    relays[RELAY_LOCK] = lock;
  }

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

  relayLock = relays[0];
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
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door activating");
#endif
  if (relayOpen) // setup firing sequence
  {
    relayLock->chainRelay = relayOpen;
  }
  else 
  {
    relayLock->chainRelay = nullptr;
  }
  state = 1;
  lastMilli = millis();
  relayLock->trigger();
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door unlocked");
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
  relayLock->update();
  bool acted = false;
  switch (state)
  {
  case 0: // Door secure (closed and locked)
    // relayAlarm->forceOff();
    if (this->statusPin->rose())
    {
      state = 4; //tamper
      // send alarm
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("tamper detected");
#endif
    }
    break;
  case 1: // Door unlocked
    // relayAlarm->forceOff();
    if (this->statusPin->rose())
    {
      lastMilli = millis();
      state = 2; // Door open
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door opened");
#endif
    }
    else
    {
      if (millis() > lastMilli + 10000) {
        relayLock->forceOn();
        state = 10; // verify door still closed after lock
        acted = true;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door relocking");
#endif
      }
      // else maintain state
    }
    break;
  case 10: // intermeidate state open/unlocked to locked
    if (this->statusPin->rose()) // door opened before lock succeeded
    {
      // unlock door, go back to open state
      relayLock->forceOff();
      state = 2;
      acted = true;
      // do not reset lastMillis
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door failed to relock -- door open");
#endif

    } else { // door succesfully locked
      state = 0;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door locked");
#endif

    }
    break;
  case 2: // Door open
    if (this->statusPin->fell()) // door has been closed
    {
      relayLock->forceOn();
      state = 10;
      acted = true;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door closed -- door locking");
#endif
    }
    else if (lastMilli - millis() > this->maxOpenTime * 1000)
    {
      // relayAlarm->trigger();
      state = 3;
      acted = true;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door exceeded maxOpenTime -- alarm on");
#endif
    }
    // else maintain state
    break;
  case 3: // Alarm state
    if (this->statusPin->fell())
    {
      relayLock->forceOn();
      state = 11; // verify door locked from alarm state
      acted = true;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door closed -- door locking");
#endif
    }
    break;
  case 11: // transitiion from alarm to secure
    if (this->statusPin->rose()) // door re-opened before lock succeeded
    {
      relayLock->forceOff();
      state = 3; // back to alarm state
      acted = true;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door failed to relock -- door open");
#endif
    }
    else 
    {
      state = 0;
      // relayAlarm->forceOff();
      acted = true;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("door closed -- alarm off");
#endif
    }
    break;
  case 4:
    if (lastMilli - millis() > 10000) {
      // relayAlarm->forceOn();
      state = 3;
      acted = true;
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("tamper alarm triggered");
#endif
    }
    break;
  default:
    state = 0;
    // log error
#ifdef DEBUG
      Serial.printf("milliseconds: %lu\n", millis());
      Serial.println("Door::update(): unknown state");
#endif
    break;
  }
  return acted;
}