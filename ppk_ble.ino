/*
 * ppk_usb
 *
 * Copyright (C) 2014 cy384
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license. See the LICENSE file for details.
 * 
 * MODIFIED by Christian Lysholm 2021
 * Modified for BLE support using a tinyPICO ESP32 module. 
 * Added battery monitoring and reporting via RGB LED
 * Moved keyboard booting until peripheral is detected
 * 
 */

// Arduino BLE HID adapter for the Palm Portable Keyboard

// If this include causes an error Arduino, just comment it out
//#include <Keyboard.h>
#include <SoftwareSerial.h>
#include <TinyPICO.h>
//#include <BleConnectionStatus.h>
#include <BleKeyboard.h>
//#include <KeyboardOutputCallbacks.h>

// set to 3 for III hardware, or 5 for V hardware
#define PPK_VERSION 500

// set to 1 to enable debug mode, which notes to the arduino console at 9600
#define PPK_DEBUG 0

#if PPK_VERSION == 3
#define VCC_PIN       2
#define RX_PIN        8
#define RTS_PIN       4
#define DCD_PIN       5
#define GND_PIN       6
#endif

#if PPK_VERSION == 5
#define VCC_PIN       7
#define RX_PIN        8
#define RTS_PIN       5
#define DCD_PIN       4
#define GND_PIN       2
#endif

#if PPK_VERSION == 500
#define VCC_PIN       4
#define RX_PIN        27 //External 10k pulldown resistor added
#define RTS_PIN       26
#define DCD_PIN       25
#define GND_PIN       18 //UNUSED
#define DET_PIN       22 //Peripheral detect, held low by KB
#endif

#define PULLDOWN_PIN  23
// set this to any unused pin
#define TX_PIN        19

#if (PPK_VERSION != 3) && (PPK_VERSION != 5) && (PPK_VERSION != 500)
#error
#error
#error    you did not set your ppk version!
#error    read the instructions or read the code!
#error
#error
#endif

// convenience masks
#define UPDOWN_MASK 0b10000000
#define X_MASK      0b00000111
#define Y_MASK      0b01111000
#define MAP_MASK    0b01111111

// wait this many milliseconds before making sure keyboard is still awake
#define TIMEOUT 500000
#define BAT_CHECK_TIME 120000 //2 minutes
#define HEARTBEAT_TIME 5000 //5 seconds
#define BOOT_TIMEOUT 2000 //2 seconds

// macro for testing if a char is printable ASCII
#define PRINTABLE_CHAR(x) ((x >= 32) && (x <= 126))

#define notConnectedColor 0x0000FF
#define bootingColor 0xFFCC00

uint32_t heartbeat_color; 
SoftwareSerial keyboard_serial(RX_PIN, TX_PIN, true); // RX, TX, inverted
TinyPICO tp = TinyPICO();
int check_battery(void);
int batLevel = check_battery();
BleKeyboard ble_kb("PICO_PPK", "LysTech", batLevel); 

char key_map[128] = { 0 };
char fn_key_map[128] = { 0 };

char last_byte = 0;
char key_byte = 0;

int fn_key_down = 0;
int boot_state = 0; //keyboard boot state (0/1)
int isPlugged = 0;

unsigned long last_comm = 0;
unsigned long last_bat = 0;
unsigned long last_heartbeat = 0;
unsigned long heartbeat_length = 750; //milliseconds
unsigned long boot_start = 0; //milliseconds

