#include "Arduino.h"
#include "EEPROM.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "Wire.h"
#include "mySD.h"
#define ARDUINOJSON_DECODE_UNICODE 1
#include "ArduinoJson.h"
#include "GxEPD2_32_BW.h"
#include "GxEPD2_32_3C.h"
#include "time.h"
#include "esp32-hal.h"
#include "driver/adc.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "Berkelium5pt7b.h"
#include "UdvarhelySansBold7pt7b.h"
#include "UdvarhelySansRegular7pt7b.h"

// ----------------------------------------

#define DEBUG false
#define LOGGER true
#define ConfigVersion 125

#define uSToSFactor 1000000UL
#define SecondsPerMinute 60U
#define SecondsPerHour 3600UL
#define SecondsPerDay (24L * SecondsPerHour)
#define NumberOfSeconds(_time_) (_time_ % SecondsPerMinute)  
#define NumberOfMinutes(_time_) ((_time_ / SecondsPerMinute) % SecondsPerMinute)
#define NumberOfHours(_time_) ((_time_ % SecondsPerDay) / SecondsPerHour)
#define NumberOfDays(_time_) (_time_ / SecondsPerDay)

#define GxEPD_YELLOW GxEPD_RED
#define SeparationLine "----------------------------------------"

// ----------------------------------------

_VOID _EXFUN(tzset, (_VOID));
int _EXFUN(setenv, (const char *__string, const char *__value, int __overwrite));

// ----------------------------------------

DynamicJsonDocument Data(2560);
GxEPD2_32_3C Display(GxEPD2::GDEW075Z09, 5, 17, 16, 4);
File Logger;

// ----------------------------------------

typedef struct {
  struct tm TimeInfo;
  time_t NowTime;
  unsigned long ScheduledDateTime;
  unsigned long ScheduledDelay;
  unsigned long LastSleepTime;
  unsigned long LastRefreshTime;
} RTCMemory;

RTC_DATA_ATTR RTCMemory RTC;
RTC_DATA_ATTR byte BootCount = -1;
String LoggerPath;
bool LoggerStatus = false;

// ----------------------------------------

struct {
  uint8_t Version;
  uint8_t CpuFrequency;
  int BaudRate;
  struct {
    bool StaticIP;
    bool Accents;
  } Enable;
  struct {
    uint8_t Led;
    gpio_num_t Wakeup;
    gpio_num_t Battery;
  } GPIO;
  struct {
    const char* Hostname;
    const char* SSID;
    const char* Password;
    uint8_t LocalIP[4];
    uint8_t Subnet[4];
    uint8_t Gateway[4];
    uint8_t PrimaryDNS[4];
    uint8_t SecondaryDNS[4];
  } Auth;
  struct {
    const char* Server;
    long GMTOffset;
    int DayLightOffset;
    const char* TimeZone;
  } NTP;
  struct {
    const char* Url;
    const char* ApiKey;
  } Server;
  struct {
    uint8_t Pins[4];
    int16_t Width;
    int16_t Height;
  } Display;
  struct {
    uint8_t Period;
    uint8_t OffsetHours;
    uint8_t OffsetMinutes;
    uint8_t ExecutionTimeLimit;
  } Scheduler;
  struct {
    uint8_t Pins[4];
    const char* Folder;
  } Logger;
} CFG;

// ----------------------------------------

enum FontStyle {REGULAR, BOLD};
enum FontColor {WHITE, BLACK, YELLOW};
enum TextAlignment {LEFT, RIGHT, CENTER} ;
enum TextLetterCase {NORMAL, LOWERCASE, UPPERCASE};

// ----------------------------------------

#if DEBUG
  #define dBegin(...) Serial.begin(__VA_ARGS__)
  #define dSetDebugOutput(...) Serial.setDebugOutput(__VA_ARGS__)
  #define dPrint(...) Serial.print(__VA_ARGS__)
  #define dPrintln(...) Serial.println(__VA_ARGS__)
  #define dPrintf(...) Serial.printf(__VA_ARGS__)
  #define dFlush() Serial.flush()
#else
  #define dBegin(...)
  #define dSetDebugOutput(...)
  #if LOGGER
    #define dPrint(...) { if (BootCount > 0 && LoggerStatus) { File Logger = SD.open(LoggerPath.c_str(), FILE_WRITE); Logger.print(__VA_ARGS__); Logger.close(); } }
    #define dPrintln(...) { if (BootCount > 0 && LoggerStatus) { File Logger = SD.open(LoggerPath.c_str(), FILE_WRITE); Logger.println(__VA_ARGS__); Logger.close(); } }
    #define dPrintf(...) { if (BootCount > 0 && LoggerStatus) { File Logger = SD.open(LoggerPath.c_str(), FILE_WRITE); Logger.printf(__VA_ARGS__); Logger.close(); } }
  #else
    #define dPrint(...)
    #define dPrintln(...)
    #define dPrintf(...)
  #endif
  #define dFlush()
