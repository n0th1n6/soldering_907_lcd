#include <LiquidCrystal.h>
#include <TimerOne.h>
#include <EEPROM.h>


// The LCD 0802 parallel interface
const byte LCD_RS_PIN     = 13;
const byte LCD_E_PIN      = 12;
const byte LCD_DB4_PIN    = 5;
const byte LCD_DB5_PIN    = 6;
const byte LCD_DB6_PIN    = 7;
const byte LCD_DB7_PIN    = 8;

// Rotary encoder interface
const byte R_MAIN_PIN = 2;                      // Rotary Encoder main pin (right)
const byte R_SECD_PIN = 4;                      // Rotary Encoder second pin (left)
const byte R_BUTN_PIN = 3;                      // Rotary Encoder push button pin

const byte probePIN  = A0;                      // Thermometer pin from soldering iron
const byte heaterPIN = 10;                      // The soldering iron heater pin
const byte buzzerPIN = 11;                      // The simple buzzer to make a noise

const uint16_t temp_minC = 180;                 // Minimum temperature in degrees of celsius
const uint16_t temp_maxC = 400;                 // Maximum temperature in degrees of celsius
const uint16_t temp_minF = (temp_minC *9 + 32*5 + 2)/5;
const uint16_t temp_maxF = (temp_maxC *9 + 32*5 + 2)/5;
const byte INTENSITY = 3;                       // display intensity (overwitten by brhghtness configuration parameter)

//------------------------------------------ class FastPWDdac --------------------------------------------------
/*
FastPWMdac
Copyright (C) 2015  Albert van Dalen http://www.avdweb.nl
*/

class FastPWMdac {
  public:
    void init(byte _timer1PWMpin, byte resolution);
    void analogWrite8bit(byte value8bit);
    void analogWrite10bit(int value10bit);
  private:
    byte timer1PWMpin;
};


void FastPWMdac::init(byte _timer1PWMpin, byte resolution){
  timer1PWMpin = _timer1PWMpin;
  if(resolution == 8) Timer1.initialize(32);
  if(resolution == 10) Timer1.initialize(128);
  Timer1.pwm(timer1PWMpin, 0);                  // dummy, required before setPwmDuty()
}

void FastPWMdac::analogWrite8bit(byte value8bit){
  Timer1.setPwmDuty(timer1PWMpin, value8bit*4); // faster than pwm()
}

void FastPWMdac::analogWrite10bit(int value10bit) {
  Timer1.setPwmDuty(timer1PWMpin, value10bit); // faster than pwm()
}

//------------------------------------------ Configuration data ------------------------------------------------
/* Config record in the EEPROM has the following format:
  uint32_t ID                           each time increment by 1
  struct cfg                            config data, 8 bytes
  byte CRC                              the checksum
*/
struct cfg {
  uint16_t temp_min;                            // The minimum temperature (180 centegrees)
  uint16_t temp_max;                            // The temperature for 400 centegrees
  uint16_t temp;                                // The temperature of the iron to be start
  byte     brightness;                          // The display brightness [0-15]
  bool     celsius;                             // Temperature units: true - celsius, false - farenheit
};

class CONFIG {
  public:
    CONFIG() {
      can_write = is_valid = false;
      buffRecords = 0;
      rAddr = wAddr = 0;
      eLength = 0;
      nextRecID = 0;
      save_calibration = false;
    }
    void init();
    bool load(void);
    bool isValid(void)    { return is_valid; }
    uint16_t temp(void)   { return Config.temp; }
    byte getBrightness(void) { return Config.brightness; }
    bool getTempUnits(void)  { return Config.celsius; }
    bool saveTemp(uint16_t t);
    void saveConfig(byte bright, bool cels);
    void saveCalibrationData(uint16_t t_max, uint16_t t_min);
    void getCalibrationData(uint16_t& t_max, uint16_t& t_min);
    void setDefaults(bool Write = false);
  private:
    struct cfg Config;
    bool readRecord(uint16_t addr, uint32_t &recID);
    bool save(void);
    bool can_write;                             // Tha flag indicates that data can be saved
    bool is_valid;                              // Weither tha data was loaded
    bool save_calibration;                      // Weither the calibration data should be saved
    byte buffRecords;                           // Number of the records in the outpt buffer
    uint16_t rAddr;                             // Address of thecorrect record in EEPROM to be read
    uint16_t wAddr;                             // Address in the EEPROM to start write new record
    uint16_t eLength;                           // Length of the EEPROM, depends on arduino model
    uint32_t nextRecID;                         // next record ID
    const byte record_size = 16;                // The size of one record in bytes
    const uint16_t def_min = 554;               // Default minimum temperature
    const uint16_t def_max = 900;               // Default maximun temperature
    const uint16_t def_set = 600;               // Default setup temperature
};

 // Read the records until the last one, point wAddr (write address) after the last record
void CONFIG::init(void) {
  eLength = EEPROM.length();
  byte t, p ,h;
  uint32_t recID;
  uint32_t minRecID = 0xffffffff;
  uint16_t minRecAddr = 0;
  uint32_t maxRecID = 0;
  uint16_t maxRecAddr = 0;
  byte records = 0;

  setDefaults();
  nextRecID = 0;

  // read all the records in the EEPROM find min and max record ID
  for (uint16_t addr = 0; addr < eLength; addr += record_size) {
    if (readRecord(addr, recID)) {
      ++records;
      if (minRecID > recID) {
        minRecID = recID;
        minRecAddr = addr;
      }
      if (maxRecID < recID) {
        maxRecID = recID;
        maxRecAddr = addr;
      }
    } else {
      break;
    }
  }

  if (records == 0) {
    wAddr = rAddr = 0;
    can_write = true;
    return;
  }

  rAddr = maxRecAddr;
  if (records < (eLength / record_size)) {      // The EEPROM is not full
    wAddr = rAddr + record_size;
    if (wAddr > eLength) wAddr = 0;
  } else {
    wAddr = minRecAddr;
  }
  can_write = true;
}

bool CONFIG::saveTemp(uint16_t t) {
  if (!save_calibration && (t == Config.temp)) return true;
  Config.temp = t;
  save_calibration = false;
  return save();  
}

void CONFIG::saveConfig(byte bright, bool cels) {
  if ((bright >= 0) && (bright <= 15))
    Config.brightness = bright;
  Config.celsius = cels;
  save();                                       // Save new data into the EEPROM
}

void CONFIG::saveCalibrationData(uint16_t t_max, uint16_t t_min) {
  Config.temp_max  = t_max;
  Config.temp_min  = t_min;
  save_calibration = true;
}

void CONFIG::getCalibrationData(uint16_t& t_max, uint16_t& t_min) {
  t_max = Config.temp_max;
  t_min = Config.temp_min;
}

