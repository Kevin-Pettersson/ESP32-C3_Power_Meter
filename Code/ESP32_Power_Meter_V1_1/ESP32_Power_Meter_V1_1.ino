/*
 * ESP32 3-Channel Power Logger - Modified Version
 * Author: Kevin Pettersson (Custom Circuit Co.)
 * Date: 8/7-2023
 * 
 * Description:
 * This code is a modified version of the ESP32 3-Channel Power Logger project by Ovidiu. 
 * The changes that have been made are the following:
 * 
 * Fixed interupts as they were not working properly
 * Implemented averaging as that was not actually used...
 * Added efficiency mode
 * Simplifed and removed redundant code
 * Fixed error with padding for voltage
 * Modified splash screen to CCC logo
 * Fixed SD card naming
 *
 * Tindie (modified PCB): https://www.tindie.com/products/ccc/esp32-c3-power-meterlogger/
 * Original project: https://hackaday.io/project/187504-esp32-3-channel-power-logger
 */

#include <Wire.h>
#include <INA3221.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP32Time.h>
#include <Adafruit_GFX.h>     // Core graphics library
#include <Adafruit_ST7735.h>  // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h>  // Hardware-specific library for ST7789
#include "bitmap.h"           // Startup logo


/* ------------------------------ Wifi details ------------------------------ */

#ifndef STASSID
#define STASSID "wifi_name_here"
#define STAPSK "wifi_password_here"
#endif

// ----- Define Pins ----- //
#define I2C_SDA 6
#define I2C_SCL 7
#define SCK 3
#define MISO 4
#define MOSI 5
#define SDCARD_CS 10
#define TFT_CS 2
#define TFT_RST 20
#define TFT_DC 21
#define TFT_BL 1
#define LEFT_BUTTON_PIN 8
#define RIGHT_BUTTON_PIN 9
// ----- Define Pins ----- //

// ----- Define Some Colors ----- //
#define ST7735_BLACK 0x0000
#define ST7735_RED 0x001F
#define ST7735_GREEN 0x07E0
#define ST7735_WHITE 0xFFFF
#define ST7735_BLUE 0xF800
#define ST7735_DEEP_BLUE 0x3800
#define ST7735_YELLOW 0x07FF
#define ST7735_CYAN 0xFFE0
#define ST7735_MAGENTA 0xF81F
// ----- Define Some Colors ----- //

// ----- Initialize TFT ----- //
#define ST7735_TFTWIDTH 128
#define ST7735_TFTHEIGHT 160
#define background_color ST7735_DEEP_BLUE
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
// ----- Initialize TFT ----- //

// ----- Define INA3221 Address ----- //
INA3221 INA3221(INA3221_ADDR40_GND);  //Solder J1 Pad
#define CHANNEL1 INA3221_CH1
#define CHANNEL2 INA3221_CH2
#define CHANNEL3 INA3221_CH3
// ----- Define INA3221 Address ----- //


ESP32Time rtc;
void ICACHE_RAM_ATTR handle_left_Interrupt();
void ICACHE_RAM_ATTR handle_right_Interrupt();


// ----- Define Variables ----- //
const char* ssid = STASSID;
const char* password = STAPSK;

int backlight_pwm = 255;  //Start with the display brightness at 100%
int left_button_flag = 0;
int right_button_flag = 0;
int selected = 0;
int selected_avg = 3;
int channel_number = 1;  //The default channel to display at startup

bool started = false;
bool display_state = true;  //Display is awake when true and sleeping when false
bool ignore_input = false;  //Used in order to ingnore the buttons
bool ntpcConnected = false;

unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
unsigned long interval = 200;  //Update data on screen and on SD Card every 200ms
unsigned long display_on_time = 0;
unsigned long start_delay = 0;

float shunt_voltage_1 = 0;
float bus_voltage_1 = 0;
float current_mA_1 = 0;
float load_voltage_1 = 0;
float energy_1 = 0;
float capacity_1 = 0;

float shunt_voltage_2 = 0;
float bus_voltage_2 = 0;
float current_mA_2 = 0;
float load_voltage_2 = 0;
float energy_2 = 0;
float capacity_2 = 0;