void config_keymap()
{
  // y0 row
  key_map[0b00000000] = '1';
  key_map[0b00000001] = '2';
  key_map[0b00000010] = '3';
  key_map[0b00000011] = 'z';
  key_map[0b00000100] = '4';
  key_map[0b00000101] = '5';
  key_map[0b00000110] = '6';
  key_map[0b00000111] = '7';

  // y1 row
  key_map[0b00001000] = KEY_LEFT_GUI; // "CMMD" or "Cmd"
  key_map[0b00001001] = 'q';
  key_map[0b00001010] = 'w';
  key_map[0b00001011] = 'e';
  key_map[0b00001100] = 'r';
  key_map[0b00001101] = 't';
  key_map[0b00001110] = 'y';
  key_map[0b00001111] = '`';

  // y2 row
  key_map[0b00010000] = 'x';
  key_map[0b00010001] = 'a';
  key_map[0b00010010] = 's';
  key_map[0b00010011] = 'd';
  key_map[0b00010100] = 'f';
  key_map[0b00010101] = 'g';
  key_map[0b00010110] = 'h';
  key_map[0b00010111] = ' '; // "Space 1"

  // y3 row
  key_map[0b00011000] = KEY_CAPS_LOCK;
  key_map[0b00011001] = KEY_TAB;
  key_map[0b00011010] = KEY_LEFT_CTRL;
  key_map[0b00011011] = 0;
  key_map[0b00011100] = 0;
  key_map[0b00011101] = 0;
  key_map[0b00011110] = 0;
  key_map[0b00011111] = 0;

  // y4 row
  key_map[0b00100000] = 0;
  key_map[0b00100001] = 0;
  key_map[0b00100010] = 0; // Fn key
  key_map[0b00100011] = KEY_LEFT_ALT;
  key_map[0b00100100] = 0;
  key_map[0b00100101] = 0;
  key_map[0b00100110] = 0;
  key_map[0b00100111] = 0;

  // y5 row
  key_map[0b00101000] = 0;
  key_map[0b00101001] = 0;
  key_map[0b00101010] = 0;
  key_map[0b00101011] = 0;
  key_map[0b00101100] = 'c';
  key_map[0b00101101] = 'v';
  key_map[0b00101110] = 'b';
  key_map[0b00101111] = 'n';

  // y6 row
  key_map[0b00110000] = '-';
  key_map[0b00110001] = '=';
  key_map[0b00110010] = KEY_BACKSPACE;
  key_map[0b00110011] = 0; // "Special Function One"
  key_map[0b00110100] = '8';
  key_map[0b00110101] = '9';
  key_map[0b00110110] = '0';
  key_map[0b00110111] = ' '; // "Space 2"

  // y7 row
  key_map[0b00111000] = '[';
  key_map[0b00111001] = ']';
  key_map[0b00111010] = '\\';
  key_map[0b00111011] = 0; // "Special Function Two"
  key_map[0b00111100] = 'u';
  key_map[0b00111101] = 'i';
  key_map[0b00111110] = 'o';
  key_map[0b00111111] = 'p';

  // y8 row
  key_map[0b01000000] = '\'';
  key_map[0b01000001] = KEY_RETURN;
  key_map[0b01000010] = 0; // "Special Function Three"
  key_map[0b01000011] = 0;
  key_map[0b01000100] = 'j';
  key_map[0b01000101] = 'k';
  key_map[0b01000110] = 'l';
  key_map[0b01000111] = ';';

  // y9 row
  key_map[0b01001000] = '/';
  key_map[0b01001001] = KEY_UP_ARROW;
  key_map[0b01001010] = 0; // "Special Function Four"
  key_map[0b01001011] = 0;
  key_map[0b01001100] = 'm';
  key_map[0b01001101] = ',';
  key_map[0b01001110] = '.';
  key_map[0b01001111] = 0; // "DONE" or "Done"

  // y10 row
  key_map[0b01010000] = KEY_DELETE;
  key_map[0b01010001] = KEY_LEFT_ARROW;
  key_map[0b01010010] = KEY_DOWN_ARROW;
  key_map[0b01010011] = KEY_RIGHT_ARROW;
  key_map[0b01010100] = 0;
  key_map[0b01010101] = 0;
  key_map[0b01010110] = 0;
  key_map[0b01010111] = 0;

  // y11 row
  key_map[0b01011000] = KEY_LEFT_SHIFT;
  key_map[0b01011001] = KEY_RIGHT_SHIFT;
  key_map[0b01011010] = 0;
  key_map[0b01011011] = 0;
  key_map[0b01011100] = 0;
  key_map[0b01011101] = 0;
  key_map[0b01011110] = 0;
  key_map[0b01011111] = 0;
}

void config_fnkeymap()
{
  fn_key_map[0b01010001] = KEY_HOME; // left arrow
  fn_key_map[0b01010010] = KEY_PAGE_DOWN; // down arrow
  fn_key_map[0b01010011] = KEY_END; // right arrow
  fn_key_map[0b01001001] = KEY_PAGE_UP; // up arrow
  fn_key_map[0b00011001] = KEY_ESC; // tab key
  fn_key_map[0b00000000] = KEY_F1; // 1
  fn_key_map[0b00000001] = KEY_F2; // 2
  fn_key_map[0b00000010] = KEY_F3; // 3
  fn_key_map[0b00000100] = KEY_F4; // 4
  fn_key_map[0b00000101] = KEY_F5; // 5
  fn_key_map[0b00000110] = KEY_F6; // 6
  fn_key_map[0b00000111] = KEY_F7; // 7
  fn_key_map[0b00110100] = KEY_F8; // 8
  fn_key_map[0b00110101] = KEY_F9; // 9
  fn_key_map[0b00110110] = KEY_F10; // 0
  fn_key_map[0b00110000] = KEY_F11; // -
  fn_key_map[0b00110001] = KEY_F12; // =
}