bool CONFIG::save(void) {
  if (!can_write) return can_write;
  if (nextRecID == 0) nextRecID = 1;

  uint16_t startWrite = wAddr;
  uint32_t nxt = nextRecID;
  byte summ = 0;
  for (byte i = 0; i < 4; ++i) {
    EEPROM.write(startWrite++, nxt & 0xff);
    summ <<=2; summ += nxt;
    nxt >>= 8;
  }
  byte* p = (byte *)&Config;
  for (byte i = 0; i < sizeof(struct cfg); ++i) {
    summ <<= 2; summ += p[i];
    EEPROM.write(startWrite++, p[i]);
  }
  summ ++;                                      // To avoid empty records
  EEPROM.write(wAddr+record_size-1, summ);

  rAddr = wAddr;
  wAddr += record_size;
  if (wAddr > EEPROM.length()) wAddr = 0;
  return true;
}

bool CONFIG::load(void) {

  is_valid = readRecord(rAddr, nextRecID);
  nextRecID ++;
  if (is_valid) {
    if (Config.temp_min >= Config.temp_max) {
      setDefaults();
    }
    if ((Config.temp > Config.temp_max) || (Config.temp < Config.temp_min)) Config.temp = def_set;
    if ((Config.brightness < 0) || (Config.brightness > 15)) Config.brightness = INTENSITY;
  }
  return is_valid;
}

bool CONFIG::readRecord(uint16_t addr, uint32_t &recID) {
  byte Buff[16];

  for (byte i = 0; i < 16; ++i) 
    Buff[i] = EEPROM.read(addr+i);
  
  byte summ = 0;
  for (byte i = 0; i < sizeof(struct cfg) + 4; ++i) {

    summ <<= 2; summ += Buff[i];
  }
  summ ++;                                      // To avoid empty fields
  if (summ == Buff[15]) {                       // Checksumm is correct
    uint32_t ts = 0;
    for (char i = 3; i >= 0; --i) {
      ts <<= 8;
      ts |= Buff[i];
    }
    recID = ts;
    byte i = 4;
    memcpy(&Config, &Buff[4], sizeof(struct cfg));
    return true;
  }
  return false;
}

void CONFIG::setDefaults(bool Write) {          // Restore default values
  Config.temp = def_set;
  Config.temp_min = def_min;
  Config.temp_max = def_max;
  Config.brightness = INTENSITY;                // Default display brightness [0-15]
  Config.celsius = true;                        // Default use celsius
  if (Write) {
    save();
    save_calibration = false;
  }
}

//------------------------------------------ class BUZZER ------------------------------------------------------
class BUZZER {
  public:
    BUZZER(byte BuzzerPIN) { buzzerPIN = BuzzerPIN; }
    void shortBeep(void)  { tone(buzzerPIN, 3520, 160); }
  private:
    byte buzzerPIN;
};

//------------------------------------------ class BUTTON ------------------------------------------------------
class BUTTON {
  public:
    BUTTON(byte ButtonPIN, unsigned int timeout_ms = 3000) {
      pt = tickTime = 0;
      buttonPIN = ButtonPIN;
      overPress = timeout_ms;
    }
    void init(void) { pinMode(buttonPIN, INPUT_PULLUP); }
    void setTimeout(uint16_t timeout_ms = 3000) { overPress = timeout_ms; }
    byte intButtonStatus(void) { byte m = mode; mode = 0; return m; }
    void cnangeINTR(void);
    byte buttonCheck(void);
    bool buttonTick(void);
  private:
    volatile byte mode;                         // The button mode: 0 - not pressed, 1 - pressed, 2 - long pressed
    const uint16_t tickTimeout = 200;           // Period of button tick, while tha button is pressed 
    const uint16_t shortPress = 900;            // If the button was pressed less that this timeout, we assume the short button press
    uint16_t overPress;                         // Maxumum time in ms the button can be pressed
    volatile uint32_t pt;                       // Time in ms when the button was pressed (press time)
    uint32_t tickTime;                          // The time in ms when the button Tick was set
    byte buttonPIN;                             // The pin number connected to the button
};

void BUTTON::cnangeINTR(void) {                 // Interrupt function, called when the button status changed
  
  bool keyUp = digitalRead(buttonPIN);
  unsigned long now_t = millis();
  if (!keyUp) {                                 // The button has been pressed
    if ((pt == 0) || (now_t - pt > overPress)) pt = now_t; 
  } else {
    if (pt > 0) {
      if ((now_t - pt) < shortPress) mode = 1;  // short press
        else mode = 2;                          // long press
      pt = 0;
    }
  }
}

byte BUTTON::buttonCheck(void) {                // Check the button state, called each time in the main loop

  mode = 0;
  bool keyUp = digitalRead(buttonPIN);          // Read the current state of the button
  uint32_t now_t = millis();
  if (!keyUp) {                                 // The button is pressed
    if ((pt == 0) || (now_t - pt > overPress)) pt = now_t;
  } else {
    if (pt == 0) return 0;
    if ((now_t - pt) > shortPress)              // Long press
      mode = 2;
    else
      mode = 1;
    pt = 0;
  } 
  return mode;
}

bool BUTTON::buttonTick(void) {                 // When the button pressed for a while, generate periodical ticks

  bool keyUp = digitalRead(buttonPIN);          // Read the current state of the button
  uint32_t now_t = millis();
  if (!keyUp && (now_t - pt > shortPress)) {    // The button have been pressed for a while
    if (now_t - tickTime > tickTimeout) {
       tickTime = now_t;
       return (pt != 0);
    }
  } else {
    if (pt == 0) return false;
    tickTime = 0;
  } 
  return false;
}

//------------------------------------------ class ENCODER ------------------------------------------------------
class ENCODER {
  public:
    ENCODER(byte aPIN, byte bPIN, int16_t initPos = 0) {
      pt = 0; mPIN = aPIN; sPIN = bPIN; pos = initPos;
      min_pos = -32767; max_pos = 32766; channelB = false; increment = 1;
      changed = 0;
      is_looped = false;
    }
    void init(void) {
      pinMode(mPIN, INPUT_PULLUP);
      pinMode(sPIN, INPUT_PULLUP);
    }
    void reset(int16_t initPos, int16_t low, int16_t upp, byte inc = 1, byte fast_inc = 0, bool looped = false) {
      min_pos = low; max_pos = upp;
      if (!write(initPos)) initPos = min_pos;
      increment = fast_increment = inc;
      if (fast_inc > increment) fast_increment = fast_inc;
      is_looped = looped;
    }
    void set_increment(byte inc) { increment = inc; }
    byte get_increment(void) { return increment; }
    bool write(int16_t initPos) {
      if ((initPos >= min_pos) && (initPos <= max_pos)) {
        pos = initPos;
        return true;
      }
      return false;
    }
    int16_t read(void) { return pos; }
    void cnangeINTR(void);
  private:
    const uint16_t overPress = 1000;
    int32_t min_pos, max_pos;
    volatile uint32_t pt;                       // Time in ms when the encoder was rotaded
    volatile uint32_t changed;                  // Time in ms when the value was changed
    volatile bool channelB;
    volatile int16_t pos;                       // Encoder current position
    byte mPIN, sPIN;                            // The pin numbers connected to the main channel and to the socondary channel
    bool is_looped;                             // Weither the encoder is looped
    byte increment;                             // The value to add or substract for each encoder tick
    byte fast_increment;                        // The value to change encoder when in runs quickly
    const uint16_t fast_timeout = 300;          // Time in ms to change encodeq quickly
};