float shunt_voltage_3 = 0;
float bus_voltage_3 = 0;
float current_mA_3 = 0;
float load_voltage_3 = 0;
float energy_3 = 0;
float capacity_3 = 0;

float efficiency = 0;

float battery_voltage = 0;

bool mode = false;
bool use_sd_card = true;
bool use_channel_1 = true;
bool use_channel_2 = true;
bool use_channel_3 = true;
bool setup_error = false;
bool file_active = false;

String file_name = "/log.txt";  //Default file name in case the is an error with the NTP server


// ----- Time variables ----- //
const long utcOffsetInSeconds = 7200;
unsigned long rtcOffset = 0;
unsigned long seconds = 0;
unsigned long minutes = 0;
unsigned long hours = 0;
unsigned long days = 0;
char daysOfTheWeek[7][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
// ----- Time variables ----- //


void setup() {
  Serial.begin(115200);
  Serial.println("");

  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, backlight_pwm);
  pinMode(LEFT_BUTTON_PIN, INPUT);
  pinMode(RIGHT_BUTTON_PIN, INPUT);

  Wire.begin(I2C_SDA, I2C_SCL);
  SPI.begin(SCK, MISO, MOSI, SDCARD_CS);

  INA3221.begin();
  INA3221.reset();
  INA3221.setShuntRes(10, 10, 10);   //You must specify the shunt resistor values for calibration (in mOhm)
  INA3221.setFilterRes(10, 10, 10);  //You must specify the filter resistor values for calibration (in Ohm)
  INA3221.setAveragingMode(INA3221_REG_CONF_AVG_256);
  INA3221.setModeContinious();

  // ----- Initiate the TFT display ----- //
  //tft.initR();
  tft.initR(25, 0, 0); //if not standard LCD
  tft.setCursor(0, 0);
  tft.setRotation(1);
  tft.fillScreen(background_color);

  for (int y = 0; y < 128; y++) {
    for (int x = 0; x < 160; x++) {
      tft.drawPixel(x, y, shiba[(y * 160) + x]);
    }
  }

  // ----- Initiate the TFT display ----- //

  delay(2000);

  // ----- Run the startup checks ----- //
  tft.fillScreen(background_color);
  boot_sequesnce();
  if(use_sd_card){
    create_file();
  }

  // ----- Run the startup checks ----- //

  delay(2000);

  // ----- Enable Buttons ----- //
  attachInterrupt(digitalPinToInterrupt(LEFT_BUTTON_PIN), handle_left_Interrupt, RISING);  //prev FALLING
  attachInterrupt(digitalPinToInterrupt(RIGHT_BUTTON_PIN), handle_right_Interrupt, RISING);
  // ----- Enable Buttons ----- //

  // ----- Run the setup menu ----- //
  setup_menu();
  // ----- Run the setup menu ----- //

  tft.fillScreen(background_color);

  display_on_time = millis();
  start_delay = millis();
}

void loop() {
  currentMillis = millis();

  if (mode) {  //Efficency mode, CH1 OUTPUT and CH3 INPUT
    use_channel_1 = true;
    use_channel_2 = false;
    use_channel_3 = true;
    if (currentMillis - previousMillis >= interval) {
      if (started) {
        measure_values();
        previousMillis = currentMillis;
        displaydataEff();
        if (use_sd_card == true) {
          write_file();
        }
      }
    }
  } else {  //normal measurement mode
    // ----- Handle measuring and writing data ----- //
    if (currentMillis - previousMillis >= interval) {
      if (started) {
        measure_values();
        previousMillis = currentMillis;
        displaydata();
        if (use_sd_card == true) {
          write_file();
        }
      }
    }
    // ----- Handle measuring and writing data ----- //

    // ----- Handle left button being pressed ----- //
    if (left_button_flag == 1 && display_state == true) {
      if (started == true) {
        channel_number = channel_number + 1;
        if (channel_number == 1 && use_channel_1 == false) {
          channel_number = channel_number + 1;
        }
        if (channel_number == 2 && use_channel_2 == false) {
          channel_number = channel_number + 1;
        }
        if (channel_number == 3 && use_channel_3 == false) {
          channel_number = channel_number + 1;
        }
        if (channel_number > 3) {
          if (use_channel_1 == false) {
            if (use_channel_2 == false) {
              channel_number = 3;
            } else {
              channel_number = 2;
            }
          } else {
            channel_number = 1;
          }
        }
      }

      left_button_flag = 0;
    }
    // ----- Handle left button being pressed ----- //
  }

  // ----- Handle right button being pressed ----- //
  if (right_button_flag == 1 && display_state == true) {
    use_channel_1 = true;
    use_channel_2 = true;
    use_channel_3 = true;
    started = false;
    selected = 0;
    right_button_flag = 0;
    shunt_voltage_1 = 0;
    bus_voltage_1 = 0;
    current_mA_1 = 0;
    load_voltage_1 = 0;
    energy_1 = 0;
    capacity_1 = 0;
    shunt_voltage_2 = 0;
    bus_voltage_2 = 0;
    current_mA_2 = 0;
    load_voltage_2 = 0;
    energy_2 = 0;
    capacity_2 = 0;
    shunt_voltage_3 = 0;
    bus_voltage_3 = 0;
    current_mA_3 = 0;
    load_voltage_3 = 0;
    energy_3 = 0;
    capacity_3 = 0;
    file_active = false;
    setup_menu();
  }
  // ----- Handle right button being pressed ----- //
}