void print_byte_bin(char bin_byte)
{
  Serial.print("0b");

  for (int i = 7; i > -1; i--) Serial.print(int((bin_byte & (1 << i)) >> i));
}

void print_keychange(char key_byte, char key_code, int key_up)
{
  if (key_up) Serial.print("released: "); else Serial.print("pressed:  ");
  print_byte_bin(key_byte);

  Serial.print(" mapped to ");

  if (key_code)
  {
    print_byte_bin(key_code);

    if(PRINTABLE_CHAR(key_code))
    {
      Serial.print(" (");
      Serial.print(key_code);
      Serial.print(")");
    }
    else
    {
      Serial.print(" (unprintable)");
    }
  }
  // Fn has no keycode, special case it
  else if (key_byte == 34)
  {
    Serial.print("Fn");
  }
  else
  {
    Serial.print("nothing");
  }

  Serial.println("");
}

void boot_keyboard()
{
  if (PPK_DEBUG)
  {
    // delay for a bit to allow for opening serial monitor etc.
    for (int i = 0; i < 15; delay(1000 + i++)) Serial.print(".");

    Serial.println("beginning keyboard boot sequence");
  }
  tp.DotStar_SetPower(true);
  tp.DotStar_SetPixelColor(bootingColor);

  digitalWrite(VCC_PIN, HIGH);

  keyboard_serial.begin(9600); 
  keyboard_serial.listen();

  if (PPK_DEBUG) Serial.print("waiting for keyboard response...");

  while(digitalRead(DCD_PIN) != HIGH) {;};

  if (PPK_DEBUG) Serial.println(" done");


  if (PPK_DEBUG) Serial.print("finishing handshake...");

  if (digitalRead(RTS_PIN) == LOW)
  {
    delay(10);
    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, HIGH);
  }
  else
  {
    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, HIGH);
    digitalWrite(RTS_PIN, LOW);
    delay(10); 
    digitalWrite(RTS_PIN, HIGH);
  }

  delay(5);
  
  if (PPK_DEBUG) Serial.println(" done");

  if (PPK_DEBUG) Serial.print("waiting for keyboard serial ID...");

  boot_start = millis(); 
  while (keyboard_serial.available() < 2) 
  {
      if ((millis() - boot_start) > BOOT_TIMEOUT)
      {
        tp.DotStar_SetPower(true);
        tp.DotStar_SetPixelColor(notConnectedColor);
        return; 
      }
  }

  if (PPK_DEBUG) Serial.println(" done");

  int byte1 = keyboard_serial.read();
  int byte2 = keyboard_serial.read();

  if (!((byte1 == 0xFA) && (byte2 == 0xFD)))
  {
    if (PPK_DEBUG) Serial.println("got wrong bytes? giving up here");
    tp.DotStar_SetPower(true);
    tp.DotStar_SetPixelColor(notConnectedColor);
    return;
    //while (1) {;};
  }

  boot_state = 1; 
  last_comm = millis();
  ble_kb.begin();
  tp.DotStar_SetPower(false);
  batLevel = check_battery();
  ble_kb.setBatteryLevel(batLevel);
  setHeartbeatColor(batLevel);
  //tp.DotStar_SetPixelColor(0x00FFFF);
}

int check_battery()
{
  //batVolt has voltage levels every 5% of charge level. Adjust as necessary. Slightly better than a straight linear interp of voltage
  float batVolt_lookup[] = {3.27, 3.61, 3.69, 3.71, 3.73, 3.75, 3.77, 3.79, 3.80, 3.82, 3.84, 3.85, 3.87, 3.91, 3.95, 3.98, 4.02, 4.08, 4.11, 4.15, 4.20};
  float batVolt = 0.0; //volts
  int batLevel = -1; //0 to 100 (percent as int)
  
  batVolt = tp.GetBatteryVoltage();
  for (int n=0; n<=21; n++) 
  {
    if (batVolt < batVolt_lookup[n])
    {
      batLevel = 5*n; 
      break;
    }
  }
  if (batLevel < 0)
  {
    //You shouldn't be here, batVolt was not found in range of lookup values.
    batLevel = 100; 
  }
  return batLevel; 
}

int check_connection()
{
  int isNotConnected;
  if (not(boot_state))
  {
    digitalWrite(VCC_PIN, HIGH);
  }
  isNotConnected = digitalRead(DET_PIN);
  if (not(boot_state))
  {
    digitalWrite(VCC_PIN, LOW); 
  }
  
  if(isNotConnected)
  {
    boot_state = 0;
    ble_kb.end();
    keyboard_serial.end(); 
    delay(500);
    //tp.DotStar_SetPower(true);
    //tp.DotStar_SetPixelColor(notConnectedColor);
    digitalWrite(VCC_PIN, LOW); 

    if (PPK_DEBUG) Serial.println("Keyboard is not connected.");

  }
  return not(isNotConnected);
}