void ENCODER::cnangeINTR(void) {                // Interrupt function, called when the channel A of encoder changed
  
  bool rUp = digitalRead(mPIN);
  unsigned long now_t = millis();
  if (!rUp) {                                   // The channel A has been "pressed"
    if ((pt == 0) || (now_t - pt > overPress)) {
      pt = now_t;
      channelB = digitalRead(sPIN);
    }
  } else {
    if (pt > 0) {
      byte inc = increment;
      if ((now_t - pt) < overPress) {
        if ((now_t - changed) < fast_timeout) inc = fast_increment;
        changed = now_t;
        if (channelB) pos -= inc; else pos += inc;
        if (pos > max_pos) { 
          if (is_looped)
            pos = min_pos;
          else 
            pos = max_pos;
        }
        if (pos < min_pos) {
          if (is_looped)
            pos = max_pos;
          else
            pos = min_pos;
        }
      }
      pt = 0; 
    }
  }
}

//------------------------------------------ class lcd DSPLay for soldering iron -----------------------------
class DSPL : protected LiquidCrystal {
  public:
    DSPL(byte RS, byte E, byte DB4, byte DB5, byte DB6, byte DB7) : LiquidCrystal(RS, E, DB4, DB5, DB6, DB7) { }
    void init(void);
    void clear(void) { LiquidCrystal::clear(); }
    void tSet(uint16_t t, bool celsuis);        // Show the temperature set
    void tCurr(uint16_t t);                     // Show The current temperature
    void pSet(byte p);                          // Show the power set
    void tempLim(byte indx, uint16_t temp);     // Show the upper or lower temperature limit
    void msgNoIron(void);                       // Show 'No iron' message
    void msgReady(void);                        // Show 'Ready' message
    void msgOn(void);                           // Show 'On' message
	  void msgOff(void);                          // Show 'Off' message
	  void msgCold(void);                         // Show 'Cold' message
	  void msgFail(void);                         // Show 'Fail' message
	  void msgTune(void);                         // Show 'Tune' message
	  void msgCelsius(void);                      // Show 'Cels.' message
	  void msgFarneheit(void);                    // Show 'Faren.' message
    void msgUpper(void);                        // Show 'setting upper temperature' process
    void msgLower(void);                        // Show 'setting lower temperature' process
    void msgDefault();                          // Show 'default' message (load default configuratuin)
    void msgCancel(void);                       // Show 'cancel' message
    void heating(void) {}                       // Do not animate heating process
    void cooling(void) {}                       // Do not animate cooling process
    void setupMode(byte mode, byte p = 0);      // Show the configureation mode [0 - 2]
    void noAnimation(void) {}                   // Switch off the animation
    void show(void) {}                          // No animation ever
    void percent(byte Power);                   // Show the percentage
};

void DSPL::init(void) {
  LiquidCrystal::begin(8, 2);
  LiquidCrystal::clear();
}

void DSPL::tSet(uint16_t t, bool celsius) {
  char buff[5];
  char units = 'C';
  if (!celsius) units = 'F';
  LiquidCrystal::setCursor(0, 0);
  sprintf(buff, "%3d%c", t, units);
  LiquidCrystal::print(buff);
}

void DSPL::tCurr(uint16_t t) {
  char buff[4];
  LiquidCrystal::setCursor(0, 1);
  if (t < 1000) {
    sprintf(buff, "%3d", t);
  } else {
    LiquidCrystal::print(F("xxx"));
    return;
  }
  LiquidCrystal::print(buff);
}

void DSPL::pSet(byte p) {
  char buff[6];
  sprintf(buff, "P:%3d", p);
  LiquidCrystal::setCursor(0, 0);
  LiquidCrystal::print(buff);
}

void DSPL::tempLim(byte indx, uint16_t temp) {
  char buff[9];
  if (indx == 0) {
    buff[0] = 'u';
    buff[1] = 'p';
  } else {
    buff[0] = 'l';
    buff[1] = 'o';
  }
  sprintf(&buff[2], ": %3d ", temp);
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(buff);
}

void DSPL::msgNoIron(void) {
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(F("no iron "));
}

void DSPL::msgReady(void) {
  LiquidCrystal::setCursor(4, 0);
  LiquidCrystal::print(F(" rdy"));
}

void DSPL::msgOn(void) {
  LiquidCrystal::setCursor(4, 0);
  LiquidCrystal::print(F("  ON"));
}

void DSPL::msgOff(void) {
  LiquidCrystal::setCursor(4, 0);
  LiquidCrystal::print(F(" OFF"));
}

void DSPL::msgCold(void) {
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(F("  cold  "));
}

void DSPL::msgFail(void) {
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(F(" Failed "));
}

void DSPL::msgTune(void) {
  LiquidCrystal::setCursor(0, 0);
  LiquidCrystal::print(F("Tune"));
}

void DSPL::msgCelsius(void) {
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(F("Celsius "));
}

void DSPL::msgFarneheit(void) {
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(F("Faren.  "));
}
void DSPL::msgUpper(void) {
  LiquidCrystal::setCursor(6, 0);
  LiquidCrystal::print(F("up"));
}

void DSPL::msgLower(void) {
  LiquidCrystal::setCursor(6, 0);
  LiquidCrystal::print(F("lo"));
}

void DSPL::msgDefault() {
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(F(" default"));
}

void DSPL::msgCancel(void) {
  LiquidCrystal::setCursor(0, 1);
  LiquidCrystal::print(F(" cancel "));
}

void DSPL::setupMode(byte mode, byte p) {
  LiquidCrystal::clear();
  LiquidCrystal::print(F("setup"));
  LiquidCrystal::setCursor(1,1);
  switch (mode) {
    case 0:
	  LiquidCrystal::print(F("bright"));
	  break;
    case 1:
      LiquidCrystal::print(F("units"));
      LiquidCrystal::setCursor(7,1);
      if (p)
        LiquidCrystal::print("C");
      else
        LiquidCrystal::print("F");
      break;
    case 2:
      LiquidCrystal::print(F("tune"));
      break;
  }
}

void DSPL::percent(byte Power) {
  char buff[6];
  sprintf(buff, " %3d%c", Power, '%');
  LiquidCrystal::setCursor(3, 1);
  LiquidCrystal::print(buff);
}

//------------------------------------------ class HISTORY ----------------------------------------------------
#define H_LENGTH 8
class HISTORY {
  public:
    HISTORY(void) { len = 0; }
    void init(void) { len = 0; }
    void put(int item) {
      if (len < H_LENGTH) {
        queue[len++] = item;
      } else {
        for (byte i = 0; i < len-1; ++i) queue[i] = queue[i+1];
        queue[H_LENGTH-1] = item;
      }
    }
    bool  isFull(void)                          { return len == H_LENGTH; }
    int   last(void)                            { return queue[len-1]; }
    int   top(void)                             { return queue[0]; }
    int   average(void);
    float dispersion(void);
    float gradient(void);
  private:
    int queue[H_LENGTH];
    byte len;
};