void displaydataEff() {
  // ----- Display the data ----- //
  tft.setTextSize(2);
  tft.setTextColor(ST7735_YELLOW, background_color);
  tft.setCursor(0, 0);

  convert_time();
  tft.print("T: ");
  tft.print(days);
  tft.print(":");
  if (hours < 10) { tft.print("0"); }
  tft.print(hours);
  tft.print(":");
  if (minutes < 10) { tft.print("0"); }
  tft.print(minutes);
  tft.print(":");
  if (seconds < 10) { tft.print("0"); }
  tft.println(seconds);

  tft.println("          ");



  // voltage
  String voltage_padding_in = padding(load_voltage_3);
  String voltage_padding_out = padding(load_voltage_1);

  tft.setTextColor(ST7735_BLUE, background_color);
  tft.print("Vin:  " + voltage_padding_in);
  tft.setTextColor(ST7735_WHITE, background_color);
  tft.println(load_voltage_3, 2);

  tft.setTextColor(ST7735_RED, background_color);
  tft.print("Vout: " + voltage_padding_out);
  tft.setTextColor(ST7735_WHITE, background_color);
  tft.println(load_voltage_1, 2);


  // power
  float power_in = load_voltage_3 * current_mA_3;
  float power_out = load_voltage_1 * current_mA_1;

  String power_padding_in = padding(power_in);
  String power_padding_out = padding(power_out);

  tft.setTextColor(ST7735_BLUE, background_color);
  tft.print("mW:   " + power_padding_in);
  tft.setTextColor(ST7735_WHITE, background_color);
  tft.println(power_in, 2);

  tft.setTextColor(ST7735_RED, background_color);
  tft.print("mW:   " + power_padding_out);
  tft.setTextColor(ST7735_WHITE, background_color);
  tft.println(power_out, 2);

  //efficiency
  if (power_in != 0) {
    efficiency = (power_out / power_in) * 100;
  } else {
    efficiency = 0;
  }
  String efficiency_padding = padding(efficiency);

  tft.print("EFF %:" + efficiency_padding);
  tft.println(efficiency, 2);

  //battery
  battery_voltage = get_battery_voltage();
  if(battery_voltage == 0){
    tft.print("        P:USB");
  }else if(battery_voltage > 3.6){
    tft.print("       B:");
    tft.print(get_battery_voltage());
  }else{
    tft.setTextColor(ST7735_RED, background_color);
    tft.print("       B:");
    tft.print(get_battery_voltage());
  }

  tft.setTextColor(ST7735_WHITE, background_color);
}

String padding(float value) {
  String padding;

  if (value >= 0 && value < 10) {
    padding = "   ";
  } else if (value >= 10 && value < 100) {
    padding = "  ";
  } else if (value >= 100 && value < 1000) {
    padding = " ";
  } else if (value >= 1000 && value < 10000) {
    padding = "";
  }
  return padding;
}

