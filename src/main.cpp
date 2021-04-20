#include "Arduino.h"
#include "EEPROM.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "Wire.h"
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_7C.h>
#include "tinyxml2.h"
#define ARDUINOJSON_DECODE_UNICODE 1
#include "ArduinoJson.h"
#include "time.h"
#include "esp32-hal.h"
#include "driver/adc.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "Berkelium5pt7b.h"
#include "UdvarhelySansRegular7pt7b.h"
#include "UdvarhelySansBold7pt7b.h"
#include "UdvarhelySansRegular9pt7b.h"
#include "UdvarhelySansBold9pt7b.h"
#include "UdvarhelySansRegular16pt7b.h"
#include "UdvarhelySansBold16pt7b.h"

// ----------------------------------------

#define DEBUG false
#define PRINT_DATA false
#define ConfigVersion 1

#define uSToSFactor 1000000UL
#define SecondsPerMinute 60UL
#define SecondsPerHour 3600UL
#define SecondsPerDay 86400UL
#define NumberOfSeconds(_time_) (_time_ % SecondsPerMinute)  
#define NumberOfMinutes(_time_) ((_time_ / SecondsPerMinute) % SecondsPerMinute)
#define NumberOfHours(_time_) ((_time_ % SecondsPerDay) / SecondsPerHour)
#define NumberOfDays(_time_) (_time_ / SecondsPerDay)

#define SeparationLine "----------------------------------------"

// ----------------------------------------

using namespace tinyxml2;

// ----------------------------------------

_VOID _EXFUN(tzset, (_VOID));
int _EXFUN(setenv, (const char *__string, const char *__value, int __overwrite));

// ----------------------------------------

DynamicJsonDocument Data(6144);
GxEPD2_BW<GxEPD2_213_B73, GxEPD2_213_B73::HEIGHT> Display(GxEPD2_213_B73(5, 17, 16, 4));

// ----------------------------------------

typedef struct {
  struct tm TimeInfo;
  time_t NowTime;
  unsigned long ScheduledDateTime;
  unsigned long ScheduledDelay;
  unsigned long LastSleepTime;
  struct {
    struct {
      float Value;
      float PrevValue;
      int8_t Index;
    } Eur;
    struct {
      float Value;
      float PrevValue;
      int8_t Index;
    } Usd;
    struct {
      float Value;
      float PrevValue;
      int8_t Index;
    } Huf;
    struct {
      float Value;
      float PrevValue;
      int8_t Index;
    } Btc;    
  } Currencies;
  struct {
    char NameDays[30];
    struct {
      char Name[20];
      char Date[20];
    } BirthDay;
  } GCalendar;
} RTCMemory;

RTC_DATA_ATTR RTCMemory RTC;
RTC_DATA_ATTR byte BootCount = -1;

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
    const char* BNR;
    struct {
      const char* Url;
      const char* NameDays;
      const char* BirthDays;
      const char* ApiKey;
    } GCalendar;
    const char* BTC;
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
} CFG;

// ----------------------------------------

enum FontStyle {REGULAR, BOLD};
enum FontColor {WHITE, BLACK};
enum TextAlignment {LEFT, RIGHT, CENTER} ;
enum TextLetterCase {NORMAL, LOWERCASE, UPPERCASE};
enum CurrencyType {BNR, BTC};
enum CurrencyRateIndex {UP_INDEX, DOWN_INDEX};
enum GCalendarType {NAMEDAYS, BIRTHDAYS};
enum GCalendarFeature {TODAY, UPCOMING};

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
  #define dPrint(...)
  #define dPrintln(...)
  #define dPrintf(...)
  #define dFlush()
#endif

// ----------------------------------------

class App {