#endif

// ----------------------------------------

class App {

  private: 
    size_t mEEPROMSize = 256;
    uint16_t mEEPROMAddress = 0;
    uint8_t mBatteryPercentage = 100;
    float mVoltage;

    template <class T> void WriteEEPROM(const T &value) {
      const byte *p = (const byte *)(const void *)&value;
      int i;
      int ee = mEEPROMAddress;
      for (i = 0; i < (int)sizeof(value); i++) EEPROM.write(ee++ , *p++);
      EEPROM.commit();
    };

    template <class T> void ReadEEPROM(T &value) {
      byte *p = (byte *)(void *)&value;
      int i;
      int ee = mEEPROMAddress;
      for (i = 0; i < (int)sizeof(value); i++) *p++= EEPROM.read(ee++);
    };

    void InitConfig() {
      EEPROM.begin(mEEPROMSize);
      ReadEEPROM(CFG);
      if (CFG.Version != ConfigVersion) {
        CFG = { 
          ConfigVersion, 240, 115200, 
          {true, false}, 
          {LED_BUILTIN, GPIO_NUM_39, GPIO_NUM_35}, 
          {"HomeEinkDisplay", "Szeklerman", "tokosmagor2012", {192, 168, 0, 50}, {255, 255, 255, 0}, {192, 168, 0, 1}, {192, 168, 0, 1}, {0, 0, 0, 0}}, 
          {"pool.ntp.org", 3, 0, "EET-2EEST,M3.5.0/3,M10.5.0/4"}, 
          {"http://www.szeklerman.com/~apps/expenses-display/api", "vjTK2KMhYz"}, 
          {{5, 17, 16, 4}, Display.width(), Display.height()}, 
          {24, 7, 0, 2},
          {{13, 15, 2, 14}, "HED"}
        };
        WriteEEPROM(CFG);
        ReadEEPROM(CFG);
      };
    };

    void InitLogger() {
      if (BootCount <= 0) return;
      if (!SD.begin(CFG.Logger.Pins[0], CFG.Logger.Pins[1], CFG.Logger.Pins[2], CFG.Logger.Pins[3])) {
        LoggerStatus = false;
        return;
      };
      if (SD.cardType() == 0) {
        LoggerStatus = false;
        return;
      };
      LoggerStatus = true;
      String loggerTemp = String(String(CFG.Logger.Folder) + "/" + String(RTC.TimeInfo.tm_year + 1900) + "/" + ((int)(RTC.TimeInfo.tm_mon + 1) <= 9 ? "0" : "") + String(RTC.TimeInfo.tm_mon + 1));
      char loggerDirectory[13];
      strcpy(loggerDirectory, loggerTemp.c_str());
      SD.mkdir(loggerDirectory);
      LoggerPath = String(loggerTemp + "/" + ((int)RTC.TimeInfo.tm_mday <= 9 ? "0" : "") + String(RTC.TimeInfo.tm_mday) + ".log");
      Logger = SD.open(LoggerPath.c_str(), FILE_WRITE);
      Logger.println(SeparationLine);
      Logger.close();
      Logger = SD.open(LoggerPath.c_str());
    };

    bool MeasureBattery() {
      mVoltage = analogRead(CFG.GPIO.Battery) / 4096.0 * 7.23;
      mBatteryPercentage = 2808.3808 * pow(mVoltage, 4) - 43560.9157 * pow(mVoltage, 3) + 252848.5888 * pow(mVoltage, 2) - 650767.4615 * mVoltage + 626532.5703;
      if (mVoltage >= 4.20) mBatteryPercentage = 100;
      if (mVoltage <= 3.50) mBatteryPercentage = 0;
      if (mBatteryPercentage <= 0) return false;
      return true;
    };

    String SDCardType(uint8_t cardType) {
      switch (cardType) {
        case SD_CARD_TYPE_SD1: return "SD1";
        case SD_CARD_TYPE_SD2: return "SD2";
        case SD_CARD_TYPE_SDHC: return "SDHC";
        default: return "N/A";
      };
    };

    void DeviceInfo() {
      dPrintln(SeparationLine);
      if (BootCount == 0) {
        dPrintf("First boot (%d)\n", BootCount);
      }else {
        dPrintf("Boot nr. %d\n", BootCount);
      };
      dPrintln(SeparationLine);
      dPrintf("Device name: %s\n", WiFi.getHostname());
      dPrintf("CPU frequency: %dMHz\n", getCpuFrequencyMhz());
      dPrint("Battery power: ");
      dPrint(String(mBatteryPercentage));
      dPrint("% (");
      dPrint(String(mVoltage));
      dPrintln("V)");
      if (mBatteryPercentage <= 0) dPrintln("Low battery");
      #if LOGGER
        if (LoggerStatus) {
          dPrintln(SeparationLine);
          dPrintln("SD Card attached the device");
          dPrintf("SD Card Type: %s\n", SDCardType(SD.cardType()).c_str());
          dPrintf("SD Card Size: %3.2fGB\n", (float)SD.cardSize() / (1024 * 1024));
        };
      #endif
    };