int HISTORY::average(void) {
  long sum = 0;
  if (len == 0) return 0;
  if (len == 1) return queue[0];
  for (byte i = 0; i < len; ++i) sum += queue[i];
  sum += len >> 1;                              // round the average
  sum /= len;
  return (int)sum;
}

float HISTORY::dispersion(void) {
  if (len < 3) return 1000;
  long sum = 0;
  long avg = average();
  for (byte i = 0; i < len; ++i) {
    long q = queue[i];
    q -= avg;
    q *= q;
    sum += q;
  }
  sum += len << 1;
  float d = (float)sum / (float)len;
  return d;
}

// approfimating the history with the line (y = ax+b) using method of minimum square. Gradient is parameter a
float HISTORY::gradient(void) {
  if (len < 2) return 0;
  long sx, sx_sq, sxy, sy;
  sx = sx_sq = sxy = sy = 0;
  for (byte i = 1; i <= len; ++i) {
    sx    += i;
  sx_sq += i*i;
  sxy   += i*queue[i-1];
  sy    += queue[i-1];
  }
  long numerator   = len * sxy - sx * sy;
  long denominator = len * sx_sq - sx * sx;
  float a = (float)numerator / (float)denominator;
  return a;
}

//------------------------------------------ class PID algoritm to keep the temperature -----------------------
/*  The PID algoritm 
 *  Un = Kp*(Xs - Xn) + Ki*summ{j=0; j<=n}(Xs - Xj) + Kd(Xn-1 - Xn)
 *  In this program the interactive formulae is used:
 *    Un = Un-1 + Kp*(Xn-1 - Xn) + Ki*(Xs - Xn) + Kd*(2*Xn-1 - Xn - Xn-2)
 *  With the first step:
 *  U0 = Kp*(Xs - X0) + Ki*(Xs - X0); Xn-1 = Xn;
 */
class PID {
  public:
    PID(void) {}
    void resetPID(int temp = -1);               // reset PID algoritm history parameters
    // Calculate the power to be applied
    int reqPower(int temp_set, int temp_curr, int power);
  private:
    int  temp_hist[2];                          // previously measured temperature
    bool  pid_iterate;                          // Weither the inerative PID formulae can be used
    const byte denominator_p = 8;               // The common coefficeient denominator power of 2 (8 means divide by 256)
    const int Kp = 1024;                        // Kp multiplied by denominator
    const int Ki = 900;                         // Ki multiplied by denominator
    const int Kd = 1700;                        // Kd multiplied by denominator
};

void PID::resetPID(int temp) {
  pid_iterate = false;
  temp_hist[0] = 0;
  if (temp > 0)
    temp_hist[1] = temp;
  else
    temp_hist[1] = 0;
}

int PID::reqPower(int temp_set, int temp_curr, int power) {
  if (temp_hist[0] == 0) {                      // first, use the direct formulae, not the iterate process
    long p = (long)Kp*(temp_set - temp_curr) + (long)Ki*(temp_set - temp_curr);
    p += (1 << (denominator_p-1));
    p >>= denominator_p;
    temp_hist[1] = temp_curr;
    if ((temp_set - temp_curr) < 30) {          // If the temperature is near, prepare the PID iteration process
      if (!pid_iterate) {                       // The first loop
        pid_iterate = true;                   
      } else {                                  // The second loop
        temp_hist[0] = temp_hist[1];            // Now we are redy to use iterate algorythm
      }
    }
    power = p;
  } else {
    long delta_p = (long)Kp * (temp_hist[1] - temp_curr);
    delta_p += (long)Ki * (temp_set - temp_curr);
    delta_p += (long)Kd * (2*temp_hist[1] - temp_hist[0] - temp_curr);
    delta_p += (1 << (denominator_p-1));
    delta_p >>= denominator_p;
    power += delta_p;
    temp_hist[0] = temp_hist[1];
    temp_hist[1] = temp_curr;
  }
  return power;
}

//------------------------------------------ class soldering iron ---------------------------------------------
class IRON : protected PID {
  public:
    IRON(byte heater_pin, byte sensor_pin) {
      hPIN = heater_pin;
      sPIN = sensor_pin;
      on = false;
      unit_celsius = true;
      fix_power = false;
      unit_celsius = true;
      no_iron = true;
    }
    void     init(uint16_t t_max, uint16_t t_min);
    void     switchPower(bool On);
    bool     isOn(void)                         { return on; }
    bool     isCold(void)                       { return (h_temp.last() < temp_cold); }
    bool     noIron(void)                       { return no_iron; }
    void     setTempUnits(bool celsius)         { unit_celsius = celsius; }
    bool     getTempUnits(void)                 { return unit_celsius; }
    uint16_t getTemp(void)                      { return temp_set; }
    uint16_t tempAverage(void)                  { return h_temp.average(); }
    uint16_t tempDispersion(void)               { return h_temp.dispersion(); }
    byte     getMaxFixedPower(void)             { return max_fixed_power; }
    void     setTemp(int t);                    // Set the temperature to be keeped
    // Set the temperature to be keeped in human readable units (celsius or farenheit)
    void     setTempHumanUnits(int t);
    // Translate internal temperature to the celsius or farenheit
    uint16_t temp2humanUnits(uint16_t temp);
    byte     getAvgPower(void);                 // Average applied power
    byte     appliedPower(void);                // Power applied to the solder [0-100%]
    byte     hotPercent(void);                  // How hot is the iron (used in the idle state)
    void     keepTemp(void);                    // Main solder iron loop
    bool     fixPower(byte Power);              // Set the specified power to the the soldering iron
  private:
    uint16_t temp(void);                        // Read the actual temperature of the soldering iron
    void     applyPower(void);                  // Check the the power limits and apply power to the heater
    FastPWMdac fastPWMdac;                      // Power the irom using fastPWMdac
    uint32_t   checkMS;                         // Milliseconds to measure the temperature next time
    byte       hPIN, sPIN;                      // The heater PIN and the sensor PIN
    int        power;                           // The soldering station power
    byte       actual_power;                    // The power supplied to the iron
    bool       on;                              // Weither the soldering iron is on
    bool       fix_power;                       // Weither the soldering iron is set the fix power
    bool       no_iron;                         // Weither the iron is connected
    bool       unit_celsius;                    // Human readable units for the temparature (celsius or farenheit)
    int        temp_set;                        // The temperature that should be keeped
    bool       iron_checked;                    // Weither the iron works
    int        temp_start;                      // The temperature when the solder was switched on
    uint32_t   elapsed_time;                    // The time elipsed from the start (ms)
    uint16_t   temp_min;                        // The minimum temperature (180 centegrees)
    uint16_t   temp_max;                        // The maximum temperature (400 centegrees)
    HISTORY    h_power;
    HISTORY    h_temp;
    const uint16_t temp_cold   = 340;           // The cold temperature to touch the iron safely
    const uint16_t temp_no_iron = 980;          // Sensor reading whae the iron disconnected
    const byte max_power       = 180;           // maximum power to the iron (220)
    const byte max_fixed_power = 120;           // Maximum power in fiexed power mode
    const byte delta_t         = 2;             // The measurement error of the temperature
    const uint16_t period      = 500;           // The period to check the soldering iron temperature, ms
    const int check_time       = 10000;         // Time in ms to check weither the solder is heating
    const int heat_expected    = 10;            // The iron should change the temperature at check_time
};