void setHeartbeatColor(int batLevel)
{
  if (batLevel > 35) //35-100%
    {
     //set green heartbeat
     heartbeat_color = 0x00FF00;
    }
    else if (batLevel >= 15)//15-35%
    {
      //set yellow/orange heartbeat
      heartbeat_color = 0xFF8000;
    }
    else // <15%
    {
      //set red heartbeat
      heartbeat_color = 0xFF0000;
    }
}

void setup()  
{
  if (PPK_DEBUG)
  {
    Serial.begin(9600);
    Serial.print("compiled in debug mode with PPK_VERSION ");
    Serial.println(PPK_VERSION);
  }
  pinMode(VCC_PIN, OUTPUT);
  pinMode(GND_PIN, OUTPUT);
  pinMode(PULLDOWN_PIN, OUTPUT);

  pinMode(RX_PIN, INPUT); //Add external Pulldown
  pinMode(DCD_PIN, INPUT);
  pinMode(RTS_PIN, INPUT);

  digitalWrite(VCC_PIN, LOW);
  digitalWrite(GND_PIN, LOW);
  digitalWrite(PULLDOWN_PIN, LOW);
  
  config_keymap();
  config_fnkeymap();
  pinMode(DET_PIN, INPUT_PULLUP);
  boot_keyboard();

  tp.DotStar_SetPower(true);
  //tp.DotStar_SetBrightness(64); //25% brightness
  last_bat = BAT_CHECK_TIME;

  if (PPK_DEBUG) Serial.println("setup completed");
}

void loop()
{
  if(boot_state)
  {
    if (keyboard_serial.available())
    {
      for (int i = keyboard_serial.available(); i > 0; i--)
      {
        key_byte = keyboard_serial.read();
  
        int key_up = key_byte & UPDOWN_MASK;
        int key_x = 0 + (key_byte & X_MASK);
        int key_y = 0 + ((key_byte & Y_MASK) >> 3);
  
        char key_code = 0;
  
        if (fn_key_down && fn_key_map[key_byte & MAP_MASK])
        {
          key_code += fn_key_map[key_byte & MAP_MASK];
        }
        else
        {
          key_code += key_map[key_byte & MAP_MASK];
        }
  
        // keyboard duplicates the final key-up byte
        if (key_byte == last_byte)
        {
          ble_kb.releaseAll();
        }
        else
        {
          if (PPK_DEBUG) print_keychange(key_byte & MAP_MASK, key_code, key_up);
  
          if (key_code != 0)
          {
            if (key_up)
            {
              ble_kb.release(key_code);
            }
            else
            {
              ble_kb.press(key_code);
            }
          }
          else
          {
            // special case the Fn key
            if ((key_byte & MAP_MASK) == 34)
            {
              fn_key_down = !key_up;
            }
          }
        }
  
        last_byte = key_byte;
        last_comm = millis();
      }
    }
    else
    {
      // reboot if no recent comms, otherwise keyboard falls asleep
      if ((millis() - last_comm) > TIMEOUT)
      {
        if (PPK_DEBUG) Serial.println("rebooting keyboard for timeout");
  
        digitalWrite(VCC_PIN, LOW);
        boot_keyboard();
      }
    }
    //hotplug_start = millis(); 
  }
  /*else
  {
    if (isPlugged)
    {
      boot_keyboard(); 
    }
  }*/
  if ((millis() - last_bat)  > BAT_CHECK_TIME)
  {
    batLevel = check_battery();
    ble_kb.setBatteryLevel(batLevel);
    setHeartbeatColor(batLevel);  
    //tp.DotStar_SetPower(false);
    last_bat = millis();
  }
  if (not(boot_state) && ((millis() - last_heartbeat) > (HEARTBEAT_TIME + heartbeat_length/2)))
  {
    tp.DotStar_SetPixelColor(notConnectedColor);
  }
  
  if ((millis() - last_heartbeat) > (HEARTBEAT_TIME + heartbeat_length))
  {
    isPlugged = check_connection();
    tp.DotStar_Clear();
    tp.DotStar_SetPower(false);
    last_heartbeat = millis();
    //hotplug_start = millis(); 
    if ( isPlugged && not(boot_state))
    {
      boot_keyboard();
    }
//    if (not(isPlugged))
//    {
//      tp.DotStar_SetPower(true);
//      tp.DotStar_SetPixelColor(notConnectedColor); 
//    }
  }
  else if ((millis() - last_heartbeat) > HEARTBEAT_TIME)
  {
    tp.DotStar_SetPower(true);
    tp.DotStar_SetPixelColor(heartbeat_color);
  }
  
}