void displaydata() {
  float current_mA = 0;
  float load_voltage = 0;
  float capacity = 0;
  float energy = 0;
  float bus_voltage = 0;


  // ----- Display data of the selected channel ----- //
  if (channel_number == 1) {
    bus_voltage = bus_voltage_1;
    current_mA = current_mA_1;
    load_voltage = load_voltage_1;
    energy = energy_1;
    capacity = capacity_1;
  } else if (channel_number == 2) {
    bus_voltage = bus_voltage_2;
    current_mA = current_mA_2;
    load_voltage = load_voltage_2;
    energy = energy_2;
    capacity = capacity_2;
  } else if (channel_number == 3) {
    bus_voltage = bus_voltage_3;
    current_mA = current_mA_3;
    load_voltage = load_voltage_3;
    energy = energy_3;
    capacity = capacity_3;
  }
  // ----- Display data of the selected channel ----- //

  // ----- Some padding so that text is properly aligned ----- //
  String voltage_padding = padding(bus_voltage);
  String current_padding = padding(current_mA);
  String power_padding = padding(load_voltage * current_mA);
  String energy_padding = padding(energy);
  String capacity_padding = padding(energy);
  // ----- Some padding so that text is properly aligned ----- //

  // ----- Display the data ----- //
  tft.setTextSize(2);
  tft.setTextColor(ST7735_YELLOW, background_color);
  tft.setCursor(0, 0);

  convert_time();
  tft.print("T: ");
  tft.print(days);
  tft.print(":");
  if (hours < 10) { tft.print("0"); }
  tft.print(hours);
  tft.print(":");
  if (minutes < 10) { tft.print("0"); }
  tft.print(minutes);
  tft.print(":");
  if (seconds < 10) { tft.print("0"); }
  tft.println(seconds);

  tft.println("          ");

  tft.setTextColor(ST7735_WHITE, background_color);
  tft.print("V:    " + voltage_padding);
  tft.println(load_voltage, 2);
  tft.print("mA:   " + current_padding);
  tft.println(current_mA, 2);
  tft.print("mW:   " + power_padding);
  tft.println(load_voltage * current_mA, 2);
  tft.print("mWh:  " + energy_padding);
  tft.println(energy, 2);
  tft.print("mAh:  " + capacity_padding);
  tft.println(capacity, 2);

  if(channel_number == 1){
    tft.setTextColor(ST7735_RED, background_color);
  }else if (channel_number == 2) {
    tft.setTextColor(ST7735_GREEN, background_color);
  }else {
    tft.setTextColor(ST7735_BLUE, background_color);
  }

  tft.print("CH:");
  tft.print(channel_number);
  
  tft.setTextColor(ST7735_WHITE, background_color);
  battery_voltage = get_battery_voltage();
  if(battery_voltage == 0){
    tft.print("    P:USB");
  }else if(battery_voltage > 3.6){
    tft.print("   B:");
    tft.print(get_battery_voltage());
  }else{
    tft.setTextColor(ST7735_RED, background_color);
    tft.print("   B:");
    tft.print(get_battery_voltage());
  }
  

  // ----- Display the data ----- //
}


void convert_time() {
  unsigned long elapsedMillis = currentMillis - start_delay;
  seconds = elapsedMillis / 1000;
  minutes = seconds / 60;
  hours = minutes / 60;
  days = hours / 24;
  elapsedMillis %= 1000;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
}

void measure_values() {
  if (use_channel_1) {
    shunt_voltage_1 = INA3221.getShuntVoltage(INA3221_CH1);  //Value will be returned in uV.
    bus_voltage_1 = INA3221.getVoltage(INA3221_CH1);
    current_mA_1 = INA3221.getCurrent(INA3221_CH1) * 100;  //Value will be returned in Amps. Adjust if you need mA
    load_voltage_1 = bus_voltage_1 + (shunt_voltage_1 / 1000000);
    energy_1 = energy_1 + (load_voltage_1 * current_mA_1 * interval) / 3600000;
    capacity_1 = capacity_1 + (current_mA_1 * interval) / 3600000;
  }
  if (use_channel_2) {
    shunt_voltage_2 = INA3221.getShuntVoltage(INA3221_CH2);  //Value will be returned in uV.
    bus_voltage_2 = INA3221.getVoltage(INA3221_CH2);
    current_mA_2 = INA3221.getCurrent(INA3221_CH2) * 100;  //Value will be returned in Amps. Adjust if you need mA
    load_voltage_2 = bus_voltage_2 + (shunt_voltage_2 / 1000000);
    energy_2 = energy_2 + (load_voltage_2 * current_mA_2 * interval) / 3600000;
    capacity_2 = capacity_2 + (current_mA_2 * interval) / 3600000;
  }
  if (use_channel_3) {
    shunt_voltage_3 = INA3221.getShuntVoltage(INA3221_CH3);  //Value will be returned in uV.
    bus_voltage_3 = INA3221.getVoltage(INA3221_CH3);
    current_mA_3 = INA3221.getCurrent(INA3221_CH3) * 100;  //Value will be returned in Amps. Adjust if you need mA
    load_voltage_3 = bus_voltage_3 + (shunt_voltage_3 / 1000000);
    energy_3 = energy_3 + (load_voltage_3 * current_mA_3 * interval) / 3600000;
    capacity_3 = capacity_3 + (current_mA_3 * interval) / 3600000;
  }
}