void IRON::setTemp(int t) {
  if (on) resetPID();
  temp_set = t;
}

void IRON::setTempHumanUnits(int t) {
  int temp;
  if (unit_celsius) {
    if (t < temp_minC) t = temp_minC;
    if (t > temp_maxC) t = temp_maxC;
    temp = map(t+1, temp_minC, temp_maxC, temp_min, temp_max);
  } else {
    if (t < temp_minF) t = temp_minF;
    if (t > temp_maxF) t = temp_maxF;
    temp = map(t+2, temp_minF, temp_maxF, temp_min, temp_max);
  }
  for (byte i = 0; i < 10; ++i) {
    int tH = temp2humanUnits(temp);
    if (tH <= t) break;
    --temp;
  }
  setTemp(temp);
}

uint16_t IRON::temp2humanUnits(uint16_t temp) {
  if (!unit_celsius)  return map(temp, temp_min, temp_max, temp_minF, temp_maxF);
  return map(temp, temp_min, temp_max, temp_minC, temp_maxC);  
}

byte IRON::getAvgPower(void) {
  int p = h_power.average();
  return p & 0xff;  
}

byte IRON::appliedPower(void) {
  byte p = getAvgPower(); 
  return map(p, 0, max_power, 0, 100);  
}

byte IRON::hotPercent(void) {
  uint16_t t = h_temp.average();
  char r = map(t, temp_cold, temp_set, 0, 100);
  if (r < 0) r = 0;
  return r;
}

void IRON::init(uint16_t t_max, uint16_t t_min) {
  pinMode(sPIN, INPUT);
  fastPWMdac.init(hPIN, 8);                     // initialization for 8 bit resolution
  fastPWMdac.analogWrite8bit(0);                // sawtooth output, period = 31.25Khz
  on = false;
  fix_power = false;
  power = 0;
  actual_power = 0;
  checkMS = 0;

  elapsed_time = 0;
  temp_start = analogRead(sPIN);
  iron_checked = false;
  temp_max = t_max; temp_min = t_min;

  resetPID();
  h_power.init();
  h_temp.init();
}

void IRON::switchPower(bool On) {
  on = On;
  if (!on) {
    fastPWMdac.analogWrite8bit(0);
    fix_power = false;
    return;
  }

  resetPID(analogRead(sPIN));
  h_power.init();
  checkMS = millis();
}

uint16_t IRON::temp(void) {
  int16_t temp = 0;
  if (actual_power > 0) fastPWMdac.analogWrite8bit(0);
  int16_t t1 = analogRead(sPIN);
  delayMicroseconds(500);
  int16_t t2 = analogRead(sPIN);
  if (actual_power > 0) fastPWMdac.analogWrite8bit(actual_power);

  if (abs(t1 - t2) < 10) {
    t1 += t2 + 1;
    t1 >>= 1;                                   // average of two measurements
    temp = t1;
  } else {
    int tprev = h_temp.last();
    if (abs(t1 - tprev) < abs(t2 - tprev))
      temp = t1;
    else
      temp = t2;
  }

  // If the power is off and no iron detected, do not put the temperature into the history 
  if (!on && !fix_power && (temp > temp_no_iron)) {
    no_iron = true;
  } else {
    no_iron = false;
    h_temp.put(temp);
  }

  return temp;
}

void IRON::keepTemp(void) {
  if (checkMS > millis()) return;
  checkMS = millis() + period;

  int temp_curr = temp();                       // Read the temperature and save it to the history buffer periodically

  if (!on) {                                    // If the soldering iron is set to be switched off
    if (!fix_power)
      fastPWMdac.analogWrite8bit(0);            // Surely power off the iron
    return;
  }
   
  // Check weither the iron can be heated
  if (!iron_checked) {
    elapsed_time += period;
    if (elapsed_time >= check_time) {
      if ((abs(temp_set - temp_curr) < 100) || ((temp_curr - temp_start) > heat_expected)) {
        iron_checked = true;
      } else {
        switchPower(false);                     // Prevent the iron damage
        elapsed_time = 0;
        temp_start = analogRead(sPIN);
        iron_checked = false;
      }
    }
  }

  // Use PID algoritm to calculate power to be applied
  power = reqPower(temp_set, temp_curr, power);
  applyPower();
}

void IRON::applyPower(void) {
  int p = power;
  if (p < 0) p = 0;
  if (p > max_power) p = max_power;

  if (h_temp.last() > (temp_set + 1)) p = 0;
  if (p == 0) actual_power = 0;
  if (on) actual_power = p & 0xff;
  h_power.put(p);
  fastPWMdac.analogWrite8bit(actual_power);
}

bool IRON::fixPower(byte Power) {
  if (Power == 0) {                             // To switch off the iron, set the power to 0
    fix_power = false;
    actual_power = 0;
    fastPWMdac.analogWrite8bit(0);
    return true;
  }

  if (Power > max_fixed_power) {
    actual_power = 0;
    return false;
  }

  if (!fix_power) {
    fix_power = true;
    power = Power;
    actual_power = power & 0xff;
  } else {
    if (power != Power) {
      power = Power;
      actual_power = power & 0xff;
    }
  }
  fastPWMdac.analogWrite8bit(actual_power);
  return true;
}

//------------------------------------------ class SCREEN ------------------------------------------------------
class SCREEN {
  public:
    SCREEN* next;                               // Pointer to the next screen
    SCREEN* nextL;                              // Pointer to the next Level screen, usually, setup
    SCREEN* main;                               // Pointer to the main screen
    SCREEN() {
      next = nextL = main = 0;
      force_redraw = true;
      scr_timeout = 0;
      time_to_return = 0;
    }
    virtual void init(void) { }
    virtual void show(void) { }
    virtual SCREEN* menu(void) {if (this->next != 0) return this->next; else return this; }
    virtual SCREEN* menu_long(void) { if (this->nextL != 0) return this->nextL; else return this; }
    virtual void rotaryValue(int16_t value) { }
    bool isSetup(void){ return (scr_timeout != 0); }
    void forceRedraw(void) { force_redraw = true; }
    SCREEN* returnToMain(void) {
      if (main && (scr_timeout != 0) && (millis() >= time_to_return)) {
        scr_timeout = 0;
        return main;
      }
      return this;
    }
    void resetTimeout(void) {
      if (scr_timeout > 0)
        time_to_return = millis() + scr_timeout*1000;
    }
    void setSCRtimeout(uint16_t t) {
      scr_timeout = t;
      resetTimeout(); 
    }
  protected:
    bool force_redraw;
    uint16_t scr_timeout;                       // Timeout is sec. to return to the main screen, canceling all changes
    uint32_t time_to_return;                    // Time in ms to return to main screen
};