    String WifiType(wifi_auth_mode_t encryptionType) {
      switch (encryptionType) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA-WPA2-PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENTERPRISE";
        case WIFI_AUTH_MAX: return "WiFi Auth Max";
        default: return "N/A";
      };
    };

    void ConnectWiFi() {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_STA);
      WiFi.setAutoConnect(true);
      WiFi.setAutoReconnect(true);
      WiFi.setHostname(CFG.Auth.Hostname);
      if (CFG.Enable.StaticIP) {
        IPAddress local_IP(CFG.Auth.LocalIP[0], CFG.Auth.LocalIP[1], CFG.Auth.LocalIP[2], CFG.Auth.LocalIP[3]);
        IPAddress subnet(CFG.Auth.Subnet[0], CFG.Auth.Subnet[1], CFG.Auth.Subnet[2], CFG.Auth.Subnet[3]);
        IPAddress gateway(CFG.Auth.Gateway[0], CFG.Auth.Gateway[1], CFG.Auth.Gateway[2], CFG.Auth.Gateway[3]);
        IPAddress primaryDNS(CFG.Auth.PrimaryDNS[0], CFG.Auth.PrimaryDNS[1], CFG.Auth.PrimaryDNS[2], CFG.Auth.PrimaryDNS[3]);
        IPAddress secondaryDNS(CFG.Auth.SecondaryDNS[0], CFG.Auth.SecondaryDNS[1], CFG.Auth.SecondaryDNS[2], CFG.Auth.SecondaryDNS[3]);
        if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
          dPrintln(SeparationLine);
          dPrintln("Failed to configure STA");
        };
      };
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(CFG.Auth.SSID, CFG.Auth.Password);
        delay(250);
        DeviceInfo();
        delay(3000);
        int16_t numberOfNetworks = WiFi.scanNetworks();
        dPrintln(SeparationLine);
        dPrint("Number of Wireless Networks: ");
        if (numberOfNetworks <= 0) { 
          dPrintln("N/A");
        } else {
          dPrintln(numberOfNetworks); 
          dPrintln(SeparationLine);
          for (int16_t i = 0; i < numberOfNetworks; i++) {
            String wifiType = WifiType(WiFi.encryptionType(i));
            dPrintf("%d. %s | %ddBm | %s\n", (i + 1), WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i), wifiType.c_str());
          };
        };
        dPrintln(SeparationLine);
        dPrintf("Connecting to %s", CFG.Auth.SSID);
        uint8_t retry = 0;
        while (WiFi.status() != WL_CONNECTED) {
          delay(500);
          if (++retry > 10) {
            retry = 0;
            WiFi.begin(CFG.Auth.SSID, CFG.Auth.Password);
            while (WiFi.status() != WL_CONNECTED) {
              delay(5000);
              break;
            };
          };
        };
        dPrintln(" success!");
        #if DEBUG
          dPrintln(SeparationLine);
          dPrintf("SSID: %s\n", WiFi.SSID().c_str());
          dPrintf("IP Address: %s\n", WiFi.localIP().toString().c_str());
          dPrintf("Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
          dPrintf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
          dPrintf("DNS 1: %s\n", WiFi.dnsIP().toString().c_str());
          dPrintf("DNS 2: %s\n", WiFi.dnsIP(1).toString().c_str());
          dPrintf("MAC Address: %s\n", WiFi.macAddress().c_str());
        #endif  
      };
    };

    void DisconnectWiFi() {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      dPrintln(SeparationLine);
      dPrintln("Disconnected from a Wireless Network");
      btStop();
      adc_power_off();
      esp_wifi_stop();
      esp_bt_controller_disable();
      dPrintln(SeparationLine);
      dPrintln("Turn off peripherals: WiFi, BT/BLE, ADC");
    };

    void ConnectToNTPServer() {
      dPrintln(SeparationLine);
      dPrintf("Connecting to %s", CFG.NTP.Server);    
      while (!getLocalTime(&RTC.TimeInfo)) {
        configTime((CFG.NTP.GMTOffset * SecondsPerHour), (CFG.NTP.DayLightOffset * SecondsPerHour), CFG.NTP.Server);
        delay(500);
      };
      dPrintln(" success!");
      dPrint("Current time: ");
      GetTime();
      dPrintln(&RTC.TimeInfo, "%Y.%m.%d. %H:%M:%S");
    };

    void GetTime() {
      if (getLocalTime(&RTC.TimeInfo)) {
        time(&RTC.NowTime);
        setenv("TZ", CFG.NTP.TimeZone, 1);
        tzset();
        localtime(&RTC.NowTime);
      };
    };

    void CalcScheduledTime() {
      GetTime();
      unsigned long periodTime = (CFG.Scheduler.Period * SecondsPerHour);
      unsigned long offsetTime = (CFG.Scheduler.OffsetHours * SecondsPerHour) + (CFG.Scheduler.OffsetMinutes * SecondsPerMinute);
      unsigned long scheduledTime = (RTC.NowTime - offsetTime) / periodTime * periodTime + periodTime + offsetTime - (CFG.NTP.GMTOffset * SecondsPerHour);
      if (scheduledTime < RTC.NowTime) scheduledTime = scheduledTime + SecondsPerDay;
      RTC.ScheduledDateTime = scheduledTime;
    };

    void CalcScheduledDelay() {
      CalcScheduledTime();
      RTC.ScheduledDelay = RTC.ScheduledDateTime - RTC.NowTime;
    };

    void PrintEpochTime(String startSign, unsigned long time, String endSign = "") {
      dPrint(startSign);
      if (time == 0) {
        dPrintln("0");
        return;
      };
      if (String(time).length() == 10) {
        PrintEpochDateTime(time);
        return;
      };
      if (time < SecondsPerMinute) {
        PrintWithLeadingZero(NumberOfSeconds(time), " sec");
      } else if (time == SecondsPerHour) {
        PrintWithLeadingZero(NumberOfMinutes(time), " min");
      } else if (time < SecondsPerHour) {
        PrintWithLeadingZero(NumberOfMinutes(time), ":");
        PrintWithLeadingZero(NumberOfSeconds(time), " min");
      } else if (time < SecondsPerDay) {
        PrintWithLeadingZero(NumberOfHours(time), ":");
        PrintWithLeadingZero(NumberOfMinutes(time), ":");
        PrintWithLeadingZero(NumberOfSeconds(time), " hour(s)");
      } else if (time == SecondsPerDay) {
        PrintWithLeadingZero(NumberOfDays(time), " day");  
      } else if (time > SecondsPerDay) {
        PrintWithLeadingZero(NumberOfDays(time), " day(s) ");
        PrintWithLeadingZero(NumberOfHours(time), ":");
        PrintWithLeadingZero(NumberOfMinutes(time), ":");
        PrintWithLeadingZero(NumberOfSeconds(time));
      };
      dPrintln(endSign);
    };

    void PrintEpochDateTime(unsigned long epochtime) {
      uint8_t daysOfMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      long currYear, daysTillNow, extraTime, extraDays, index, date, month, hours, minutes, seconds, flag = 0;
      epochtime = epochtime + (CFG.NTP.GMTOffset * SecondsPerHour);
      daysTillNow = epochtime / (24 * SecondsPerHour);
      extraTime = epochtime % (24 * SecondsPerHour);
      currYear = 1970;
      while (daysTillNow >= 365) {
        if (currYear % 400 == 0 || (currYear % 4 == 0 && currYear % 100 != 0)) {
          daysTillNow -= 366;
        } else {
          daysTillNow -= 365;
        };
        currYear += 1;
      };
      extraDays = daysTillNow + 1;
      if (currYear % 400 == 0 || (currYear % 4 == 0 && currYear % 100 != 0)) flag = 1;
      month = 0, index = 0;
      if (flag == 1) {
        while (true) {
          if (index == 1) {
            if (extraDays - 29 < 0) break;
            month += 1;
            extraDays -= 29;
          } else {
            if (extraDays - daysOfMonth[index] < 0) break;
            month += 1;
            extraDays -= daysOfMonth[index];
          };
          index += 1;
        };
      } else {
        while (true) {
          if (extraDays - daysOfMonth[index] < 0) break;
          month += 1;
          extraDays -= daysOfMonth[index];
          index += 1;
        };
      };
      if (extraDays > 0) {
        month += 1;
        date = extraDays;
      } else {
        if (month == 2 && flag == 1) {
          date = 29;
        } else {
          date = daysOfMonth[month - 1];
        };
      };
      hours = extraTime / SecondsPerHour;
      minutes = (extraTime % SecondsPerHour) / SecondsPerMinute;
      seconds = (extraTime % SecondsPerHour) % SecondsPerMinute;
      dPrint(currYear);
      dPrint(".");
      PrintWithLeadingZero(month, ".");
      PrintWithLeadingZero(date, ". ");
      PrintWithLeadingZero(hours, ":");
      PrintWithLeadingZero(minutes, ":");
      PrintWithLeadingZero(seconds);
      dPrintln();
    };

    void PrintWithLeadingZero(byte digits, String endSign = "") {
      if (digits <= 9 || (digits <= 9 && endSign != " day")) dPrint("0");
      dPrint(digits, DEC);
      dPrint(endSign);
    }; 

    bool WakeupReason(esp_sleep_source_t exWakeupReason) {
      esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
      #if DEBUG
        dPrintln(SeparationLine);
        switch (wakeupReason) {
          case ESP_SLEEP_WAKEUP_EXT0: dPrintln("Wake-up system by RTC_IO"); break;
          case ESP_SLEEP_WAKEUP_EXT1: dPrintln("Wake-up system by RTC_CNTL"); break;
          case ESP_SLEEP_WAKEUP_TIMER: dPrintln("Wake-up system by TIMER"); break;
          case ESP_SLEEP_WAKEUP_TOUCHPAD: dPrintln("Wake-up system by TOUCHPAD"); break;
          case ESP_SLEEP_WAKEUP_ULP: dPrintln("Wake-up system by ULP"); break;
          default: dPrintf("Boot up system normally (CODE: %d)\n", wakeupReason); break;
        };
      #endif
      return (wakeupReason == exWakeupReason) ? true : false;
    };

    void GoToDeepSleep(unsigned long scheduledDelay = RTC.ScheduledDelay) {
      unsigned long deepSleepEnd = scheduledDelay != RTC.ScheduledDelay ? RTC.NowTime + scheduledDelay : RTC.ScheduledDateTime;
      dPrintln(SeparationLine);
      dPrintln("Going to deep sleep...");
      PrintEpochTime("Start: ", RTC.NowTime);
      PrintEpochTime("End: ", deepSleepEnd);
      dPrintln(SeparationLine);
      PrintEpochTime("Execution time: ", seconds());
      dFlush();
      esp_sleep_enable_timer_wakeup(scheduledDelay * uSToSFactor);
      esp_sleep_enable_ext0_wakeup(CFG.GPIO.Wakeup, 0);
      esp_deep_sleep_start();
    };
    
    String RemoveAccents(String string) {
      uint16_t i = 0;
      uint16_t stringLength = string.length() + 1; 
      char text[stringLength];
      string.toCharArray(text, stringLength);
      uint16_t textLength = sizeof(text);
      do {
        char cText = text[i];
        char nText = text[i + 1];
        if ((int)cText == 195) {
          switch ((int)nText) {
            case 129: text[i] = 'A'; break;
            case 137: text[i] = 'E'; break;
            case 141: text[i] = 'I'; break;
            case 147: case 150: text[i] = 'O'; break;          
            case 154: case 156: text[i] = 'U'; break;
            case 161: text[i] = 'a'; break;
            case 169: text[i] = 'e'; break;
            case 173: text[i] = 'i'; break;
            case 179: case 182: text[i] = 'o'; break;
            case 186: case 188: text[i] = 'u'; break;
            default: break;
          };
          for (uint16_t j = i + 1; j < textLength; j++) text[j] = text[j + 1];     
          textLength--;
        } else if ((uint16_t)cText == 197) {
          switch ((uint16_t)nText) {
            case 144: text[i] = 'O'; break;
            case 145: text[i] = 'o'; break;            
            case 176: text[i] = 'U'; break;
            case 177: text[i] = 'u'; break;          
            default: break;      
          };
          for (uint16_t j = i + 1; j < textLength; j++) text[j] = text[j + 1];
          textLength--;    
        };
        i++;
      } while (i < textLength);
      return String(text);
    };

    void DrawString(int x, int y, String text, FontStyle style = REGULAR, FontColor color = BLACK, TextAlignment align = LEFT, TextLetterCase letter = NORMAL) {
      int16_t x1, y1;
      uint16_t w, h;
      if (CFG.Enable.Accents == false) text = RemoveAccents(text);
      switch (letter) {
        case UPPERCASE: text.toUpperCase(); break;
        case LOWERCASE: text.toLowerCase(); break;
        default: break;
      };
      switch (style) {
        case BOLD: Display.setFont(&UdvarhelySansBold7pt7b); break;
        case REGULAR: default: Display.setFont(&UdvarhelySansRegular7pt7b); break;
      };
      switch (color) {
        case WHITE: Display.setTextColor(GxEPD_WHITE); break;
        case YELLOW: Display.setTextColor(GxEPD_YELLOW); break;
        case BLACK: default: Display.setTextColor(GxEPD_BLACK); break;      
      };
      Display.setTextWrap(false);
      Display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
      switch (align) {
        case RIGHT: x = x - w; break;
        case CENTER: x = x - w / 2; break;
        case LEFT: default: break;
      };
      h += (14 - h); 
      if (text.length() <= 3 && text.indexOf(".") > 0) h -= 1;
      Display.setCursor(x, (y + h));
      Display.println(text);
    };

    uint16_t TextWidth(String text, FontStyle style = REGULAR, TextLetterCase letter = NORMAL) {
      int16_t x1, y1;
      uint16_t w, h;
      if (CFG.Enable.Accents == false) text = RemoveAccents(text);
      switch(style) {
        case BOLD: Display.setFont(&UdvarhelySansBold7pt7b); break;
        case REGULAR: default: Display.setFont(&UdvarhelySansRegular7pt7b); break;
      }; 
      switch(letter) {
        case UPPERCASE: text.toUpperCase(); break;
        case LOWERCASE: text.toLowerCase(); break;
        default: break;
      };  
      Display.setTextWrap(false);
      Display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
      return w;
    };    

    bool GetData() {
      HTTPClient http;
      dPrintln(SeparationLine);
      dPrint("Connecting to Data Server");
      http.begin(String(CFG.Server.Url) + String("?key=") + String(CFG.Server.ApiKey));
      uint16_t httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        dPrintln(" succes!");
        dPrint("HTTP Status Code: ");
        dPrintln(httpCode);
        DeserializationError error = deserializeJson(Data, http.getString());
        dPrintf("Json data size: %d byte\n", Data.memoryUsage());
        if (error) {
          dPrintln(SeparationLine);
          dPrintln("Json deserialize failed!");
          return false;
        };
        #if DEBUG
          dPrintln("Requested data: ");
          serializeJsonPretty(Data, Serial);
          dPrintln();
        #endif
        http.end();
        return true;         
      };
      dPrintln(" failed!");
      dPrint("HTTP Status Code: ");
      dPrintln(httpCode);
      dPrintln("Error on HTTP request!");
      http.end();
      return false;
    };

    void DrawEmptyBattery(String message = "Az akkumulátor lemerült!") {
      uint16_t lWidth = 2;
      uint16_t bWidth = 200;
      uint16_t bHeight = 80;
      uint16_t b2Width = 15;
      uint16_t b2Height = 40;
      uint16_t bXPos1 = (CFG.Display.Width - bWidth - b2Width) / 2;
      uint16_t bYPos1 = (CFG.Display.Height - bHeight) / 2;
      uint16_t roundCorner = 8;
      for (uint16_t i = 1; i <= lWidth; i++) {
        Display.drawRoundRect(bXPos1, bYPos1, bWidth, bHeight, roundCorner, GxEPD_BLACK);
        bXPos1++; 
        bYPos1++;
        bWidth -= 2; 
        bHeight -= 2;
        roundCorner--;
      };
      Display.fillRoundRect((bXPos1 + bWidth), (bYPos1 + ((bHeight - b2Height) / 2)), b2Width, b2Height, 3, GxEPD_BLACK);
      DrawString((CFG.Display.Width / 2 - b2Width  + 3 ), ((CFG.Display.Height / 4) * 3), message, REGULAR, BLACK, CENTER, NORMAL);
    };

    void DrawHeader(String title = "Számlafizetési kötelezettségek") {
      GetTime();
      char nowDate[100];
      strftime(nowDate, sizeof(nowDate), "%Y.%m.%d. %H:%M:%S", &RTC.TimeInfo);  
      uint16_t x = 13;
      uint16_t y = 6;
      if (Data.containsKey("title")) title = Data["title"].as<String>();
      uint16_t txtWidth = TextWidth(title, BOLD, UPPERCASE);
      Display.fillRect(x, 0, (x + txtWidth + 8), 28, GxEPD_YELLOW);
      DrawString((x + 10), y, title, BOLD, BLACK, LEFT, UPPERCASE);
      DrawString((x + txtWidth + 30), y, "Frissítés: " + String(nowDate), REGULAR, BLACK, LEFT, NORMAL);
      txtWidth = TextWidth(String(mBatteryPercentage) + "%", REGULAR, NORMAL);
      x = (CFG.Display.Width - txtWidth - 48); 
      DrawString((CFG.Display.Width - 14), y, String(mBatteryPercentage) + "%", REGULAR, BLACK, RIGHT, NORMAL);
      y += 5; 
      Display.drawRect((x + 7), y, 19, 10, GxEPD_BLACK);
      Display.fillRect((x + 26), (y + 2), 2, 6, GxEPD_BLACK);
      Display.fillRect((x + 9), (y + 2), (15 * mBatteryPercentage/100.0), 6, ((mBatteryPercentage <= 40) ? GxEPD_YELLOW : GxEPD_BLACK));
    };

    void DrawData() {
      DrawHeader();
      int maxItems = Data["data"]["sum"]["max_items"];
      int unpaidItems = Data["data"]["sum"]["unpaid"];
      int debitItems = Data["data"]["sum"]["debit"];
      int paidItems = Data["data"]["sum"]["paid"];
      JsonArray rows = Data["data"]["debit"].as<JsonArray>();  
      FontStyle fontStyle[5] = {REGULAR, BOLD, REGULAR, REGULAR, REGULAR};
      TextAlignment textAlignment[5] = {RIGHT, LEFT, LEFT, RIGHT, RIGHT};
      TextLetterCase textLetterCase[5] = {NORMAL, UPPERCASE, NORMAL, NORMAL, NORMAL};
      uint16_t headerYStart = 28;
      uint16_t colXPos[5] = {38, 50, 362, 574, 622};
      uint16_t lineXPos1 = 13;
      uint16_t* lineYPos1 = &headerYStart;
      uint16_t lineXPos2 = (CFG.Display.Width - lineXPos1);
      uint16_t* lineYPos2 = lineYPos1;
      Display.drawLine(lineXPos1, *lineYPos1, lineXPos2, *lineYPos2, GxEPD_BLACK);
      JsonArray dataTitle = Data["data"]["header"].as<JsonArray>();
      int i = 0;
      for (JsonVariant title : dataTitle) {
        DrawString(colXPos[i], (headerYStart + (debitItems < maxItems ? 1 : 5)), title.as<String>(), REGULAR, BLACK, textAlignment[i], NORMAL);
        i++;
      };
      *lineYPos1 += (debitItems<maxItems ? 22 : 28);
      Display.drawLine(lineXPos1, *lineYPos1, lineXPos2, *lineYPos2, GxEPD_BLACK);
      uint16_t rowSpaceH = 3;
      uint16_t rowH = 22;
      uint16_t rowYStart = (*lineYPos1 + rowSpaceH + 2);
      uint16_t* rowXPos1 = &lineXPos1;
      uint16_t rowYPos1 = rowYStart;
      uint16_t rowXPos2 = (CFG.Display.Width - (lineXPos1 * 2) + 1);
      uint16_t rowYPos2 = rowH;
      uint16_t circleXPos = (CFG.Display.Width - lineXPos1 - 12);
      int nr = 1;
      int rowStyle = 1;
      for (JsonVariant row : rows) {
        if (row[3] == 2 && rowStyle != row[3] && debitItems < maxItems) {
          for (uint16_t xPos = lineXPos1; xPos <= lineXPos2; xPos += 4) Display.drawLine(xPos, rowYPos1, (xPos + 2), rowYPos1, GxEPD_BLACK);
          rowYPos1 += (rowSpaceH + 1);
          rowStyle = row[3];
        };
        if (row[3] == 3 && rowStyle != row[3]) {
          Display.drawLine(lineXPos1, rowYPos1, lineXPos2, rowYPos1, GxEPD_BLACK);
          rowYPos1 += (rowSpaceH + 1);
          rowStyle = row[3];
        };
        if (row[3] == 2) Display.fillRect(*rowXPos1, rowYPos1, rowXPos2, rowYPos2, GxEPD_YELLOW);
        DrawString(colXPos[0], (rowYPos1 + 1), String(nr) + ".", fontStyle[0], BLACK, RIGHT, textLetterCase[0]);
        for (int j = 0; j <= 2; j++) {
          DrawString(colXPos[j + 1], (rowYPos1 + 1), row[j].as<String>(), (row[3] == 2 ? fontStyle[j + 1] : REGULAR), BLACK, textAlignment[j + 1], textLetterCase[j + 1]);
          if (j == 0 && row[4] == 1) {
            String badgeText = "AUTO";
            int badgeXPos1 = (colXPos[1] + TextWidth(row[0].as<String>(), (row[3] == 2 ? fontStyle[j + 1] : REGULAR), UPPERCASE) + 7);
            int badgeYPos1 = (rowYPos1 + 6);
            int16_t bX1, bY1;
            uint16_t bW, bH;
            Display.setTextWrap(false);
            Display.setFont(&Berkelium5pt7b);
            Display.setTextColor((row[3] == 2 ? GxEPD_YELLOW : GxEPD_BLACK));
            Display.getTextBounds(badgeText, 0, 0, &bX1, &bY1, &bW, &bH);             
            int badgeXPos2 = (bW + 8);
            int badgeYPos2 = 11;
            Display.fillRoundRect(badgeXPos1, badgeYPos1, badgeXPos2, badgeYPos2, 1, (row[3] == 2 ? GxEPD_BLACK : GxEPD_YELLOW));
            Display.setCursor((badgeXPos1 + 4), (badgeYPos1 + bH + 1));
            Display.println(badgeText);
          };
        };
        uint16_t circleYPos = (rowYPos1 + rowH / 2);
        Display.fillCircle(circleXPos, circleYPos, 5, GxEPD_WHITE);
        Display.drawCircle(circleXPos, circleYPos, 5, GxEPD_BLACK);
        if (row[3] == 2) {
          Display.fillCircle(circleXPos, circleYPos, 2, GxEPD_YELLOW);
        } else if (row[3] == 3) {
          Display.fillCircle(circleXPos, circleYPos, 2, GxEPD_BLACK);
        };
        rowYPos1 += (rowH + rowSpaceH);
        nr++;
      };
      for (int xPos = lineXPos1; xPos <= lineXPos2; xPos += 4) Display.drawLine(xPos, rowYPos1, (xPos + 2), rowYPos1, GxEPD_BLACK);
      int16_t x1, y1;
      uint16_t w, h;
      uint16_t y = 368;
      uint8_t x_1 = 14;
      uint16_t x_2 = 627;
      String rest = Data["data"]["note"]["sum"];
      String note = Data["data"]["note"]["note"];
      if (CFG.Enable.Accents == false) { 
        rest = RemoveAccents(rest);
        note = RemoveAccents(note);
      };
      rest.toUpperCase();
      note.toUpperCase();
      Display.setTextWrap(false);
      Display.setFont(&Berkelium5pt7b);
      Display.setTextColor(GxEPD_BLACK);
      Display.getTextBounds(rest, x_1, y, &x1, &y1, &w, &h);
      Display.setCursor(x_1, (y + h - (debitItems == 0 || (unpaidItems == 0 && paidItems == 1) ? 2 : 0)));
      Display.println(rest); 
      Display.fillRect(*rowXPos1, (CFG.Display.Height - 3), (*rowXPos1 + w - 11), CFG.Display.Height, GxEPD_YELLOW);
      Display.getTextBounds(note, x_2, y, &x1, &y1, &w, &h);
      Display.setCursor((x_2 - w), (y + h - (debitItems == 0 || (unpaidItems == 0 && paidItems == 1 ) ? 2 : 0)));
      Display.println(note);
    };

    void RefreshEinkDisplay(bool state = true) {
      if (state) {
        if (GetData()) {
          DisconnectWiFi();
          dPrintln(SeparationLine);
          dPrintln("Display refresh start");
          Display.setFullWindow();
          Display.firstPage();
          do {
            Display.fillScreen(GxEPD_WHITE);
            DrawData();
          } while (Display.nextPage());
          Display.powerOff();
          dPrintln("Display refresh end");
          dPrintln(SeparationLine);
          dPrintln("Turn off the display");
          RTC.LastRefreshTime = RTC.NowTime;
          return;
        };
        dPrintln("Restarting device...");
        #if DEBUG
          delay(1000);
        #endif
        GoToDeepSleep(10);
      };
      dPrintln(SeparationLine);
      dPrintln("Display refresh start");
      Display.setFullWindow();
      Display.firstPage();
      do {
        Display.fillScreen(GxEPD_WHITE);
        DrawEmptyBattery();
      } while (Display.nextPage());
      Display.powerOff();
      dPrintln("Display refresh end");
      dPrintln(SeparationLine);
      dPrintln("Turn off the display");
    };

    void PrintMessageIfFirstBootOrNot(String message, bool state) {
      String wakeupReason = "";
      if (state) wakeupReason = ", manually";
      if (RTC.LastSleepTime == 0) {
        dPrintf("First boot (%s)\n", message.c_str());
      } else {
        dPrintf("Waked-up %s!\n", (message + wakeupReason).c_str());
      };
    };

    unsigned long IRAM_ATTR seconds() {
      return (unsigned long) (esp_timer_get_time() / uSToSFactor);
    };

  public: 
    void Init() {
      InitConfig();
      dBegin(CFG.BaudRate);
      dSetDebugOutput(true);
      dSetDebugOutput(false);
      for (uint8_t i = 0; i < 3; i++) dPrintln();
      pinMode(SS, OUTPUT);
      pinMode(CFG.GPIO.Led, OUTPUT);
      digitalWrite(CFG.GPIO.Led, LOW);
      Display.init();
      Display.setRotation(0);
      BootCount++;
      InitLogger();
      if (MeasureBattery() == false) {
        RefreshEinkDisplay(false);
        GoToDeepSleep(365 * SecondsPerDay);
      };
      ConnectWiFi();
      ConnectToNTPServer();
      CalcScheduledDelay();
      if (RTC.LastSleepTime != 0) {
        dPrintln(SeparationLine);
        dPrintln("Previous deep sleep");
        PrintEpochTime("Start: ", RTC.LastSleepTime);
        PrintEpochTime("End: ", RTC.NowTime);
      };
      bool wakeupReason = WakeupReason(ESP_SLEEP_WAKEUP_EXT0);
      if (wakeupReason) RefreshEinkDisplay(true);
      long executionTimeLimit = CFG.Scheduler.ExecutionTimeLimit * SecondsPerHour;
      long halfSecPeriodFactor = (CFG.Scheduler.Period * SecondsPerHour) / 2;
      long delta = ((RTC.NowTime - RTC.ScheduledDateTime) + halfSecPeriodFactor) * -1;
      executionTimeLimit = executionTimeLimit > halfSecPeriodFactor ? halfSecPeriodFactor : executionTimeLimit;
      unsigned long overTime = halfSecPeriodFactor - delta;
      dPrintln(SeparationLine);
      if (overTime <= executionTimeLimit) {
        PrintMessageIfFirstBootOrNot("after due time", wakeupReason);
        PrintEpochTime("Over time: ", overTime);
        unsigned long lastScheduledDateTime = RTC.ScheduledDateTime - SecondsPerDay;
        if (RTC.LastRefreshTime <= lastScheduledDateTime) RefreshEinkDisplay(true);
      } else {
        PrintMessageIfFirstBootOrNot("before time", wakeupReason);
        PrintEpochTime("Time left: ", RTC.ScheduledDelay);
      };
      GoToDeepSleep();
    };
    
} HomeApp;

// ----------------------------------------

void setup() {
  HomeApp.Init();
};

void loop() { };