void boot_sequesnce() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  tft.fillScreen(background_color);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.setTextColor(ST7735_RED, background_color);
  tft.setTextWrap(false);

  tft.println("Booting ...");
  tft.println("");
  tft.setTextColor(ST7735_WHITE, background_color);

  // ----- Check if SD Card is OK ----- //
  tft.print("SD Card");
  if (!SD.begin(SDCARD_CS)) {
    Serial.println("Card failed, or not present");
    tft.setTextColor(ST7735_RED, background_color);
    tft.println("     X");
    tft.setTextColor(ST7735_WHITE, background_color);
    use_sd_card = false;
  } else {
    tft.println("    OK");
  }
  // ----- Check if SD Card is OK ----- //


  // ----- Wait for WIFI ----- //
  tft.print("WIFI");
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    tft.println("       OK");
  } else {
    tft.setTextColor(ST7735_RED, background_color);
    tft.println("        X");
    tft.setTextColor(ST7735_WHITE, background_color);
  }
  // ----- Wait for WIFI ----- //


  // ----- Get Time ----- //
  tft.print("TIME");
  timeClient.begin();
  if (timeClient.update()) {
    rtc.setTime(timeClient.getEpochTime());
    rtcOffset = millis();
    tft.println("       OK");
    ntpcConnected = true;
  } else {
    Serial.println("NTP Failed!");
    tft.setTextColor(ST7735_RED, background_color);
    tft.println("        X");
    tft.setTextColor(ST7735_WHITE, background_color);
  }
  // ----- Get Time ----- //


  // ----- Battery check ----- //
  
  battery_voltage = get_battery_voltage();
  if (battery_voltage == 0){
    tft.println("POWER:    USB");
  }else if (battery_voltage < 3.5) {
    tft.print("BATTERY. ");
    tft.setTextColor(ST7735_RED, background_color);
    tft.println(battery_voltage);
    tft.setTextColor(ST7735_WHITE, background_color);
  } else {
    tft.print("BATTERY. ");
    tft.println(battery_voltage);
  }
  // ----- Battery check ----- //


  INA3221.setWarnAlertCurrentLimit(INA3221_CH1, -1);
  delay(250);
  INA3221.setCritAlertCurrentLimit(INA3221_CH1, -1);
  delay(500);
  INA3221.setWarnAlertCurrentLimit(INA3221_CH1, 1000);
  delay(250);
  INA3221.setCritAlertCurrentLimit(INA3221_CH1, 1500);
  delay(250);

  tft.println("             ");
  tft.setTextColor(ST7735_GREEN, background_color);
  tft.println("BOOT COMPLETE");
  tft.setTextColor(ST7735_WHITE, background_color);
}


float get_battery_voltage() {
  float sensorValue = 0;
  for (int i = 1; i <= 20; i++) {
    sensorValue = sensorValue + analogRead(0);
  }
  sensorValue = sensorValue / 20;

  float voltage = ((sensorValue * 5) / 4095) - 0.1; //remove offset error
  if(sensorValue > 3900){ //USB power
    return 0;
  }
  return voltage;

  if(battery_voltage < voltage){
    return battery_voltage;
  }else{
    return voltage;
  }
}