//---------------------------------------- class mainSCREEN [the soldering iron is OFF] ------------------------
class mainSCREEN : public SCREEN {
  public:
    mainSCREEN(IRON* Iron, DSPL* DSP, ENCODER* ENC, BUZZER* Buzz, CONFIG* Cfg) {
      update_screen = 0;
      pIron = Iron;
      pD = DSP;
      pEnc = ENC;
      pBz = Buzz;
      pCfg = Cfg;
      is_celsius = true;
    }
    virtual void init(void);
    virtual void show(void);
    virtual void rotaryValue(int16_t value);
  private:
    IRON*    pIron;                             // Pointer to the iron instance
    DSPL*    pD;                                // Pointer to the DSPLay instance
    ENCODER* pEnc;                              // Pointer to the rotary encoder instance
    BUZZER*  pBz;                               // Pointer to the simple buzzer instance
    CONFIG*  pCfg;                              // Pointer to the configuration instance
    uint32_t update_screen;                     // Time in ms to switch information on the display
    bool     used;                              // Weither the iron was used (was hot)
    bool     cool_notified;                     // Weither there was cold notification played
    bool     is_celsius;                        // The temperature units (Celsius or farenheit)
    bool     no_iron;                           // 'No iron connected' message displayed
	  const uint16_t period = 1000;               // The period to update the screen
};

void mainSCREEN::init(void) {
  no_iron = false;
  pIron->switchPower(false);
  uint16_t temp_set = pIron->getTemp();
  is_celsius = pCfg->getTempUnits();
  pIron->setTempUnits(is_celsius);
  uint16_t tempH = pIron->temp2humanUnits(temp_set);
  if (is_celsius)
    pEnc->reset(tempH, temp_minC, temp_maxC, 1, 5);
  else
    pEnc->reset(tempH, temp_minF, temp_maxF, 1, 5);
  update_screen = millis();
  pD->clear();
  pD->tSet(tempH, is_celsius);
  pD->msgOff();
  forceRedraw();
  uint16_t temp = pIron->tempAverage();
  used = ((temp > 450) && (temp < 950));
  cool_notified = !used;
  if (used) {                                   // the iron was used, we should save new data in EEPROM
    pCfg->saveTemp(temp_set);
  }
}

void mainSCREEN::rotaryValue(int16_t value) {
  update_screen = millis() + period;
  pIron->setTempHumanUnits(value);
  pD->tSet(value, is_celsius);
}

void mainSCREEN::show(void) {
  if ((!force_redraw) && (millis() < update_screen)) return;

  force_redraw = false;
  update_screen = millis();

  if (pIron->noIron()) {                        // No iron connected
    pD->msgNoIron();
    no_iron = true;                             // 'no iron' message is displayed
    return;
  } else {
   if (no_iron) {                               // clear the 'no iron' message
    no_iron = false;
    pD->clear();
    uint16_t temp_set = pIron->getTemp();
    temp_set = pIron->temp2humanUnits(temp_set);
    pD->tSet(temp_set, is_celsius);
    pD->msgOff();
   }
  }

  update_screen += period;
  uint16_t temp = pIron->tempAverage();
  temp = pIron->temp2humanUnits(temp);
  if (used && pIron->isCold()) {
    pD->msgCold();
    if (!cool_notified) {
      pBz->shortBeep();
      cool_notified = true;
    }
  } else {
    pD->tCurr(temp);
    pD->msgOff();
  }
}

//---------------------------------------- class workSCREEN [the soldering iron is ON] -------------------------
class workSCREEN : public SCREEN {
  public:
    workSCREEN(IRON* Iron, DSPL* DSP, ENCODER* Enc, BUZZER* Buzz) {
      update_screen = 0;
      pIron = Iron;
      pD    = DSP;
      pBz   = Buzz;
      pEnc   = Enc;
      ready = false;
    }
    virtual void init(void);
    virtual void show(void);
    virtual void rotaryValue(int16_t value);
  private:
    uint32_t update_screen;                     // Time in ms to update the screen
    IRON* pIron;                                // Pointer to the iron instance
    DSPL* pD;                                   // Pointer to the DSPLay instance
    BUZZER* pBz;                                // Pointer to the simple Buzzer instance
    ENCODER* pEnc;                              // Pointer to the rotary encoder instance
    bool ready;                                 // Weither the iron is ready
	  const uint16_t period = 1000;               // The period to update the screen (ms)
};

void workSCREEN::init(void) {
  uint16_t temp_set = pIron->getTemp();
  bool is_celsius = pIron->getTempUnits();
  uint16_t tempH = pIron->temp2humanUnits(temp_set);
  if (is_celsius)
    pEnc->reset(tempH, temp_minC, temp_maxC, 1, 5);
  else
    pEnc->reset(tempH, temp_minF, temp_maxF, 1, 5);
  pIron->switchPower(true);
  ready = false;
  pD->clear();
  pD->tSet(tempH, is_celsius);
  pD->msgOn();
  forceRedraw();
}

void workSCREEN::rotaryValue(int16_t value) {
  ready = false;
  pD->msgOn();
  update_screen = millis() + period;
  pIron->setTempHumanUnits(value);
  pD->tSet(value, pIron->getTempUnits());
}

void workSCREEN::show(void) {
  if ((!force_redraw) && (millis() < update_screen)) return;

  force_redraw = false;
  update_screen = millis() + period;

  int temp = pIron->tempAverage();
  int temp_set = pIron->getTemp();
  int tempH = pIron->temp2humanUnits(temp);
  pD->tCurr(tempH);
  byte p = pIron->appliedPower();
  pD->percent(p);
  if ((abs(temp_set - temp) < 4) && (pIron->tempDispersion() < 15))  {
    pD->noAnimation();
    if (!ready) {
      pBz->shortBeep();
      pD->msgReady();
      ready = true;
    }
    return;
  }
  if (!ready && temp < temp_set) {
    pD->heating();
  }
}

//---------------------------------------- class errorSCREEN [the soldering iron error detected] ---------------
class errorSCREEN : public SCREEN {
  public:
    errorSCREEN(DSPL* DSP) {
      pD = DSP;
    }
    virtual void init(void) { pD->clear(); pD->msgFail(); }
  private:
    DSPL* pD;                                   // Pointer to the display instance
};