  private: 
    size_t mEEPROMSize = 512;
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
          {"HomeEinkBadge", "Szeklerman", "tokosmagor2012", {192, 168, 0, 51}, {255, 255, 255, 0}, {192, 168, 0, 1}, {192, 168, 0, 1}, {0, 0, 0, 0}}, 
          {"pool.ntp.org", 3, 0, "EET-2EEST,M3.5.0/3,M10.5.0/4"}, 
          {"https://www.bnr.ro/nbrfxrates.xml", {"https://www.googleapis.com/calendar/v3/calendars/", "nd0bsakc20brgj1k84g34n145s@group.calendar.google.com", 
          "ka9f0r2ntq81omtftucrp5cucs@group.calendar.google.com", "AIzaSyDeSk9u__88kLbiAm-U873VGrNFzb0ax3A"},
          "https://bitpay.com/rates/RON"},
          {{5, 17, 16, 4}, Display.height(), Display.width()}, 
          {24, 7, 0, 2}
        };
        WriteEEPROM(CFG);
        ReadEEPROM(CFG);
      };
    };

    bool MeasureBattery() {
      mVoltage = analogRead(CFG.GPIO.Battery) / 4096.0 * 7.23;
      mBatteryPercentage = 2808.3808 * pow(mVoltage, 4) - 43560.9157 * pow(mVoltage, 3) + 252848.5888 * pow(mVoltage, 2) - 650767.4615 * mVoltage + 626532.5703;
      if (mVoltage >= 4.20) mBatteryPercentage = 100;
      if (mVoltage <= 3.50) mBatteryPercentage = 0;
      if (mBatteryPercentage <= 0) return false;
      return true;
    };

    void DeviceInfo(bool state = true) {
      dPrintln(SeparationLine);
      if (BootCount == 0) {
        dPrintf("First boot (%d)\n", BootCount);
      }else {
        dPrintf("Boot nr. %d\n", BootCount);
      };
      dPrintln(SeparationLine);
      dPrintf("Device name: %s\n", (state ? WiFi.getHostname() : CFG.Auth.Hostname));
      dPrintf("CPU frequency: %dMHz\n", getCpuFrequencyMhz());
      dPrint("Battery power: ");
      dPrint(String(mBatteryPercentage));
      dPrint("% (");
      dPrint(String(mVoltage));
      dPrintln("V)");
      if (mBatteryPercentage <= 0) dPrintln("Low battery!");
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
        DeviceInfo(true);
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
      setenv("TZ", CFG.NTP.TimeZone, 1);
      tzset();
      dPrintln(SeparationLine);
      dPrintf("Connecting to %s", CFG.NTP.Server);
      while (!getLocalTime(&RTC.TimeInfo)) {
        configTime((CFG.NTP.GMTOffset * SecondsPerHour), (CFG.NTP.DayLightOffset * SecondsPerHour), CFG.NTP.Server);
        delay(500);
      };
      dPrintln(" success!");
      GetTime();
      PrintEpochTime("Current time: ", RTC.NowTime);
    };

    void GetTime() {
      if (getLocalTime(&RTC.TimeInfo)) {
        time(&RTC.NowTime);
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
      } else if (time == (365 * SecondsPerDay)) {
        PrintWithLeadingZero(NumberOfDays(time), " day(s)");
      } else if (time > SecondsPerDay) {
        PrintWithLeadingZero(NumberOfDays(time), " day(s) ");
        PrintWithLeadingZero(NumberOfHours(time), ":");
        PrintWithLeadingZero(NumberOfMinutes(time), ":");
        PrintWithLeadingZero(NumberOfSeconds(time));
      };
      dPrintln(endSign);
    };

    void PrintWithLeadingZero(uint16_t digits, String endSign = "") {
      if (digits <= 9 || (digits <= 9 && endSign != " day")) dPrint("0");
      dPrint(digits, DEC);
      dPrint(endSign);
    };

    void PrintEpochDateTime(unsigned long epochtime) {
      dPrintln(ConvertEpochToDateTime(epochtime));
    };

    String ConvertEpochToDateTime(unsigned long epochtime = RTC.NowTime, const char* format = "%d.%02d.%02d %02d:%02d:%02d") {
      char dateTime[100];
      uint8_t daysOfMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      long year, daysTillNow, extraTime, extraDays, index, day, month, hours, minutes, seconds, flag = 0;
      epochtime = epochtime + (CFG.NTP.GMTOffset * SecondsPerHour);
      daysTillNow = epochtime / SecondsPerDay;
      extraTime = epochtime % SecondsPerDay;
      year = 1970;
      while (daysTillNow >= 365) {
        if (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0)) {
          daysTillNow -= 366;
        } else {
          daysTillNow -= 365;
        };
        year += 1;
      };
      extraDays = daysTillNow + 1;
      if (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0)) flag = 1;
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
        day = extraDays;
      } else {
        if (month == 2 && flag == 1) {
          day = 29;
        } else {
          day = daysOfMonth[month - 1];
        };
      };
      hours = extraTime / SecondsPerHour;
      minutes = (extraTime % SecondsPerHour) / SecondsPerMinute;
      seconds = (extraTime % SecondsPerHour) % SecondsPerMinute;
      snprintf(dateTime, sizeof(dateTime), format, year, month, day, hours, minutes, seconds);
      return String(dateTime);
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
      if (RTC.NowTime != 0) {
        PrintEpochTime("Start: ", RTC.NowTime);
        PrintEpochTime("End: ", deepSleepEnd);
      } else {
        PrintEpochTime("Deep sleep length: ", scheduledDelay);
      };
      dPrintln(SeparationLine);
      PrintEpochTime("Execution time: ", seconds());
      dFlush();
      esp_sleep_enable_timer_wakeup(scheduledDelay * uSToSFactor);
      esp_sleep_enable_ext0_wakeup(CFG.GPIO.Wakeup, 0);
      esp_deep_sleep_start();
    };

    bool GetCurrencies(CurrencyType type) {
      HTTPClient http;
      if (type == BNR) {
        XMLDocument xml;
        dPrintln(SeparationLine);
        dPrint("Connecting to BNR Server");
        http.begin(String(CFG.Server.BNR));
        uint16_t httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
          dPrintln(" succes!");
          dPrintf("HTTP Status Code: %d\n", httpCode);
          String httpString = http.getString();
          dPrintf("Data size: %d byte\n", http.getSize());
          if (xml.Parse(httpString.c_str(), 20000U) != XML_SUCCESS) {
            dPrintln("XML parsing failed!");
            return false; 
          };
          uint8_t status = 0;
          XMLElement* currencies = xml.FirstChildElement()->LastChildElement("Body")->LastChildElement("Cube")->FirstChildElement("Rate");
          for (; currencies != NULL; currencies = currencies->NextSiblingElement()) {
            if (currencies->Attribute("currency", "EUR")) {
              currencies->QueryFloatText(&RTC.Currencies.Eur.Value);
              if (RTC.Currencies.Eur.Index == 0) RTC.Currencies.Eur.Index = UP_INDEX;
              RTC.Currencies.Eur.Index = RTC.Currencies.Eur.PrevValue < RTC.Currencies.Eur.Value ? UP_INDEX : DOWN_INDEX;
              if (RTC.Currencies.Eur.PrevValue != RTC.Currencies.Eur.Value) RTC.Currencies.Eur.PrevValue = RTC.Currencies.Eur.Value;
              status += 1;
            };
            if (currencies->Attribute("currency", "USD")) {
              currencies->QueryFloatText(&RTC.Currencies.Usd.Value);
              if (RTC.Currencies.Usd.Index == 0) RTC.Currencies.Usd.Index = UP_INDEX;
              RTC.Currencies.Usd.Index = RTC.Currencies.Usd.PrevValue < RTC.Currencies.Usd.Value ? UP_INDEX : DOWN_INDEX;
              if (RTC.Currencies.Usd.PrevValue != RTC.Currencies.Usd.Value) RTC.Currencies.Usd.PrevValue = RTC.Currencies.Usd.Value;              
              status += 1;
            };
            if (currencies->Attribute("currency", "HUF")) {
              currencies->QueryFloatText(&RTC.Currencies.Huf.Value);
              if (RTC.Currencies.Huf.Index == 0) RTC.Currencies.Huf.Index = UP_INDEX;
              RTC.Currencies.Huf.Index = RTC.Currencies.Huf.PrevValue < RTC.Currencies.Huf.Value ? UP_INDEX : DOWN_INDEX;
              if (RTC.Currencies.Huf.PrevValue != RTC.Currencies.Huf.Value) RTC.Currencies.Huf.PrevValue = RTC.Currencies.Huf.Value;               
              status += 1;
            };
          };
          dPrintf("XML parsed %s!\n", (status == 3 ? "successful" : "failed"));
          dPrintf("EURO %.4f | USD %.4f | HUF %.4f\n", RTC.Currencies.Eur.Value, RTC.Currencies.Usd.Value, RTC.Currencies.Huf.Value);
          if (status < 3) return false;
          http.end();
          return true;
        };
        http.end();
        dPrintln(" failed!");
        dPrintf("HTTP Status Code: %d\n", httpCode);
        dPrintln("Error on HTTP request!");
        return false;
      } else if (type == BTC) {
        dPrintln(SeparationLine);
        dPrint("Connecting to Bitpay Server");
        http.begin(String(CFG.Server.BTC));
        uint16_t httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
          dPrintln(" succes!");
          dPrintf("HTTP Status Code: %d\n", httpCode);
          DeserializationError error = deserializeJson(Data, http.getString());
          dPrintf("Data size: %d byte\n", Data.memoryUsage());
          if (error) {
            dPrintln("Json deserialize failed!");
            return false;
          };
          #if PRINT_DATA
            dPrintln("Requested data: ");
            serializeJsonPretty(Data, Serial);
            dPrintln();
          #endif
          http.end();
          if (Data.size() == 0) return false;
          RTC.Currencies.Btc.Value = Data["data"]["rate"];
          if (RTC.Currencies.Btc.Index == 0) RTC.Currencies.Btc.Index = UP_INDEX;
          RTC.Currencies.Btc.Index = RTC.Currencies.Btc.PrevValue < RTC.Currencies.Btc.Value ? UP_INDEX : DOWN_INDEX;
          if (RTC.Currencies.Btc.PrevValue != RTC.Currencies.Btc.Value) RTC.Currencies.Btc.PrevValue = RTC.Currencies.Btc.Value;
          dPrintln("Json serialized successful!");
          dPrintf("BTC: %.2f\n", RTC.Currencies.Btc.Value);
          return true;
        };
        http.end();
        dPrintln(" failed!");
        dPrintf("HTTP Status Code: %d\n", httpCode);
        dPrintln("Error on HTTP request!");
        return false;
      };
      return false;
    };

    bool GetGCalendarData(GCalendarType type) {
      HTTPClient http;
      String nameDays = "";
      String birthdayName = "";
      String birthdayDate = "";
      String timeMin = ConvertEpochToDateTime(RTC.NowTime, "%d-%02d-%02dT00:00:00Z");
      String timeMax = ConvertEpochToDateTime(RTC.NowTime, "%d-%02d-%02dT23:59:59Z");
      String url = "";
      if (type == NAMEDAYS) {
        url = String(String(CFG.Server.GCalendar.Url) + String(CFG.Server.GCalendar.NameDays) + "/events?timeMin=" + timeMin + "&timeMax=" + timeMax + "&orderBy=startTime&singleEvents=True&maxResults=4&key=" + String(CFG.Server.GCalendar.ApiKey));
      } else if (type == BIRTHDAYS) {
        timeMax = ConvertEpochToDateTime((RTC.NowTime + (183 * SecondsPerDay)), "%d-%02d-%02dT00:00:00Z");
        url = String(String(CFG.Server.GCalendar.Url) + String(CFG.Server.GCalendar.BirthDays) + "/events?timeMin=" + timeMin + "&timeMax=" + timeMax + "&orderBy=startTime&singleEvents=True&maxResults=1&key=" + String(CFG.Server.GCalendar.ApiKey));
      };
      dPrintln(SeparationLine);
      dPrintf("Connecting to GCalendar/ %s", (type == NAMEDAYS ? "Namedays" : (type == BIRTHDAYS ? "Birthdays" : "N/A")));
      http.begin(url);
      uint16_t httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        dPrintln(" succes!");
        dPrintf("HTTP Status Code: %d\n", httpCode);
        DeserializationError error = deserializeJson(Data, http.getString());
        dPrintf("Data size: %d byte\n", Data.memoryUsage());
        if (error) {
          dPrintln("Json deserialize failed!");
          return false;
        };
        #if PRINT_DATA
          dPrintln("Requested data: ");
          serializeJsonPretty(Data, Serial);
          dPrintln();
        #endif
        http.end();
        if (Data.size() == 0) return false;
        JsonArray rows = Data["items"].as<JsonArray>();
        for (JsonVariant row : rows) {
          if (row["summary"].as<String>().length() != 0) {
            String startDate = row["start"]["date"].as<String>().substring(-5, 5);
            String endDate = row["end"]["date"].as<String>().substring(-5, 5);
            String nowDate = ConvertEpochToDateTime(RTC.NowTime, "%d-%02d-%02d").substring(-5, 5);
            if (type == NAMEDAYS) {
              String nextDate = ConvertEpochToDateTime((RTC.NowTime + SecondsPerDay), "%d-%02d-%02d").substring(-5, 5);
              if (startDate == nowDate && endDate == nextDate) {
                if (nameDays.length() == 0) {
                  nameDays = row["summary"].as<String>();
                } else {
                  nameDays += String(", " + row["summary"].as<String>());
                };
              };
            } else if (type == BIRTHDAYS) {
              birthdayName = row["summary"].as<String>();
              String tBirthdayDate = String(row["description"].as<String>() + "-" + startDate);
              birthdayDate = tBirthdayDate;
              birthdayDate.replace("-", ".");
            };
          };
        };
        dPrintln("Json serialized successful!");
        if (type == NAMEDAYS) {
          strcpy(RTC.GCalendar.NameDays, nameDays.c_str());
          dPrintf("Namedays: %s\n", RTC.GCalendar.NameDays);
        };
        if (type == BIRTHDAYS) {
          strcpy(RTC.GCalendar.BirthDay.Name, birthdayName.c_str());
          strcpy(RTC.GCalendar.BirthDay.Date, birthdayDate.c_str());
          dPrintf("Birthday: %s (%s)\n", RTC.GCalendar.BirthDay.Name, RTC.GCalendar.BirthDay.Date);
        };
        return true;
      };
      http.end();
      dPrintln(" failed!");
      dPrintf("HTTP Status Code: %d\n", httpCode);
      dPrintln("Error on HTTP request!");
      return false;
    };

    bool GetData() {
      bool result = false;
      uint8_t runCalc = 0;
      uint8_t retry = 0;
      for (;;) {
        if (GetCurrencies(BNR)) runCalc += 1;
        if (GetCurrencies(BTC)) runCalc += 1;
        if (GetGCalendarData(NAMEDAYS)) runCalc += 1;
        if (GetGCalendarData(BIRTHDAYS)) runCalc += 1;
        if (runCalc == 4) {
          result = true;
          break;
        };
        if (runCalc < 4) {
          runCalc = 0;
          delay(1000);
        };
        if (++retry > 5) break;
      };
      return result;
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

    uint16_t DrawString(int16_t x, int16_t y, String text, const GFXfont *font, FontColor color = BLACK, TextAlignment align = LEFT, TextLetterCase letter = NORMAL) {
      int16_t x1, y1;
      uint16_t w, h;
      if (CFG.Enable.Accents == false) text = RemoveAccents(text);
      switch (letter) {
        case UPPERCASE: text.toUpperCase(); break;
        case LOWERCASE: text.toLowerCase(); break;
        default: break;
      };
      Display.setFont(font);
      switch (color) {
        case WHITE: Display.setTextColor(GxEPD_WHITE); break;
        case BLACK: default: Display.setTextColor(GxEPD_BLACK); break;      
      };
      Display.setTextWrap(false);
      Display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
      switch (align) {
        case RIGHT: x = x - w; break;
        case CENTER: x = x - w / 2; break;
        case LEFT: default: break;
      };
      Display.setCursor(x, (y + h));
      Display.println(text.c_str());
      return x + TextWidth(text, font, letter);
    };

    uint16_t TextWidth(String text, const GFXfont *font, TextLetterCase letter = NORMAL) {
      int16_t x1, y1;
      uint16_t w, h;
      if (CFG.Enable.Accents == false) text = RemoveAccents(text);
      Display.setFont(font);
      switch(letter) {
        case UPPERCASE: text.toUpperCase(); break;
        case LOWERCASE: text.toLowerCase(); break;
        default: break;
      };  
      Display.setTextWrap(false);
      Display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
      return w;
    }; 

    uint16_t DrawIndex(int16_t x, int16_t y, uint8_t index, FontColor color) {
      uint8_t indexW = 10;
      uint8_t indexH = 7;
      uint16_t indexColor = GxEPD_BLACK;
      switch (color) {
        case WHITE: indexColor = GxEPD_WHITE; break;
        case BLACK: indexColor = GxEPD_BLACK; break;      
      };
      int16_t x0, y0, x1, y1, x2, y2;
      x0 = x;
      x1 = x + (indexW / 2);
      x2 = x + indexW;
      y0 = y2 = index == 1 ? y + indexH : y;
      y1 = index == 1 ? y : y + indexH;
      Display.fillTriangle(x0, y0, x1, y1, x2, y2, indexColor);        
      return x + indexW;
    };

    void DrawEmptyBattery() {
      uint8_t lWidth = 2;
      uint8_t bWidth = 145;
      uint8_t bHeight = 50;
      uint8_t b2Width = 10;
      uint8_t b2Height = 25;
      uint8_t bXPos1 = (CFG.Display.Width - bWidth - b2Width) / 2;
      uint8_t bYPos1 = (CFG.Display.Height - bHeight) / 2;
      uint8_t roundCorner = 8;
      for (uint8_t i = 1; i <= lWidth; i++) {
        Display.drawRoundRect(bXPos1, bYPos1, bWidth, bHeight, roundCorner, GxEPD_BLACK);
        bXPos1++; 
        bYPos1++;
        bWidth -= 2; 
        bHeight -= 2;
        roundCorner--;
      };
      Display.fillRoundRect((bXPos1 + bWidth), (bYPos1 + ((bHeight - b2Height) / 2)), b2Width, b2Height, 3, GxEPD_BLACK);
      DrawString((CFG.Display.Width / 2 - b2Width + 3 ), ((CFG.Display.Height / 4) * 3 + 5), String("Az akkumulátor lemerült!"), &UdvarhelySansRegular7pt7b, BLACK, CENTER, NORMAL);
    };

    void DrawData() {
      uint8_t space = 6;
      uint8_t marginW = 7;
      uint8_t headerW = CFG.Display.Width;
      uint8_t headerH = 16;
      uint8_t headerContentH = 8;
      uint8_t headerMarginH = (headerH - headerContentH) / 2;
      Display.fillRect(0, 0, headerW, headerH, GxEPD_BLACK);
      DrawString(marginW, headerMarginH, String("Frissites: " + ConvertEpochToDateTime()), &Berkelium5pt7b, WHITE, LEFT, UPPERCASE);  
      uint8_t batteryW = 18;
      uint8_t batteryX = headerW - batteryW - marginW;
      uint8_t batteryY = headerMarginH;
      DrawString((batteryX - 4), headerMarginH, String(String(mBatteryPercentage) + "%"), &Berkelium5pt7b, WHITE, RIGHT, UPPERCASE);
      Display.drawRect(batteryX, batteryY, (batteryW - 2), headerContentH, GxEPD_WHITE);
      Display.fillRect((batteryX + batteryW - 2), (batteryY + 2), 2, (headerContentH / 2), GxEPD_WHITE);
      Display.fillRect((batteryX + 2), (batteryY + 2), ceil(((batteryW - 6) * mBatteryPercentage) / 100.0), (headerContentH / 2), GxEPD_WHITE);
      uint8_t displayYPos = headerH + space;
      // ----------------------------------------
      uint8_t dateVLineH = 50;
      char month[3], day[3];
      strftime(month, sizeof(month), "%m", &RTC.TimeInfo);
      strftime(day, sizeof(day), "%d", &RTC.TimeInfo);
      uint16_t monthW = TextWidth(String(month), &UdvarhelySansBold16pt7b, NORMAL);
      uint16_t dayW = TextWidth(String(day), &UdvarhelySansBold16pt7b, NORMAL);
      uint16_t dateTextW = max(monthW, dayW);
      uint16_t dateTextXPos = space + (dateTextW / 2);
      DrawString(dateTextXPos, (displayYPos + 1), String(month), &UdvarhelySansRegular16pt7b, BLACK, CENTER, NORMAL);
      DrawString(dateTextXPos, (displayYPos + (dateVLineH / 2) + 2), String(day), &UdvarhelySansBold16pt7b, BLACK, CENTER, NORMAL); 
      // ----------------------------------------
      uint8_t displayXPos = marginW + dateTextW + 10;
      uint8_t dateVLineY1 = displayYPos;
      uint8_t dateVLineY2 = displayYPos + dateVLineH;
      Display.drawLine(displayXPos, displayYPos, displayXPos, dateVLineY2, GxEPD_BLACK);
      displayXPos += 15;
      // ----------------------------------------
      uint8_t namesTextY1 = dateVLineY1;
      uint8_t namesTextY2 = namesTextY1 + 9;
      uint8_t namesTextY3 = namesTextY1 + (dateVLineH / 2) + 1;
      uint8_t namesTextY4 = namesTextY3 + 10;
      DrawString(displayXPos, namesTextY1, String("Szuletesnap"), &Berkelium5pt7b, BLACK, LEFT, UPPERCASE);
      uint16_t birthDayXPos = DrawString(displayXPos, namesTextY2, String(RTC.GCalendar.BirthDay.Name), &UdvarhelySansBold7pt7b, BLACK, LEFT, UPPERCASE);
      DrawString(birthDayXPos, (namesTextY2 + 2), String(" | " + String(RTC.GCalendar.BirthDay.Date)), &Berkelium5pt7b, BLACK, LEFT, NORMAL);
      DrawString(displayXPos, namesTextY3, String("Nevnap(ok)"), &Berkelium5pt7b, BLACK, LEFT, UPPERCASE);
      TextLetterCase namesLetterCase = String(RTC.GCalendar.NameDays).length() <= 20 ? UPPERCASE : NORMAL;
      DrawString(displayXPos, namesTextY4, String(RTC.GCalendar.NameDays), &UdvarhelySansBold7pt7b, BLACK, LEFT, namesLetterCase);
      // ----------------------------------------
      displayYPos = dateVLineY2 + space;
      Display.drawLine(0, displayYPos, CFG.Display.Width, displayYPos, GxEPD_BLACK);
      displayYPos += space;
      // ----------------------------------------
      uint8_t currencyXPos = marginW;
      uint8_t currencyY1Pos = displayYPos + 2;
      uint8_t currencyY2Pos = currencyY1Pos + 20;
      String currencyEUR = String(RTC.Currencies.Eur.Value, 3U);
      String currencyUSD = String(RTC.Currencies.Usd.Value, 3U);
      currencyEUR = currencyEUR.substring(0, 4);
      currencyEUR.replace(".", ",");
      currencyUSD = currencyUSD.substring(0, 4);
      currencyUSD.replace(".", ",");    
      uint16_t currencyEURXPos = DrawString(currencyXPos, currencyY1Pos, String("Eur"), &UdvarhelySansBold9pt7b, BLACK, LEFT, UPPERCASE);
      uint16_t currencyUSDXPos = DrawString(currencyXPos, currencyY2Pos, String("Usd"), &UdvarhelySansBold9pt7b, BLACK, LEFT, UPPERCASE);
      currencyXPos = max(currencyEURXPos, currencyUSDXPos) + space;
      uint16_t currencyEURIndexXPos = DrawIndex(currencyXPos, (currencyY1Pos + 3), RTC.Currencies.Eur.Index, BLACK);
      uint16_t currencyUSDIndexXPos = DrawIndex(currencyXPos, (currencyY2Pos + 3), RTC.Currencies.Usd.Index, BLACK);
      currencyXPos = max(currencyEURIndexXPos, currencyUSDIndexXPos) + space;
      uint16_t currencyEURValXPos = DrawString(currencyXPos, (currencyY1Pos - 3), currencyEUR, &UdvarhelySansRegular9pt7b, BLACK, LEFT, NORMAL);
      uint16_t currencyUSDValXPos = DrawString(currencyXPos, (currencyY2Pos - 3), currencyUSD, &UdvarhelySansRegular9pt7b, BLACK, LEFT, NORMAL);
      // ----------------------------------------
      currencyXPos = max(currencyEURValXPos, currencyUSDValXPos) + 15;
      uint8_t currencyVLineY1 = displayYPos;
      uint8_t currencyVLineY2 = CFG.Display.Height - space;
      Display.drawLine(currencyXPos, currencyVLineY1, currencyXPos, currencyVLineY2, GxEPD_BLACK);
      currencyXPos = currencyXPos + 15;
      // ----------------------------------------
      String currencyHUF = String(RTC.Currencies.Huf.Value, 3U);
      String currencyBTC = String(RTC.Currencies.Btc.Value);
      currencyHUF = currencyHUF.substring(0, 4);
      currencyHUF.replace(".", ",");
      currencyBTC.replace(".", ",");
      uint16_t currencyHUFXPos = DrawString(currencyXPos, currencyY1Pos, String("Huf"), &UdvarhelySansBold9pt7b, BLACK, LEFT, UPPERCASE);
      uint16_t currencyBTCXPos = DrawString(currencyXPos, currencyY2Pos, String("Btc"), &UdvarhelySansBold9pt7b, BLACK, LEFT, UPPERCASE);
      currencyXPos = max(currencyHUFXPos, currencyBTCXPos) + space;
      uint16_t currencyHUFIndexXPos = DrawIndex(currencyXPos, (currencyY1Pos + 3), RTC.Currencies.Huf.Index, BLACK);
      uint16_t currencyBTCIndexXPos = DrawIndex(currencyXPos, (currencyY2Pos + 3), RTC.Currencies.Btc.Index, BLACK);
      currencyXPos = max(currencyHUFIndexXPos, currencyBTCIndexXPos) + space;
      DrawString(currencyXPos, (currencyY1Pos - 5), String(currencyHUF + "*"), &UdvarhelySansRegular9pt7b, BLACK, LEFT, NORMAL);
      DrawString(currencyXPos, (currencyY2Pos - 3), currencyBTC, &UdvarhelySansRegular9pt7b, BLACK, LEFT, NORMAL);
    };

    void UpdateEinkDisplay(bool state = true) {
      if (state) {
        if (GetData()) {
          DisconnectWiFi();
          dPrintln(SeparationLine);
          dPrintln("Display updating...");
          Display.setFullWindow();
          Display.firstPage();
          do {
            Display.fillScreen(GxEPD_WHITE);
            DrawData();
          } while (Display.nextPage());
          Display.powerOff();
          dPrintln("Display updated");
          dPrintln("Display turned off");
          RTC.LastSleepTime = RTC.NowTime;
          return;
        };
        dPrintln("Restarting device...");
        #if DEBUG
          delay(1000);
        #endif
        GoToDeepSleep(10);
      };
      dPrintln(SeparationLine);
      dPrintln("Display updating...");
      Display.setFullWindow();
      Display.firstPage();
      do {
        Display.fillScreen(GxEPD_WHITE);
        DrawEmptyBattery();
      } while (Display.nextPage());
      Display.powerOff();
      dPrintln("Display updated");
      dPrintln(SeparationLine);
      dPrintln("Display turned off");
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
      for (uint8_t i = 0; i < 3; i++) dPrintln();
      pinMode(SS, OUTPUT);
      pinMode(CFG.GPIO.Led, OUTPUT);
      digitalWrite(CFG.GPIO.Led, LOW);
      Display.init(CFG.BaudRate);
      Display.setRotation(3);
      BootCount++;
      if (MeasureBattery() == false) {
        DeviceInfo(false);
        UpdateEinkDisplay(false);
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
      if (wakeupReason) UpdateEinkDisplay(true);
      long executionTimeLimit = CFG.Scheduler.ExecutionTimeLimit * SecondsPerHour;
      long halfSecPeriodFactor = (CFG.Scheduler.Period * SecondsPerHour) / 2;
      long delta = ((RTC.NowTime - RTC.ScheduledDateTime) + halfSecPeriodFactor) * -1;
      executionTimeLimit = executionTimeLimit > halfSecPeriodFactor ? halfSecPeriodFactor : executionTimeLimit;
      unsigned long overTime = halfSecPeriodFactor - delta;
      dPrintln(SeparationLine);
      if (overTime <= executionTimeLimit) {
        PrintMessageIfFirstBootOrNot("after due time", wakeupReason);
        PrintEpochTime("Over time: ", overTime);
        if (RTC.LastSleepTime <= (RTC.ScheduledDateTime - SecondsPerDay)) UpdateEinkDisplay(true);
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