void setup_menu() {

  tft.fillScreen(background_color);
  tft.setCursor(0, 0);
  tft.setTextSize(2);
  tft.setTextColor(ST7735_WHITE, background_color);
  tft.setTextWrap(false);

  while (started == false) {
    if (left_button_flag == 1) {
      if (mode) {
        if (selected == 0 || selected == 1 || selected == 2 || selected == 3) {
          selected = 4;
        } else {
          selected += 1;
        }
      } else {
        selected = selected + 1;
      }
      left_button_flag = 0;
    }
    if (selected > 5) {
      selected = 0;
    }
    if (right_button_flag == 1) {
      right_button_flag = 0;
      if (selected == 0) {
        mode = !mode;
      } else if (selected == 1 && mode == false) {
        use_channel_1 = !use_channel_1;
      } else if (selected == 2 && mode == false) {
        use_channel_2 = !use_channel_2;
      } else if (selected == 3 && mode == false) {
        use_channel_3 = !use_channel_3;
      } else if (selected == 4) {
        selected_avg = selected_avg + 1;
        if (selected_avg > 8) {
          selected_avg = 1;
        }
      } else if (selected == 5) {
        if (setup_error == false) {
          started = true;
          ignore_input = true;
        }
      }
    }

    tft.setCursor(0, 0);
    tft.setTextColor(ST7735_WHITE, background_color);
    if (selected == 0) {
      tft.print(">");
    } else {
      tft.print(" ");
    }
    tft.print("MODE:");

    if (mode) {
      tft.println(" EFF");
    } else {
      tft.println(" NORM");
    }
    tft.setTextColor(ST7735_RED, background_color);
    if (setup_error == true && mode == false) {
      tft.print("    ERROR");
    } else {
      tft.print("     ");
    }
    tft.setTextColor(ST7735_WHITE, background_color);
    tft.println("             ");


    if (selected == 1) {
      tft.print(">");
    } else {
      tft.print(" ");
    }
    tft.setTextColor(ST7735_RED, background_color);
    tft.print("CH1: ");
    tft.setTextColor(ST7735_WHITE, background_color);
    if (mode == true) {
      tft.println(" OUTPUT");
    } else if (use_channel_1) {
      channel_number = 1;
      tft.println(" ENABLE");
    } else {
      tft.setTextColor(ST7735_RED, background_color);
      tft.println("DISABLE");
      tft.setTextColor(ST7735_WHITE, background_color);
    }


    if (selected == 2) {
      tft.print(">");
    } else {
      tft.print(" ");
    }
    tft.setTextColor(ST7735_GREEN, background_color);
    tft.print("CH2: ");
    tft.setTextColor(ST7735_WHITE, background_color);
    if (mode == true) {
      tft.setTextColor(ST7735_RED, background_color);
      tft.println("DISABLE");
      tft.setTextColor(ST7735_WHITE, background_color);
    } else if (use_channel_2) {
      tft.println(" ENABLE");
    } else {
      tft.setTextColor(ST7735_RED, background_color);
      tft.println("DISABLE");
      tft.setTextColor(ST7735_WHITE, background_color);
    }


    if (selected == 3) {
      tft.print(">");
    } else {
      tft.print(" ");
    }
    tft.setTextColor(ST7735_BLUE, background_color);
    tft.print("CH3: ");
    tft.setTextColor(ST7735_WHITE, background_color);
    if (mode == true) {
      tft.println("  INPUT");
    } else if (use_channel_3) {
      tft.println(" ENABLE");
    } else {
      tft.setTextColor(ST7735_RED, background_color);
      tft.println("DISABLE");
      tft.setTextColor(ST7735_WHITE, background_color);
    }

    // check to ensure the first channel displayed is actually enabled
    if(use_channel_1){
      channel_number = 1;
    } else if(use_channel_2){
      channel_number = 2;
    }else if(use_channel_3){
      channel_number = 3;
    }

    if (selected == 4) {
      tft.print(">");
    } else {
      tft.print(" ");
    }
    tft.print("AVG: ");
    if (selected_avg == 1) {
      tft.println("      1");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_1);
    } else if (selected_avg == 2) {
      tft.println("      4");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_4);
    } else if (selected_avg == 3) {
      tft.println("     16");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_16);
    } else if (selected_avg == 4) {
      tft.println("     64");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_64);
    } else if (selected_avg == 5) {
      tft.println("    128");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_128);
    } else if (selected_avg == 6) {
      tft.println("    256");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_256);
    } else if (selected_avg == 7) {
      tft.println("    512");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_512);
    } else if (selected_avg == 8) {
      tft.println("   1024");
      INA3221.setAveragingMode(INA3221_REG_CONF_AVG_1024);
    }


    tft.println("             ");

    if (selected == 5) {
      tft.print(">");
    } else {
      tft.print(" ");
    }
    tft.println("START");

    if (use_channel_1 == false && use_channel_2 == false && use_channel_3 == false) {
      setup_error = true;
    } else {
      setup_error = false;
    }

    delay(200);
  }
  tft.fillScreen(background_color);
}