//---------------------------------------- class powerSCREEN [fixed power to the iron] -------------------------
class powerSCREEN : public SCREEN {
  public:
    powerSCREEN(IRON* Iron, DSPL* DSP, ENCODER* Enc) {
      pIron = Iron;
      pD = DSP;
      pEnc = Enc;
      on = false;
    }
    virtual void init(void);
    virtual void show(void);
    virtual void rotaryValue(int16_t value);
    virtual SCREEN* menu(void);
    virtual SCREEN* menu_long(void);
  private:
    IRON* pIron;                                // Pointer to the iron instance
    DSPL* pD;                                   // Pointer to the DSPLay instance
    ENCODER* pEnc;                              // Pointer to the rotary encoder instance
    uint32_t update_screen;                     // Time in ms to update the screen
    bool on;                                    // Weither the power of soldering iron is on
};

void powerSCREEN::init(void) {
  byte p = pIron->getAvgPower();
  byte max_power = pIron->getMaxFixedPower();
  pEnc->reset(p, 0, max_power, 1);
  on = true;                                    // Do start heating immediately
  pIron->switchPower(false);
  pIron->fixPower(p);
  pD->clear();
  pD->pSet(p);
  pD->noAnimation();
}

void powerSCREEN::show(void) {
  if ((!force_redraw) && (millis() < update_screen)) return;

  force_redraw = false;

  uint16_t temp = pIron->tempAverage();
  temp = pIron->temp2humanUnits(temp);
  pD->tCurr(temp);
  update_screen = millis() + 500;
}

void powerSCREEN::rotaryValue(int16_t value) {
  pD->pSet(value);
  if (on)
    pIron->fixPower(value);
  update_screen = millis() + 1000;
}

SCREEN* powerSCREEN::menu(void) {
  on = !on;
  if (on) {
    uint16_t pos = pEnc->read();
    on = pIron->fixPower(pos);
	  pD->clear();
    pD->pSet(pos);
	  update_screen = 0;
  } else {
    pIron->fixPower(0);
	  pD->clear();
	  pD->pSet(0);
	  pD->msgOff();
  }
  return this;
}

SCREEN* powerSCREEN::menu_long(void) {
  pIron->fixPower(0);
  if (nextL) {
    pIron->switchPower(true);
    return nextL;
  }
  return this;
}

//---------------------------------------- class configSCREEN [configuration menu] -----------------------------
class configSCREEN : public SCREEN {
  public:
    configSCREEN(IRON* Iron, DSPL* DSP, ENCODER* Enc, CONFIG* Cfg) {
      pIron = Iron;
      pD = DSP;
      pEnc = Enc;
      pCfg = Cfg;
    }
    virtual void init(void);
    virtual void show(void);
    virtual void rotaryValue(int16_t value);
    virtual SCREEN* menu(void);
    virtual SCREEN* menu_long(void);
  private:
    IRON* pIron;                                // Pointer to the iron instance
    DSPL* pD;                                   // Pointer to the DSPLay instance
    ENCODER* pEnc;                              // Pointer to the rotary encoder instance
    CONFIG*  pCfg;                              // Pointer to the config instance
    uint32_t update_screen;                     // Time in ms to update the screen
    byte mode;                                  // Which parameter to change: 0 - brightness, 1 - C/F, 2 - tuneSCREEN, 3 cancel
    bool tune;                                  // Weither the parameter is modified
    bool changed;                               // Weither some configuration parameter has been changed
    bool cels;                                  // Current celsius/farenheit;
};

void configSCREEN::init(void) {
  mode = 1;
  pEnc->reset(mode, 1, 3, 1, 0, true);          // 0 - brightness, 1 - C/F, 2 - tuneSCREEN, 3 - cancel
  tune    = false;
  changed = false;
  cels    = pCfg->getTempUnits();
  pD->clear();
  pD->setupMode(0);
  this->setSCRtimeout(30);
}

void configSCREEN::show(void) {
  if ((!force_redraw) && (millis() < update_screen)) return;
  force_redraw = false;
  update_screen = millis() + 10000;
  if ((mode == 1) && tune) {
    if (cels)
      pD->msgCelsius();
    else
      pD->msgFarneheit();
    return;
  }
  if (mode == 3)
    pD->msgCancel();
  else
    pD->setupMode(mode, cels);
}

void configSCREEN::rotaryValue(int16_t value) {
  update_screen = millis() + 10000;
  if (tune) {                                   // tune the temperature units
    changed = true;
    cels = !value;
  } else {
    mode = value;
  }
  force_redraw = true;
}

SCREEN* configSCREEN::menu(void) {
  if (tune) {
    tune = false;
    pEnc->reset(mode, 1, 3, 1, 0, true);
  } else {
    switch (mode) {
      case 1:                                   // Celsius / Farenheit
        pEnc->reset(cels, 0, 1, 1, 0, true);
        break;
      case 2:                                   // Calibration
        if (next) return next;
        break;
      case 3:
        if (main) return main;
        break;
    }
    tune = true;
  }
  force_redraw = true;
  return this;
}

SCREEN* configSCREEN::menu_long(void) {
  if (nextL) {
    if (changed) {
      pCfg->saveConfig(INTENSITY, cels);
      pIron->setTempUnits(cels);
    }
    return nextL;
  }
  return this;
}

//---------------------------------------- class tuneSCREEN [tune the register and calibrating the iron] -------
class tuneSCREEN : public SCREEN {
  public:
    tuneSCREEN(IRON* Iron, DSPL* DSP, ENCODER* ENC, BUZZER* Buzz, CONFIG* Cfg) {
      update_screen = 0;
      pIron = Iron;
      pD = DSP;
      pEnc = ENC;
      pBz  = Buzz;
      pCfg = Cfg;
    }
    virtual void init(void);
    virtual SCREEN* menu(void);
    virtual SCREEN* menu_long(void);
    virtual void show(void);
    virtual void rotaryValue(int16_t value);
  private:
    IRON* pIron;                                // Pointer to the iron instance
    DSPL* pD;                                   // Pointer to the display instance
    ENCODER* pEnc;                              // Pointer to the rotary encoder instance
    BUZZER* pBz;                                // Pointer to the simple Buzzer instance
    CONFIG* pCfg;                               // Pointer to the configuration class
    byte mode;                                  // Which temperature to tune [0-3]: select, up temp, low temp, defaults
    bool arm_beep;                              // Weither beep is armed
    byte max_power;                             // Maximum possible power to be applied
    uint32_t update_screen;                     // Time in ms to switch information on the display
    uint16_t tul[2];                            // upper & lower temp
    byte pul[2];                                // upper and lower power
};

void tuneSCREEN::init(void) {
  max_power = pIron->getMaxFixedPower();
  mode = 0;                                     // select the element from the list
  pul[0] = 75; pul[1] = 20;
  pEnc->reset(0, 0, 3, 1, 1, true);             // 0 - up temp, 1 - low temp, 2 - defaults, 3 - cancel
  update_screen = millis();
  arm_beep = true;
  tul[0] = tul[1] = 0;
  pD->clear();
  pD->msgTune();
  pD->tempLim(0, 0);
  forceRedraw();
}

void tuneSCREEN::rotaryValue(int16_t value) {
  if (mode == 0) {                              // No limit is selected, list the menu
    switch (value) {
      case 2:
        pD->msgDefault();
        break;
      case 3:
        pD->msgCancel();
        break;
      default:
       pD->tempLim(value, tul[value]);
       break;
    }
  } else {
    pIron->fixPower(value);
    force_redraw = true;
  }
  update_screen = millis() + 1000;
}

void tuneSCREEN::show(void) {
  if ((!force_redraw) && (millis() < update_screen)) return;

  force_redraw = false;
  update_screen = millis() + 1000;
  if (mode != 0) {                              // Selected upper or lower temperature
    int16_t temp = pIron->tempAverage();
    pD->tCurr(temp);
    byte power = pEnc->read();                  // applied power
    power = map(power, 0, max_power, 0, 100);
    pD->percent(power);
    if (mode == 1)
      pD->msgUpper();
    else
      pD->msgLower();
  }
  if (arm_beep && (pIron->tempDispersion() < 15)) {
    pBz->shortBeep();
    arm_beep = false;
  }
}
  
SCREEN* tuneSCREEN::menu(void) {                // The rotary button pressed
  if (mode == 0) {                              // select upper or lower temperature limit
    int val = pEnc->read();
    if (val == 2) {                             // load defaults
      pCfg->setDefaults(true);                  // Write default config to the EEPROM           
      if (main) return main;                    // Return to the main screen
    }
    if (val == 3) {
      if (nextL) return nextL;                  // Return to the previous menu
    }
    mode = val + 1;
    pD->clear();
    pD->msgTune();
    switch (mode) {
      case 1:                                   // upper temp
        pD->msgUpper();
        break;
      case 2:                                   // lower temp
        pD->msgLower();
        break;
      default:
        break;
    }
    pEnc->reset(pul[mode-1], 0, max_power, 1, 5);
    pIron->fixPower(pul[mode-1]);               // Switch on the soldering iron
  } else {                                      // upper or lower temperature limit just setup     
    pul[mode-1] = pEnc->read();                 // The supplied power
    tul[mode-1] = pIron->tempAverage();
    pD->clear();
    pD->msgTune();
    pEnc->reset(mode-1, 0, 3, 1, 1, true);      // 0 - up temp, 1 - low temp, 2 - defaults, 3 -cancel
    mode = 0;
    pIron->fixPower(0);
  }
  arm_beep = true;
  force_redraw = true;
  return this;
}

SCREEN* tuneSCREEN::menu_long(void) {
  pIron->fixPower(0);                           // switch off the power
  bool all_data = true;
  for (byte i = 0; i < 2; ++i) {
    if (!tul[i]) all_data = false;
  }
  if (all_data) {                               // save calibration data. Config will be written to the EEPROM later on
    pCfg->saveCalibrationData(tul[0], tul[1]); 
  }
  if (nextL) return nextL;
  return this;
}
//=================================== End of class declarations ================================================

DSPL       disp(LCD_RS_PIN, LCD_E_PIN, LCD_DB4_PIN, LCD_DB5_PIN, LCD_DB6_PIN, LCD_DB7_PIN);
ENCODER    rotEncoder(R_MAIN_PIN, R_SECD_PIN);
BUTTON     rotButton(R_BUTN_PIN);
IRON       iron(heaterPIN, probePIN);
CONFIG     ironCfg;
BUZZER     simpleBuzzer(buzzerPIN);

mainSCREEN   offScr(&iron, &disp, &rotEncoder, &simpleBuzzer, &ironCfg);
workSCREEN   wrkScr(&iron, &disp, &rotEncoder, &simpleBuzzer);
errorSCREEN  errScr(&disp);
powerSCREEN  powerScr(&iron, &disp, &rotEncoder);
configSCREEN cfgScr(&iron, &disp, &rotEncoder, &ironCfg);
tuneSCREEN   tuneScr(&iron, &disp, &rotEncoder, &simpleBuzzer, &ironCfg);

SCREEN *pCurrentScreen = &offScr;

// the setup routine runs once when you press reset:
void setup() {
//Serial.begin(115200);
  disp.init();

  // Load configuration parameters
  ironCfg.init();
  bool is_cfg_valid = ironCfg.load();
  uint16_t temp_min, temp_max;
  ironCfg.getCalibrationData(temp_max, temp_min);

  iron.init(temp_max, temp_min);
  uint16_t temp = ironCfg.temp();
  iron.setTemp(temp);

  // Initialize rotary encoder
  rotEncoder.init();
  rotButton.init();
  delay(500);
  attachInterrupt(digitalPinToInterrupt(R_MAIN_PIN), rotEncChange,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(R_BUTN_PIN), rotPushChange,  CHANGE);

  // Initialize SCREEN hierarchy
  offScr.next    = &wrkScr;
  offScr.nextL   = &cfgScr;
  wrkScr.next    = &offScr;
  wrkScr.nextL   = &powerScr;
  errScr.next    = &offScr;
  errScr.nextL   = &offScr;
  powerScr.nextL = &wrkScr;
  cfgScr.next    = &tuneScr;
  cfgScr.nextL   = &offScr;
  cfgScr.main    = &offScr;
  tuneScr.nextL  = &cfgScr;
  tuneScr.main   = &offScr;
  pCurrentScreen->init();

}

void rotEncChange(void) {
  rotEncoder.cnangeINTR();
}

void rotPushChange(void) {
  rotButton.cnangeINTR();
}

// The main loop
void loop() {
  static int16_t old_pos = rotEncoder.read();
  iron.keepTemp();                                // First, read the temperature

  bool iron_on = iron.isOn();
  if ((pCurrentScreen == &wrkScr) && !iron_on) {  // the soldering iron failed
    pCurrentScreen = &errScr;
  pCurrentScreen->init();
  }

  SCREEN* nxt = pCurrentScreen->returnToMain();
  if (nxt != pCurrentScreen) {                  // return to the main screen by timeout
    pCurrentScreen = nxt;
    pCurrentScreen->init();
  }

  byte bStatus = rotButton.intButtonStatus();
  switch (bStatus) {
    case 2:                                     // long press;
      nxt = pCurrentScreen->menu_long();
      if (nxt != pCurrentScreen) {
        pCurrentScreen = nxt;
        pCurrentScreen->init();
      } else {
        if (pCurrentScreen->isSetup())
         pCurrentScreen->resetTimeout();
      }
      break;
    case 1:                                     // short press
      nxt = pCurrentScreen->menu();
      if (nxt != pCurrentScreen) {
        pCurrentScreen = nxt;
        pCurrentScreen->init();
      } else {
        if (pCurrentScreen->isSetup())
         pCurrentScreen->resetTimeout();
      }
      break;
    case 0:                                     // Not pressed
    default:
      break;
  }

  int16_t pos = rotEncoder.read();
  if (old_pos != pos) {
    pCurrentScreen->rotaryValue(pos);
    old_pos = pos;
    if (pCurrentScreen->isSetup())
     pCurrentScreen->resetTimeout();
  }

  pCurrentScreen->show();
   
  disp.show();
  delay(10);
}