void handle_left_Interrupt() {
  left_button_flag = 1;
}


void handle_right_Interrupt() {
  right_button_flag = 1;
}



void create_file() {
  if (use_sd_card == true) {

    //file_name = "/" + String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay) + "_" + String(timeClient.getHours()) + "-" + String(timeClient.getMinutes()) + "-" + String(timeClient.getSeconds()) + ".txt";
    if(ntpcConnected){
      file_name = "/" + String(rtc.getTime("%Y-%B-%d_%H-%M-%S")) + ".txt";
    }
    
    File Log = SD.open(file_name, FILE_WRITE);
    if (Log) {
      Log.print("date");
      Log.print(",");
      Log.print("time");
      if (use_channel_1 == true) {
        Log.print(",");
        Log.print("load voltage 1");
        Log.print(",");
        Log.print("current mA 1");
        Log.print(",");
        Log.print("power mW 1");
        Log.print(",");
        Log.print("energy mWh 1");
        Log.print(",");
        Log.print("capacity mAh 1");
      }
      if (use_channel_2 == true) {
        Log.print(",");
        Log.print("load voltage 2");
        Log.print(",");
        Log.print("current mA 2");
        Log.print(",");
        Log.print("power mW 2");
        Log.print(",");
        Log.print("energy mWh 2");
        Log.print(",");
        Log.print("capacity mAh 2");
      }
      if (use_channel_3 == true) {
        Log.print(",");
        Log.print("load voltage 3");
        Log.print(",");
        Log.print("current mA 3");
        Log.print(",");
        Log.print("power mW 3");
        Log.print(",");
        Log.print("energy mWh 3");
        Log.print(",");
        Log.print("capacity mAh 3");
      }
      if (mode) {
        Log.print(",");
        Log.print("efficiency");
      }

      Log.println();
      Log.close();
    }

    file_active = true;
  }
}


void write_file() {
  File Log = SD.open(file_name, FILE_APPEND);
  if (Log) {
    //Serial.println(String(rtc.getTime("%Y/%B/%D %H:%M:%S:")) + String((currentMillis-rtcOffset)%1000));
    Log.print(String(rtc.getTime("%y/%m/%d")));
    Log.print(",");
    Log.print(String(rtc.getTime("%H:%M:%S:")) + String((currentMillis - rtcOffset) % 1000));

    if (use_channel_1 == true) {
      Log.print(",");
      Log.print(load_voltage_1);
      Log.print(",");
      Log.print(current_mA_1);
      Log.print(",");
      Log.print(load_voltage_1 * current_mA_1);
      Log.print(",");
      Log.print(energy_1);
      Log.print(",");
      Log.print(capacity_1);
    }
    if (use_channel_2 == true) {
      Log.print(",");
      Log.print(load_voltage_2);
      Log.print(",");
      Log.print(current_mA_2);
      Log.print(",");
      Log.print(load_voltage_2 * current_mA_2);
      Log.print(",");
      Log.print(energy_2);
      Log.print(",");
      Log.print(capacity_2);
    }
    if (use_channel_3 == true) {
      Log.print(",");
      Log.print(load_voltage_3);
      Log.print(",");
      Log.print(current_mA_3);
      Log.print(",");
      Log.print(load_voltage_3 * current_mA_3);
      Log.print(",");
      Log.print(energy_3);
      Log.print(",");
      Log.print(capacity_3);
    }
    if (mode) {
      Log.print(",");
      Log.print(efficiency);
    }
    Log.println();
    Log.close();
  }
}
