
  #include <Wire.h>
  #include <LiquidCrystal_I2C.h>
  #include <virtuabotixRTC.h>
  #include <EEPROM.h>
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <time.h>
  #include <ESP8266HTTPClient.h>
  #include <NTPClient.h>
  #include <WiFiUdp.h>

  // Hardware pin definitions
  #define RELAY_PIN 13 // D7 (GPIO 13) - Relay control for bell activation (active low)
  #define RTC_CLK 14   // D5 - DS1302 RTC clock pin
  #define RTC_DAT 12   // D6 - DS1302 RTC data pin
  #define RTC_RST 2    // D4 - DS1302 RTC reset pin

  // Hardware instances
  LiquidCrystal_I2C lcd(0x27, 20, 4);     // LCD 20x4 display via I2C
  virtuabotixRTC Rtc(RTC_CLK, RTC_DAT, RTC_RST); // DS1302 RTC module
  ESP8266WebServer server(80);            // Web server on port 80

  // LCD row definitions
  #define LCD_ROW0 0  // Top row
  #define LCD_ROW1 1  // Second row
  #define LCD_ROW2 2  // Third row
  #define LCD_ROW3 3  // Bottom row
// I2C pins for LCD (GPIO 4=SDA, GPIO 5=SCL on ESP8266)
#define D1 5
#define D2 4

  // NTP configuration
  const char* ntpServer = "pool.ntp.org";
  long utcOffsetInSeconds = 0; // Will be loaded from EEPROM

  WiFiUDP ntpUDP;
NTPClient* timeClient;

  // ===== USER CONFIGURATION =====
  // Set your WiFi access point (AP) credentials for initial configuration
  const char* apSSID = "";          // Your AP SSID
  const char* apPassword = "";      // Your AP password

  // Set your known WiFi networks (SSID/password pairs) that the device will attempt to connect to
  const char* wifiCredentials[][2] = {
    // {"YOUR_WIFI_SSID_1", "YOUR_WIFI_PASSWORD_1"},
    // {"YOUR_WIFI_SSID_2", "YOUR_WIFI_PASSWORD_2"},
    // Add more as needed
  };
  const int numNetworks = sizeof(wifiCredentials) / sizeof(wifiCredentials[0]);

  // Set your OpenWeatherMap location and API key (get free key at openweathermap.org)
  const char* weatherCity = "";     // Format: "City,CountryCode" (e.g., "New York,US")
  const char* weatherApiKey = "";   // Your OpenWeatherMap API key

  // Bell schedule definition for custom schedules
  struct BellSchedule {
    uint8_t hour;          // Hour (0-23)
    uint8_t minute;        // Minute (0-59)
    bool days[7];          // Days of week (0=Sun, 1=Mon, ..., 6=Sat)
  };

  // Fixed bell time definition for normal/Ramadan programs
  struct FixedBellTime {
    uint8_t hour;          // Hour (0-23)
    uint8_t minute;        // Minute (0-59)
    uint8_t daysMask;      // Bitmask for days (bit0=Sun, bit1=Mon, ..., bit6=Sat)
  };

  // Operating program modes
  enum ProgramMode {
    PROGRAM_NORMAL = 0,  // Standard weekday/weekend schedule
    PROGRAM_RAMADAN = 1, // Special schedule for Ramadan period
    PROGRAM_CUSTOM = 2   // User-defined custom schedule
  };

  // Global state variables
  bool wifiConnected = false;           // WiFi connection status
  int bellDuration = 5;                 // Bell ring duration in seconds (1-30)
  bool bellEnabled = false;             // Master enable/disable for bell
  ProgramMode selectedProgram = PROGRAM_CUSTOM; // Active program mode

  // Standby (sleep) period settings
  int standbyStartHour = 22, standbyStartMinute = 0; // Start time (22:00 = 10 PM)
  int standbyEndHour = 6, standbyEndMinute = 0;      // End time (06:00 = 6 AM)
  bool isInStandby = false;                  // Current standby status

  // NTP synchronization timing
  unsigned long lastSyncTime = 0;            // Last successful NTP sync
  const unsigned long syncInterval = 3600000UL; // 1 hour between syncs

  // LCD scrolling timing
  unsigned long lastScrollTime = 0;
  const unsigned long scrollInterval = 350UL; // Time between scroll updates (ms)

  // Temperature update timing
  unsigned long lastTempUpdate = 0;
  const unsigned long tempUpdateInterval = 300000UL; // 5 minutes between updates

  // Date tracking and custom message
  int lastDayOfMonth = -1;                  // Track last day to detect month changes
  String customMessage = "";                // Custom scrolling message
  bool animateMessage = false;              // Enable/disable message animation
  const char* greeting = "CPGE SALMANE ALFARISSI VOUS SOUHAITE LA BIENVENUE"; // Default greeting

  // Notification system variables
  String notification = "";                 // Current notification message
  unsigned long notificationStartTime = 0;  // When notification started
  bool showingNotification = false;         // Whether notification is active

  // LCD scrolling position tracking (for each row)
  uint8_t scrollPos[4] = {0, 0, 0, 0};
  uint32_t scrollLast[4] = {0, 0, 0, 0};

  // LCD recovery after relay activation
  bool lcdNeedsRecover = false;             // Flag to trigger LCD recovery
  unsigned long lcdRecoverAt = 0;           // When to perform recovery

  // Custom schedule storage
  #define MAX_SCHEDULES 20
  BellSchedule schedules[MAX_SCHEDULES];  // Array to store custom schedules
  int scheduleCount = 0;                  // Number of active custom schedules

  // Temperature sensor
  int currentTemperature = -127;          // Current temperature in Celsius (-127 = error)

  // Relay control state
  bool relayActive = false;               // Whether relay is currently activated
  unsigned long relayStartTime = 0;       // When relay was last activated

  // Bell ringing tracking (to prevent multiple rings per minute)
  int lastBellYear = -1;
  int lastBellMonth = -1;
  int lastBellDay = -1;
  int lastBellHour = -1;
  int lastBellMinute = -1;

  // Days in each month (for date validation)
  const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  // EEPROM configuration
  #define EEPROM_SIZE 512
  #define EEPROM_MAGIC_VALUE 0x42       // Header validation byte
  #define EEPROM_VERSION_VALUE 0x02     // Layout version

  // EEPROM address map
  #define EE_MAGIC 0                    // Magic value for validation
  #define EE_VERSION 1                  // Version number
  #define EE_DURATION 2                 // Bell duration (1-30 seconds)
  #define EE_BELL_ENABLED 3             // Bell enabled flag (0/1)
  #define EE_STANDBY_START_H 4          // Standby start hour
  #define EE_STANDBY_START_M 5          // Standby start minute
  #define EE_STANDBY_END_H 6            // Standby end hour
  #define EE_STANDBY_END_M 7            // Standby end minute
  #define EE_PROGRAM_MODE 8             // Program mode (0=NORMAL,1=RAMADAN,2=CUSTOM)
  #define EE_ANIMATE_MESSAGE 9          // Message animation flag (0/1)

  #define CUSTOM_MESSAGE_MAX 40
  #define EE_CUSTOM_MESSAGE_START 10    // Start address for custom message
  #define EE_CUSTOM_MESSAGE_LEN (CUSTOM_MESSAGE_MAX + 1) // Length incl. null terminator

  #define EE_SCHEDULE_COUNT (EE_CUSTOM_MESSAGE_START + EE_CUSTOM_MESSAGE_LEN) // Schedule count
  #define EE_SCHEDULE_START (EE_SCHEDULE_COUNT + 1)      // Start of schedule array
  #define EE_BYTES_PER_SCHEDULE 3       // Bytes per schedule (hour, minute, days mask)
#define EE_UTC_OFFSET 112

  // Old layout for migration
  #define OLD_EEPROM_DURATION 0
  #define OLD_EEPROM_BELL_STATE 1
  #define OLD_EEPROM_STANDBY_START_H 2
  #define OLD_EEPROM_STANDBY_START_M 3
  #define OLD_EEPROM_STANDBY_END_H 4
  #define OLD_EEPROM_STANDBY_END_M 5
  #define OLD_EEPROM_SCHEDULE_COUNT 13
  #define OLD_EEPROM_SCHEDULE_START 14

  // Day bit masks for schedule days (bit 0 = Sunday, bit 6 = Saturday)
  const uint8_t DAY_SUN = (1 << 0);
  const uint8_t DAY_MON = (1 << 1);
  const uint8_t DAY_TUE = (1 << 2);
  const uint8_t DAY_WED = (1 << 3);
  const uint8_t DAY_THU = (1 << 4);
  const uint8_t DAY_FRI = (1 << 5);
  const uint8_t DAY_SAT = (1 << 6);

  // Weekday mask (Monday through Friday)
  const uint8_t DAYS_MON_FRI = DAY_MON | DAY_TUE | DAY_WED | DAY_THU | DAY_FRI;

  // Normal program fixed bell times (weekdays and Saturday)
  const FixedBellTime normalProgram[] = {
    {8,  15, DAYS_MON_FRI | DAY_SAT},  // 08:15
    {8,  25, DAYS_MON_FRI | DAY_SAT},  // 08:25
    {10, 25, DAYS_MON_FRI | DAY_SAT},  // 10:25
    {10, 35, DAYS_MON_FRI | DAY_SAT},  // 10:35
    {12, 30, DAYS_MON_FRI | DAY_SAT},  // 12:30
    {14, 15, DAYS_MON_FRI},            // 14:15 (Mon-Fri only)
    {14, 25, DAYS_MON_FRI},            // 14:25 (Mon-Fri only)
    {16, 25, DAYS_MON_FRI},            // 16:25 (Mon-Fri only)
    {16, 35, DAYS_MON_FRI},            // 16:35 (Mon-Fri only)
    {18, 30, DAYS_MON_FRI}             // 18:30 (Mon-Fri only)
  };
  const int normalProgramCount = sizeof(normalProgram) / sizeof(normalProgram[0]);

  // Ramadan program fixed bell times (weekdays and Saturday)
  const FixedBellTime ramadanProgram[] = {
    {8,  15, DAYS_MON_FRI | DAY_SAT},  // 08:15
    {8,  25, DAYS_MON_FRI | DAY_SAT},  // 08:25
    {10, 25, DAYS_MON_FRI | DAY_SAT},  // 10:25
    {10, 35, DAYS_MON_FRI | DAY_SAT},  // 10:35
    {12, 30, DAYS_MON_FRI | DAY_SAT},  // 12:30
    {12, 45, DAYS_MON_FRI},            // 12:45 (Mon-Fri only)
    {13,  0, DAYS_MON_FRI},            // 13:00 (Mon-Fri only)
    {14, 55, DAYS_MON_FRI},            // 14:55 (Mon-Fri only)
    {15,  5, DAYS_MON_FRI},            // 15:05 (Mon-Fri only)
    {17,  0, DAYS_MON_FRI}             // 17:00 (Mon-Fri only)
  };
  const int ramadanProgramCount = sizeof(ramadanProgram) / sizeof(ramadanProgram[0]);

  // WiFi connection task state machine
  enum WiFiTaskState {
    WIFI_TASK_IDLE,             // Not attempting to connect
    WIFI_TASK_CONNECTING        // Currently attempting to connect to a network
  };

  // WiFi connection management variables
  WiFiTaskState wifiTaskState = WIFI_TASK_IDLE;
  int wifiNetworkCursor = -1;                   // Current network index being tried
  unsigned long wifiAttemptStart = 0;           // Start time of current attempt
  unsigned long wifiRetryAt = 0;                // When to retry next network
  const unsigned long wifiAttemptTimeout = 8000UL; // Timeout per network attempt (8 sec)
  const unsigned long wifiFullRetryDelay = 60000UL; // Delay after all networks fail (1 min)
  const unsigned long wifiSwitchDelay = 250UL;  // Delay between network attempts (250 ms)

  // Check if a year is a leap year
  bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  }

  // Validate and correct date values if out of range
  void validateDate(int &year, int &month, int &day) {
    int maxDays = daysInMonth[month - 1];
    if (month == 2 && isLeapYear(year)) maxDays = 29;
    if (day > maxDays) day = maxDays;
    if (day < 1) day = 1;
    if (month > 12) month = 12;
    if (month < 1) month = 1;
  }

  // Calculate day of week (1=Sunday, 2=Monday, ..., 7=Saturday)
  // Uses Zeller's congruence algorithm
  int calculateDayOfWeek(int year, int month, int day) {
    if (month < 3) {
      month += 12;
      year--;
    }
    int k = day;
    int m = month;
    int D = year % 100;
    int C = year / 100;
    int f = k + ((13 * (m + 1)) / 5) + D + (D / 4) + (C / 4) - (2 * C);
    int base_dow = ((f % 7) + 7) % 7;  // 0=Sat, 1=Sun, 2=Mon, ..., 6=Fri
    if (base_dow == 0) {
      return 6;  // Saturday
    } else if (base_dow == 1) {
      return 7;  // Sunday
    } else {
      return base_dow - 1;  // Mon(2)->1, Tue(3)->2, ..., Fri(6)->5
    }
  }

  // Forward declarations for display functions used in serviceLCDRecover
  void displayDateTime();
  void displayGreetings();
  void displayCustomMessage();

  // Get program name as string for display
  const char* getProgramName(ProgramMode mode) {
    switch (mode) {
      case PROGRAM_NORMAL: return "NORMAL";
      case PROGRAM_RAMADAN: return "RAMADAN";
      default: return "CUSTOM";
    }
  }

  // Convert boolean days array to bitmask
  uint8_t daysToMask(const bool days[7]) {
    uint8_t mask = 0;
    for (int i = 0; i < 7; i++) {
      if (days[i]) mask |= (1 << i);
    }
    return mask;
  }

  // Convert bitmask to boolean days array
  void maskToDays(uint8_t mask, bool days[7]) {
    for (int i = 0; i < 7; i++) {
      days[i] = (mask & (1 << i)) != 0;
    }
  }

  // Escape HTML special characters for safe web display
  String htmlEscape(const String &input) {
    String out;
    out.reserve(input.length() * 2);

    for (unsigned int i = 0; i < input.length(); i++) {
      char c = input[i];
      switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out += c; break;
      }
    }
    return out;
  }

  // Check if the bell has already rung this minute (to prevent duplicates)
  bool isSameBellMinuteNow() {
    return lastBellYear == Rtc.year &&
          lastBellMonth == Rtc.month &&
          lastBellDay == Rtc.dayofmonth &&
          lastBellHour == Rtc.hours &&
          lastBellMinute == Rtc.minutes;
  }

  // Mark the current time as the last bell rung time
  void markBellMinuteNow() {
    lastBellYear = Rtc.year;
    lastBellMonth = Rtc.month;
    lastBellDay = Rtc.dayofmonth;
    lastBellHour = Rtc.hours;
    lastBellMinute = Rtc.minutes;
  }

  // Synchronize time with NTP server
  bool syncTimeWithNTP() {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected. Skipping NTP sync.");
      return false;
    }

    timeClient->begin();
    if (!timeClient->update()) {
      Serial.println("NTP update failed.");
      timeClient->end();
      return false;
    }

    time_t now = timeClient->getEpochTime();
    struct tm* timeinfo = localtime(&now);
    int calculatedDow = calculateDayOfWeek(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);

    Rtc.setDS1302Time(timeinfo->tm_sec, timeinfo->tm_min, timeinfo->tm_hour,
                      calculatedDow, timeinfo->tm_mday, timeinfo->tm_mon + 1,
                      timeinfo->tm_year + 1900);

    Serial.printf("NTP Time: %04d-%02d-%02d %02d:%02d:%02d, DOW: %d\n",
                  timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                  timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, calculatedDow);

    timeClient->end();
    return true;
  }

  // Fetch current temperature from OpenWeatherMap API
  int getTemperature() {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected for temperature fetch.");
      return -127;
    }

    WiFiClient client;
    HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(weatherCity) +
                "&appid=" + String(weatherApiKey) + "&units=metric";
    Serial.println("Temperature URL: " + url);

    http.setTimeout(2000);
    if (!http.begin(client, url)) {
      Serial.println("HTTP begin failed.");
      http.end();
      return -127;
    }

    ESP.wdtFeed();
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      ESP.wdtFeed();

      if (payload.length() > 1000) {
        payload = payload.substring(0, 1000);
      }

      int tempStart = payload.indexOf("\"temp\":") + 7;
      if (tempStart > 7) {
        int tempEnd = payload.indexOf(",", tempStart);
        if (tempEnd == -1) tempEnd = payload.indexOf("}", tempStart);
        String tempStr = payload.substring(tempStart, tempEnd);
        http.end();
        return tempStr.toInt();
      }
    }

    http.end();
    return -127;
  }

  // Recover I2C bus after potential lockup (common with LCD and I2C devices)
  void recoverI2CBus() {
    pinMode(D1, INPUT_PULLUP); // SCL
    pinMode(D2, INPUT_PULLUP); // SDA
    delay(2);

    if (digitalRead(D2) == LOW) {
      pinMode(D1, OUTPUT);
      digitalWrite(D1, HIGH);
      delayMicroseconds(10);

      for (uint8_t i = 0; i < 16 && digitalRead(D2) == LOW; i++) {
        digitalWrite(D1, LOW);
        delayMicroseconds(10);
        digitalWrite(D1, HIGH);
        delayMicroseconds(10);
        yield();
      }
    }

    pinMode(D1, INPUT_PULLUP);
    pinMode(D2, INPUT_PULLUP);
    delay(2);
  }

  // Initialize LCD safely with I2C bus recovery
  void initLCDSafe() {
    recoverI2CBus();

    Wire.begin(D2, D1);
    Wire.setClock(100000);
    delay(5);

    lcd.init();
    delay(20);
    lcd.backlight();
    lcd.display();

    byte degreeSymbol[8] = {
      B00110, B01001, B01001, B00110,
      B00000, B00000, B00000, B00000
    };
    lcd.createChar(0, degreeSymbol);
    lcd.clear();
    delay(5);

    for (uint8_t i = 0; i < 4; i++) {
      scrollPos[i] = 0;
      scrollLast[i] = 0;
    }
  }

  // Request LCD recovery after a delay (used after relay activation)
  void requestLCDRecover(unsigned long delayMs) {
    lcdNeedsRecover = true;
    lcdRecoverAt = millis() + delayMs;
  }

  // Service LCD recovery if needed (called periodically in loop)
  void serviceLCDRecover() {
    if (!isInStandby && lcdNeedsRecover && (long)(millis() - lcdRecoverAt) >= 0) {
      Serial.println("Recovering LCD after relay event...");
      initLCDSafe();
      displayDateTime();
      displayGreetings();
      displayCustomMessage();
      lcdNeedsRecover = false;
    }
  }

  // Save all settings to EEPROM
  void saveSettingsToEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    EEPROM.write(EE_MAGIC, EEPROM_MAGIC_VALUE);
    EEPROM.write(EE_VERSION, EEPROM_VERSION_VALUE);
    EEPROM.write(EE_DURATION, constrain(bellDuration, 1, 30));
    EEPROM.write(EE_BELL_ENABLED, bellEnabled ? 1 : 0);
    EEPROM.write(EE_STANDBY_START_H, constrain(standbyStartHour, 0, 23));
    EEPROM.write(EE_STANDBY_START_M, constrain(standbyStartMinute, 0, 59));
    EEPROM.write(EE_STANDBY_END_H, constrain(standbyEndHour, 0, 23));
    EEPROM.write(EE_STANDBY_END_M, constrain(standbyEndMinute, 0, 59));
    EEPROM.write(EE_PROGRAM_MODE, (uint8_t)selectedProgram);
    EEPROM.write(EE_ANIMATE_MESSAGE, animateMessage ? 1 : 0);

    char msgBuf[CUSTOM_MESSAGE_MAX + 1] = {0};
    customMessage.substring(0, CUSTOM_MESSAGE_MAX).toCharArray(msgBuf, sizeof(msgBuf));
    for (int i = 0; i < EE_CUSTOM_MESSAGE_LEN; i++) {
      EEPROM.write(EE_CUSTOM_MESSAGE_START + i, (uint8_t)msgBuf[i]);
    }

    EEPROM.write(EE_SCHEDULE_COUNT, (uint8_t)constrain(scheduleCount, 0, MAX_SCHEDULES));
    for (int i = 0; i < MAX_SCHEDULES; i++) {
      int base = EE_SCHEDULE_START + i * EE_BYTES_PER_SCHEDULE;
      if (i < scheduleCount) {
        EEPROM.write(base, constrain((int)schedules[i].hour, 0, 23));
        EEPROM.write(base + 1, constrain((int)schedules[i].minute, 0, 59));
        EEPROM.write(base + 2, daysToMask(schedules[i].days));
      } else {
        EEPROM.write(base, 0);
        EEPROM.write(base + 1, 0);
        EEPROM.write(base + 2, 0);
      }
    }

    // Save UTC offset
    for (int i = 0; i < 4; i++) {
      EEPROM.write(EE_UTC_OFFSET + i, (uint8_t)(utcOffsetInSeconds >> (i * 8)));
    }
    EEPROM.commit();
    EEPROM.end();
  }

  // Migrate from old EEPROM layout to current version
  void migrateOldEEPROMLayout() {
    EEPROM.begin(EEPROM_SIZE);

    bellDuration = EEPROM.read(OLD_EEPROM_DURATION);
    if (bellDuration < 1 || bellDuration > 30) bellDuration = 5;

    bellEnabled = EEPROM.read(OLD_EEPROM_BELL_STATE) != 0;
    standbyStartHour = EEPROM.read(OLD_EEPROM_STANDBY_START_H);
    standbyStartMinute = EEPROM.read(OLD_EEPROM_STANDBY_START_M);
    standbyEndHour = EEPROM.read(OLD_EEPROM_STANDBY_END_H);
    standbyEndMinute = EEPROM.read(OLD_EEPROM_STANDBY_END_M);

    if (standbyStartHour > 23) standbyStartHour = 22;
    if (standbyStartMinute > 59) standbyStartMinute = 0;
    if (standbyEndHour > 23) standbyEndHour = 6;
    if (standbyEndMinute > 59) standbyEndMinute = 0;

    scheduleCount = EEPROM.read(OLD_EEPROM_SCHEDULE_COUNT);
    if (scheduleCount < 0 || scheduleCount > MAX_SCHEDULES) scheduleCount = 0;

    for (int i = 0; i < scheduleCount; i++) {
      int base = OLD_EEPROM_SCHEDULE_START + i * 9;
      schedules[i].hour = EEPROM.read(base);
      schedules[i].minute = EEPROM.read(base + 1);
      for (int j = 0; j < 7; j++) {
        schedules[i].days[j] = EEPROM.read(base + 2 + j) != 0;
      }
      if (schedules[i].hour > 23) schedules[i].hour = 0;
    }
    utcOffsetInSeconds = 0; // Default to GMT for migrated layouts

    customMessage = "";
    animateMessage = false;
    selectedProgram = PROGRAM_CUSTOM; // keep old saved schedules active after update

    EEPROM.end();
    saveSettingsToEEPROM();
  }

  // Load all settings from EEPROM
  void loadSettingsFromEEPROM() {
    EEPROM.begin(EEPROM_SIZE);

    bool validHeader =
      EEPROM.read(EE_MAGIC) == EEPROM_MAGIC_VALUE &&
      EEPROM.read(EE_VERSION) == EEPROM_VERSION_VALUE;

    if (!validHeader) {
      EEPROM.end();
      migrateOldEEPROMLayout();
      return;
    }

    bellDuration = EEPROM.read(EE_DURATION);
    if (bellDuration < 1 || bellDuration > 30) bellDuration = 5;

    bellEnabled = EEPROM.read(EE_BELL_ENABLED) != 0;
    standbyStartHour = EEPROM.read(EE_STANDBY_START_H);
    standbyStartMinute = EEPROM.read(EE_STANDBY_START_M);
    standbyEndHour = EEPROM.read(EE_STANDBY_END_H);
    standbyEndMinute = EEPROM.read(EE_STANDBY_END_M);

    if (standbyStartHour > 23) standbyStartHour = 22;
    if (standbyStartMinute > 59) standbyStartMinute = 0;
    if (standbyEndHour > 23) standbyEndHour = 6;
    if (standbyEndMinute > 59) standbyEndMinute = 0;

    uint8_t modeByte = EEPROM.read(EE_PROGRAM_MODE);
    if (modeByte > PROGRAM_CUSTOM) modeByte = PROGRAM_CUSTOM;
    selectedProgram = (ProgramMode)modeByte;

    animateMessage = EEPROM.read(EE_ANIMATE_MESSAGE) != 0;

    char msgBuf[CUSTOM_MESSAGE_MAX + 1];
    for (int i = 0; i < EE_CUSTOM_MESSAGE_LEN; i++) {
      msgBuf[i] = (char)EEPROM.read(EE_CUSTOM_MESSAGE_START + i);
    }
    msgBuf[CUSTOM_MESSAGE_MAX] = '\0';
    customMessage = String(msgBuf);
    if (customMessage.length() > CUSTOM_MESSAGE_MAX) {
      customMessage = customMessage.substring(0, CUSTOM_MESSAGE_MAX);
    }

    scheduleCount = EEPROM.read(EE_SCHEDULE_COUNT);
    if (scheduleCount < 0 || scheduleCount > MAX_SCHEDULES) scheduleCount = 0;

    for (int i = 0; i < scheduleCount; i++) {
      int base = EE_SCHEDULE_START + i * EE_BYTES_PER_SCHEDULE;
      schedules[i].hour = EEPROM.read(base);
      schedules[i].minute = EEPROM.read(base + 1);
      uint8_t mask = EEPROM.read(base + 2);

      if (schedules[i].hour > 23) schedules[i].hour = 0;
      if (schedules[i].minute > 59) schedules[i].minute = 0;
      maskToDays(mask, schedules[i].days);
    }

    // Load UTC offset
    long offsetRaw = 0;
    for (int i = 0; i < 4; i++) {
      offsetRaw |= ((long)EEPROM.read(EE_UTC_OFFSET + i)) << (i * 8);
    }
    utcOffsetInSeconds = offsetRaw;
    // Validate offset (-12 to +14 hours in seconds)
    if (utcOffsetInSeconds < -12 * 3600 || utcOffsetInSeconds > 14 * 3600) {
      utcOffsetInSeconds = 0; // Default to GMT if invalid
    }

    EEPROM.end();
  }

  // Show a temporary notification on the LCD
  void showNotification(const String &msg) {
    notification = msg.substring(0, 20);
    notificationStartTime = millis();
    showingNotification = true;
  }

  // Check and update standby (sleep) mode based on current time
  void checkStandby() {
    Rtc.updateTime();

    // If RTC time is invalid, exit standby mode
    if (Rtc.year < 2025 || Rtc.year > 2030) {
      if (isInStandby) {
        isInStandby = false;
        initLCDSafe();
        displayDateTime();
        displayGreetings();
        displayCustomMessage();
      }
      return;
    }

    int currentHour = Rtc.hours;
    int currentMinute = Rtc.minutes;
    int currentTotal = currentHour * 60 + currentMinute;
    int startTotal = standbyStartHour * 60 + standbyStartMinute;
    int endTotal = standbyEndHour * 60 + standbyEndMinute;

    // Determine if we should be in standby mode
    // Handles case where standby period crosses midnight
    bool shouldBeInStandby = (standbyStartHour > standbyEndHour) ?
      (currentTotal >= startTotal || currentTotal < endTotal) :
      (currentTotal >= startTotal && currentTotal < endTotal);

    // Enter standby mode
    if (shouldBeInStandby && !isInStandby) {
      isInStandby = true;
      lcdNeedsRecover = false;
      lcd.noBacklight();
      lcd.clear();
    }
    // Exit standby mode
    else if (!shouldBeInStandby && isInStandby) {
      isInStandby = false;
      initLCDSafe();
      displayDateTime();
      displayGreetings();
      displayCustomMessage();
    }
  }

  // Begin attempting to connect to a specific WiFi network
  void beginWiFiAttempt(int idx) {
    if (idx < 0 || idx >= numNetworks) return;

    Serial.printf("Trying WiFi network %d: %s\n", idx, wifiCredentials[idx][0]);
    WiFi.begin(wifiCredentials[idx][0], wifiCredentials[idx][1]);
    wifiNetworkCursor = idx;
    wifiAttemptStart = millis();
    wifiTaskState = WIFI_TASK_CONNECTING;
  }

  // Handle WiFi connection state machine and reconnection logic
  void serviceWiFi() {
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
      if (!wifiConnected) {
        wifiConnected = true;
        wifiTaskState = WIFI_TASK_IDLE;
        Serial.print("WiFi connected. IP: ");
        Serial.println(WiFi.localIP());
        showNotification("WIFI CONNECTED!");
        lastSyncTime = 0;
        lastTempUpdate = 0;
      }
      return;
    }

    if (wifiConnected) {
      wifiConnected = false;
      Serial.println("WiFi connection lost.");
      showNotification("WIFI LOST!");
      wifiTaskState = WIFI_TASK_IDLE;
      wifiRetryAt = millis() + 3000UL;
    }

    if (numNetworks <= 0) return;

    // Don't process WiFi if softAP has clients and we're not connecting
    if (WiFi.softAPgetStationNum() > 0 && wifiTaskState != WIFI_TASK_CONNECTING) {
      return;
    }

    if (wifiTaskState == WIFI_TASK_CONNECTING) {
      if (status == WL_CONNECTED) return;

      if (millis() - wifiAttemptStart >= wifiAttemptTimeout) {
        Serial.printf("WiFi timeout on SSID: %s\n", wifiCredentials[wifiNetworkCursor][0]);
        WiFi.disconnect();

        if (wifiNetworkCursor + 1 < numNetworks) {
          wifiTaskState = WIFI_TASK_IDLE;
          wifiRetryAt = millis() + wifiSwitchDelay;
        } else {
          wifiNetworkCursor = -1;
          wifiTaskState = WIFI_TASK_IDLE;
          wifiRetryAt = millis() + wifiFullRetryDelay;
          Serial.println("All WiFi networks failed. Bell stays running offline.");
        }
      }
      return;
    }

    if ((long)(millis() - wifiRetryAt) < 0) return;

    int nextIndex = wifiNetworkCursor + 1;
    if (nextIndex < 0 || nextIndex >= numNetworks) nextIndex = 0;
    beginWiFiAttempt(nextIndex);
  }

  // Handle periodic network tasks: NTP sync and temperature updates
  void serviceNetworkTasks() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (relayActive) return;
    if (isInStandby) return;

    Rtc.updateTime();

    // Only process during stable seconds to reduce serial output
    if (Rtc.seconds < 5 || Rtc.seconds > 50) return;

    // Check if it's time for NTP synchronization
    if (Rtc.year < 2025 || Rtc.year > 2030 || lastSyncTime == 0 || millis() - lastSyncTime >= syncInterval) {
      bool ok = syncTimeWithNTP();
      lastSyncTime = millis();
      if (ok) {
        lastDayOfMonth = Rtc.dayofmonth;
        Serial.println("Background NTP sync OK.");
      } else {
        Serial.println("Background NTP sync failed.");
      }
      return;
    }

    // Check if it's time for temperature update
    if (lastTempUpdate == 0 || millis() - lastTempUpdate >= tempUpdateInterval) {
      int t = getTemperature();
      lastTempUpdate = millis();
      if (t != -127) {
        currentTemperature = t;
        Serial.printf("Temperature updated: %d C\n", currentTemperature);
      } else {
        Serial.println("Temperature update failed.");
      }
    }
  }

  // Check if current time matches any entry in a fixed bell time table
  bool fixedProgramMatches(const FixedBellTime *table, int count, int currentDayOfWeek, int currentHour, int currentMinute) {
    uint8_t dayBit = (1 << currentDayOfWeek);
    for (int i = 0; i < count; i++) {
      if ((table[i].daysMask & dayBit) &&      // Check if day is enabled
          table[i].hour == currentHour &&      // Check hour match
          table[i].minute == currentMinute) {  // Check minute match
        return true;
      }
    }
    return false;
  }

  // Check if current time matches any custom schedule
  bool customProgramMatches(int currentDayOfWeek, int currentHour, int currentMinute) {
    for (int i = 0; i < scheduleCount; i++) {
      if (schedules[i].days[currentDayOfWeek] &&  // Check if day is enabled
          schedules[i].hour == currentHour &&      // Check hour match
          schedules[i].minute == currentMinute) {  // Check minute match
        return true;
      }
    }
    return false;
  }

  // Check if current time matches the active program's schedule
  bool currentProgramMatches(int currentDayOfWeek, int currentHour, int currentMinute) {
    switch (selectedProgram) {
      case PROGRAM_NORMAL:
        return fixedProgramMatches(normalProgram, normalProgramCount, currentDayOfWeek, currentHour, currentMinute);
      case PROGRAM_RAMADAN:
        return fixedProgramMatches(ramadanProgram, ramadanProgramCount, currentDayOfWeek, currentHour, currentMinute);
      default:
        return customProgramMatches(currentDayOfWeek, currentHour, currentMinute);
    }
  }

  // Generate HTML preview of a fixed bell time table for web display
  String buildFixedProgramPreview(const FixedBellTime *table, int count) {
    const char* dn[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    String html = "<div style='margin:8px 0 0 0;padding:10px;background:#f9f9f9;border:1px solid #eee;border-radius:6px'><strong>Code table preview</strong><br>";
    for (int i = 0; i < count; i++) {
      html += (table[i].hour < 10 ? "0" : "") + String(table[i].hour) + ":";
      html += (table[i].minute < 10 ? "0" : "") + String(table[i].minute) + " - ";
      String d = "";
      for (int j = 0; j < 7; j++) {
        if (table[i].daysMask & (1 << j)) d += String(dn[j]) + " ";  // Add day if enabled
      }
      if (d.length()) d.remove(d.length() - 1);  // Remove trailing space
      html += d + "<br>";
    }
    html += "</div>";
    return html;
  }

  // Handle root web page request - displays main configuration interface
  void handleRoot() {
    Rtc.updateTime();

    char dateBuf[11];
    char timeBuf[6];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", Rtc.year, Rtc.month, Rtc.dayofmonth);
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", Rtc.hours, Rtc.minutes);

    String checkedNormal = (selectedProgram == PROGRAM_NORMAL) ? " checked" : "";
    String checkedRamadan = (selectedProgram == PROGRAM_RAMADAN) ? " checked" : "";
    String checkedCustom = (selectedProgram == PROGRAM_CUSTOM) ? " checked" : "";

    String html =
      "<!DOCTYPE html><html><head>"
      "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>CPGE Bell</title>"
      "<style>"
      "body{font-family:Arial;background:#f4f4f4;margin:0;padding:20px;color:#333}"
      ".container{max-width:900px;margin:auto}"
      ".section{background:#fff;padding:20px;margin:10px 0;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,.1)}"
      "h1,h2{color:#2980b9;margin-top:0}"
      "label{font-weight:bold}"
      "input,select{padding:6px;border:1px solid #ddd;border-radius:4px}"
      "input[type=submit]{background:#3498db;color:#fff;border:none;padding:8px 14px;cursor:pointer;margin:5px 0}"
      "input[type=submit]:hover{background:#2980b9}"
      ".schItem{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #eee}"
      ".schItem:last-child{border-bottom:none}"
      "#toast{position:fixed;top:10%;left:50%;transform:translateX(-50%);background:#333;color:#fff;padding:8px 16px;border-radius:4px;display:none;z-index:1000}"
      ".button-group{display:flex;gap:10px;flex-wrap:wrap;}"
      ".progWrap{display:flex;gap:14px;flex-wrap:wrap;margin:8px 0}"
      ".hint{font-size:13px;color:#666;margin-top:8px}"
      "</style></head><body>"
      "<div id='toast'></div>"
      "<div class='container'><h1>CPGE Bell Configuration</h1>"
      "<div class='section'><h2>Date / Time</h2>"
      "<form action='/saveDateTime' method='post' onsubmit='return send(this)'>"
      "<label>Date: <input type='date' name='date' required value='" + String(dateBuf) + "'></label><br>"
      "<label>Time: <input type='time' name='time' required value='" + String(timeBuf) + "'></label><br>"
      "<label>DOW (1-7): <input type='number' name='dow' min='1' max='7' required value='" + String(Rtc.dayofweek) + "'></label><br>"
      "<input type='submit' value='Save'></form>"
      "<div class='button-group'>"
      "<form action='/syncNTP' method='post' onsubmit='return send(this)'>"
      "<input type='submit' value='Sync NTP'></form>"
      "<form action='/syncMobile' method='post' onsubmit='return send(this)'>"
      "<input type='hidden' name='mobileTime' id='mTime'>"
      "<input type='submit' value='Sync Mobile' onclick='let now = new Date(); let year = now.getFullYear(); let month = String(now.getMonth() + 1).padStart(2, \"0\"); let day = String(now.getDate()).padStart(2, \"0\"); let hour = String(now.getHours()).padStart(2, \"0\"); let minute = String(now.getMinutes()).padStart(2, \"0\"); document.getElementById(\"mTime\").value = year + \"-\" + month + \"-\" + day + \"T\" + hour + \":\" + minute;'>"
      "</form></div></div>"

      "<div class='section'><h2>Bell Settings</h2>"
      "<form action='/saveBellSettings' method='post' onsubmit='return send(this)'>"
      "<label>Duration (1-30 s): <input type='number' name='duration' min='1' max='30' required value='" + String(bellDuration) + "'></label><br>"
      "<label>PROGRAMME:</label>"
      "<div class='progWrap'>"
      "<label><input type='checkbox' class='progBox' name='program_normal'" + checkedNormal + " onclick='onlyOneProgram(this)'> NORMAL</label>"
      "<label><input type='checkbox' class='progBox' name='program_ramadan'" + checkedRamadan + " onclick='onlyOneProgram(this)'> RAMADAN</label>"
      "<label><input type='checkbox' class='progBox' name='program_custom'" + checkedCustom + " onclick='onlyOneProgram(this)'> CUSTOM</label>"
      "</div>"
      "<div class='hint'>Current programme: <strong>" + String(getProgramName(selectedProgram)) + "</strong></div>"
      "<label>Enabled: <input type='checkbox' name='enabled'" + String(bellEnabled ? " checked" : "") + "></label><br>"
      "<input type='submit' value='Save'></form>";

    if (selectedProgram == PROGRAM_NORMAL) {
      html += buildFixedProgramPreview(normalProgram, normalProgramCount);
    } else if (selectedProgram == PROGRAM_RAMADAN) {
      html += buildFixedProgramPreview(ramadanProgram, ramadanProgramCount);
    } else {
      html += "<div class='hint'>CUSTOM uses the schedules stored below.</div>";
    }

    html += "</div>";

    html += "<div class='section'><h2>Standby Period</h2>"
            "<form action='/saveStandby' method='post' onsubmit='return send(this)'>"
            "<label>Sleep: <input type='time' name='sleep' required value='" +
            String(standbyStartHour < 10 ? "0" : "") + String(standbyStartHour) + ":" +
            String(standbyStartMinute < 10 ? "0" : "") + String(standbyStartMinute) + "'></label><br>"
            "<label>Wake: <input type='time' name='wake' required value='" +
            String(standbyEndHour < 10 ? "0" : "") + String(standbyEndHour) + ":" +
            String(standbyEndMinute < 10 ? "0" : "") + String(standbyEndMinute) + "'></label><br>"
            "<input type='submit' value='Save'></form></div>";

    html += "<div class='section'><h2>Custom Schedules</h2>"
            "<div class='hint'>These schedules are always stored, but they ring only when <strong>CUSTOM</strong> is checked in PROGRAMME.</div>";

    if (scheduleCount == 0) {
      html += "<p>No custom schedules configured.</p>";
    } else {
      const char* dn[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      for (int i = 0; i < scheduleCount; i++) {
        html += "<div class='schItem'>";
        html += String(schedules[i].hour < 10 ? "0" : "") + String(schedules[i].hour) + ":" +
                String(schedules[i].minute < 10 ? "0" : "") + String(schedules[i].minute) + " - ";
        String d;
        for (int j = 0; j < 7; j++) {
          if (schedules[i].days[j]) d += String(dn[j]) + " ";
        }
        if (d.length()) d.remove(d.length() - 1);
        html += d + "<span style='float:right'>"
                    "<form action='/editSchedule' method='get' style='display:inline'>"
                    "<input type='hidden' name='index' value='" + String(i) + "'>"
                    "<input type='submit' value='Edit'></form>"
                    "<form action='/removeSchedule' method='post' style='display:inline' onsubmit='return send(this)'>"
                    "<input type='hidden' name='index' value='" + String(i) + "'>"
                    "<input type='submit' value='Delete'></form></span></div>";
      }
    }

    html += "<form action='/addSchedule' method='post' onsubmit='return send(this)'>"
            "<label>Time: <input type='time' name='time' required></label><br>"
            "<label>Days: "
            "<input type='checkbox' name='sun'>Sun "
            "<input type='checkbox' name='mon'>Mon "
            "<input type='checkbox' name='tue'>Tue "
            "<input type='checkbox' name='wed'>Wed "
            "<input type='checkbox' name='thu'>Thu "
            "<input type='checkbox' name='fri'>Fri "
            "<input type='checkbox' name='sat'>Sat "
            "</label><br><input type='submit' value='Add Custom Schedule'></form></div>";

    html += "<div class='section'><h2>Custom Message</h2>"
            "<form action='/saveCustomMessage' method='post' onsubmit='return send(this)'>"
            "<label>Message: <input type='text' name='message' maxlength='40' value='" + customMessage + "'></label><br>"
            "<label>Animate: <input type='checkbox' name='animate'" + String(animateMessage ? " checked" : "") + "></label><br>"
            "<input type='submit' value='Save'></form></div>";
    html += "<div class='section'><h2>UTC Offset</h2>"
            "<form action='/saveUTCOffset' method='post' onsubmit='return send(this)'>"
            "<label>UTC Offset (hours): <input type='number' name='offset' min='-12' max='14' step='0.5' required value='" + String(utcOffsetInSeconds / 3600.0) + "'></label><br>"
            "<small>Offset from GMT (e.g., -5 for EST, +1 for CET)</small><br>"
            "<input type='submit' value='Save'></form></div>";

    html += "<script>"
            "function send(form){"
            "  event.preventDefault();"
            "  fetch(form.action,{method:form.method,body:new FormData(form)})"
            "  .then(r=>r.text())"
            "  .then(t=>{"
            "     const msg=document.getElementById('toast');"
            "     msg.textContent=t;msg.style.display='block';"
            "     setTimeout(()=>{msg.style.display='none';location.reload();},800);"
            "  });"
            "  return false;"
            "}"
            "function onlyOneProgram(clicked){"
            "  const boxes=document.querySelectorAll('.progBox');"
            "  boxes.forEach(b=>{ if(b!==clicked) b.checked=false; });"
            "  if(!clicked.checked) clicked.checked=true;"
            "}"
            "</script>";

    html += "</div></body></html>";

    server.send(200, "text/html", html);
  }

  // Handle saving custom message from web form
  void handleSaveCustomMessage() {
    customMessage = server.arg("message");
    if (customMessage.length() > CUSTOM_MESSAGE_MAX) {
      customMessage = customMessage.substring(0, CUSTOM_MESSAGE_MAX);
    }
    animateMessage = server.arg("animate") == "on";
    saveSettingsToEEPROM();
    showNotification("MESSAGE SAVED!");
    server.send(200, "text/plain", "Message saved");
  }
  // Handle saving UTC offset from web form
  void handleSaveUTCOffset() {
    String offsetStr = server.arg("offset");
    float offsetHours = offsetStr.toFloat();
    // Validate offset range
    if (offsetHours < -12.0 || offsetHours > 14.0) {
      offsetHours = 0.0; // Default to GMT if invalid
    }
    utcOffsetInSeconds = (long)(offsetHours * 3600.0);
    // Reinitialize NTP client with new offset
    delete timeClient;
    timeClient = new NTPClient(ntpUDP, ntpServer, utcOffsetInSeconds);
    saveSettingsToEEPROM();
    showNotification("UTC OFFSET SAVED!");
    server.send(200, "text/plain", "UTC offset saved");
  }

  // Handle saving date and time from web form
  void handleSaveDateTime() {
    String dateStr = server.arg("date");
    String timeStr = server.arg("time");
    String dowStr = server.arg("dow");

    int year = dateStr.substring(0, 4).toInt();
    int month = dateStr.substring(5, 7).toInt();
    int day = dateStr.substring(8, 10).toInt();
    int hour = timeStr.substring(0, 2).toInt();
    int minute = timeStr.substring(3, 5).toInt();
    int dow = dowStr.toInt();

    if (year < 2025 || year > 2030) year = 2025;
    if (month < 1 || month > 12) month = 1;
    validateDate(year, month, day);
    if (hour < 0 || hour > 23) hour = 0;
    if (minute < 0 || minute > 59) minute = 0;

    int calculatedDow = calculateDayOfWeek(year, month, day);
    if (dow < 1 || dow > 7) dow = calculatedDow;

    Rtc.setDS1302Time(0, minute, hour, dow, day, month, year);
    lastDayOfMonth = day;
    saveSettingsToEEPROM();
    showNotification("TIME CHANGED!");
    server.send(200, "text/plain", "Date/Time saved");
  }

  // Handle saving bell settings from web form
  void handleSaveBellSettings() {
    bellDuration = server.arg("duration").toInt();
    if (bellDuration < 1 || bellDuration > 30) bellDuration = 5;

    bellEnabled = server.arg("enabled") == "on";

    if (server.arg("program_normal") == "on") {
      selectedProgram = PROGRAM_NORMAL;
    } else if (server.arg("program_ramadan") == "on") {
      selectedProgram = PROGRAM_RAMADAN;
    } else {
      selectedProgram = PROGRAM_CUSTOM;
    }

    saveSettingsToEEPROM();
    showNotification("PROGRAM SAVED!");
    server.send(200, "text/plain", "Bell settings saved");
  }

  // Handle saving standby period from web form
  void handleSaveStandby() {
    String sleepStr = server.arg("sleep");
    String wakeStr = server.arg("wake");

    standbyStartHour = sleepStr.substring(0, 2).toInt();
    standbyStartMinute = sleepStr.substring(3, 5).toInt();
    standbyEndHour = wakeStr.substring(0, 2).toInt();
    standbyEndMinute = wakeStr.substring(3, 5).toInt();

    if (standbyStartHour > 23) standbyStartHour = 22;
    if (standbyStartMinute > 59) standbyStartMinute = 0;
    if (standbyEndHour > 23) standbyEndHour = 6;
    if (standbyEndMinute > 59) standbyEndMinute = 0;

    saveSettingsToEEPROM();
    showNotification("STANDBY SAVED!");
    server.send(200, "text/plain", "Standby period saved");
  }

  // Handle manual NTP synchronization request from web form
  void handleSyncNTP() {
    if (syncTimeWithNTP()) {
      showNotification("NTP SYNCED!");
      lastDayOfMonth = Rtc.dayofmonth;
      lastSyncTime = millis();
    } else {
      showNotification("NTP FAILED!");
    }
    server.send(200, "text/plain", "NTP sync attempted");
  }

  // Handle editing custom schedules (GET for form, POST for submission)
  void handleEditSchedule() {
    int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= scheduleCount) {
      server.send(200, "text/plain", "Invalid index");
      return;
    }

    if (server.method() == HTTP_GET) {
      String html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Edit Schedule</title>"
        "<style>body{font-family:Arial;background:#f4f4f4;margin:0;padding:20px;color:#333}"
        ".container{max-width:500px;margin:auto;background:#fff;padding:20px;border-radius:8px}"
        "h2{color:#2980b9}"
        "form{display:flex;flex-direction:column;gap:10px}"
        "label{font-weight:bold}"
        "input[type=submit]{background:#3498db;color:#fff;border:none;padding:8px 14px;cursor:pointer}"
        "#toast{position:fixed;top:10%;left:50%;transform:translateX(-50%);background:#333;color:#fff;padding:8px 16px;border-radius:4px;display:none;z-index:1000}"
        "</style></head><body><div id='toast'></div><div class='container'>"
        "<h2>Edit Custom Schedule</h2>"
        "<form action='/editSchedule' method='post' onsubmit='return send(this)'>"
        "<input type='hidden' name='index' value='" + String(idx) + "'>"
        "<label>Time: <input type='time' name='time' value='";

      html += String(schedules[idx].hour < 10 ? "0" : "") + String(schedules[idx].hour) + ":" +
              String(schedules[idx].minute < 10 ? "0" : "") + String(schedules[idx].minute) +
              "'></label>"
              "<label>Days:<br>"
              "<input type='checkbox' name='sun'" + String(schedules[idx].days[0] ? " checked" : "") + ">Sun<br>"
              "<input type='checkbox' name='mon'" + String(schedules[idx].days[1] ? " checked" : "") + ">Mon<br>"
              "<input type='checkbox' name='tue'" + String(schedules[idx].days[2] ? " checked" : "") + ">Tue<br>"
              "<input type='checkbox' name='wed'" + String(schedules[idx].days[3] ? " checked" : "") + ">Wed<br>"
              "<input type='checkbox' name='thu'" + String(schedules[idx].days[4] ? " checked" : "") + ">Thu<br>"
              "<input type='checkbox' name='fri'" + String(schedules[idx].days[5] ? " checked" : "") + ">Fri<br>"
              "<input type='checkbox' name='sat'" + String(schedules[idx].days[6] ? " checked" : "") + ">Sat<br>"
              "</label>"
              "<input type='submit' value='Save Changes'></form>"
              "<p><a href='/'>Back</a></p>"
              "</div><script>"
              "function send(form){"
              "  event.preventDefault();"
              "  fetch(form.action,{method:form.method,body:new FormData(form)})"
              "  .then(r=>r.text())"
              "  .then(t=>{"
              "     const msg=document.getElementById('toast');"
              "     msg.textContent=t;msg.style.display='block';"
              "     setTimeout(()=>{msg.style.display='none';location.href=\"/\";},800);"
              "  });"
              "  return false;"
              "}"
              "</script></body></html>";

      server.send(200, "text/html", html);
      return;
    }

    String timeStr = server.arg("time");
    schedules[idx].hour = timeStr.substring(0, 2).toInt();
    schedules[idx].minute = timeStr.substring(3, 5).toInt();

    if (schedules[idx].hour > 23) schedules[idx].hour = 0;
    if (schedules[idx].minute > 59) schedules[idx].minute = 0;

    bool anyDay = false;
    for (int d = 0; d < 7; d++) {
      schedules[idx].days[d] = server.arg(
        d == 0 ? "sun" :
        d == 1 ? "mon" :
        d == 2 ? "tue" :
        d == 3 ? "wed" :
        d == 4 ? "thu" :
        d == 5 ? "fri" : "sat"
      ) == "on";
      if (schedules[idx].days[d]) anyDay = true;
    }

    if (!anyDay) {
      server.send(200, "text/plain", "Select at least one day");
      return;
    }

    saveSettingsToEEPROM();
    showNotification("CUSTOM UPDATED!");
    server.send(200, "text/plain", "Schedule updated");
  }

  // Handle removing a custom schedule from web form
  void handleRemoveSchedule() {
    int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= scheduleCount) {
      server.send(200, "text/plain", "Invalid index");
      return;
    }

    for (int i = idx; i < scheduleCount - 1; i++) {
      schedules[i] = schedules[i + 1];
    }
    scheduleCount--;

    saveSettingsToEEPROM();
    showNotification("CUSTOM REMOVED!");
    server.send(200, "text/plain", "Schedule removed");
  }

  // Handle time synchronization from mobile device
  void handleSyncMobile() {
    String mobileTime = server.arg("mobileTime");

    int year = mobileTime.substring(0, 4).toInt();
    int month = mobileTime.substring(5, 7).toInt();
    int day = mobileTime.substring(8, 10).toInt();
    int hour = mobileTime.substring(11, 13).toInt();
    int minute = mobileTime.substring(14, 16).toInt();
    int dow = calculateDayOfWeek(year, month, day);

    Rtc.setDS1302Time(0, minute, hour, dow, day, month, year);
    lastDayOfMonth = day;

    showNotification("MOBILE SYNCED!");
    server.send(200, "text/plain", "Mobile synced");
  }

  bool scrollTextDisplay(const String& text, int row) {
    if (millis() - scrollLast[row] < 350) return false;

    lcd.setCursor(0, row);
    int textLen = text.length() + 20;

    for (uint8_t i = 0; i < 20; i++) {
      uint8_t idx = (scrollPos[row] + i) % textLen;
      lcd.write(idx < text.length() ? text[idx] : ' ');
    }

    scrollPos[row] = (scrollPos[row] + 1) % textLen;
    scrollLast[row] = millis();
    return (scrollPos[row] == 0);
  }

  void displayGreetings() {
    if (showingNotification) {
      if (millis() - notificationStartTime < 2000) {
        lcd.setCursor(0, LCD_ROW2);
        lcd.print("                    ");
        lcd.setCursor((20 - notification.length()) / 2, LCD_ROW2);
        lcd.print(notification);
        return;
      } else {
        showingNotification = false;
        notification = "";
        int len = strlen(greeting);
        scrollPos[LCD_ROW2] = (len <= 20) ? (20 - len) / 2 : 0;
        scrollLast[LCD_ROW2] = 0;
        lcd.setCursor(0, LCD_ROW2);
        lcd.print("                    ");
      }
    }

    scrollTextDisplay(String(greeting), LCD_ROW2);
  }

  void displayCustomMessage() {
    if (animateMessage && customMessage.length() > 0) {
      scrollTextDisplay(customMessage, LCD_ROW3);
    } else {
      String shown = customMessage.substring(0, 20);
      lcd.setCursor(0, LCD_ROW3);
      lcd.print(shown);
      for (uint8_t i = shown.length(); i < 20; i++) lcd.print(' ');
    }
  }

  void displayDateTime() {
    Rtc.updateTime();

    char buf[24];
    if (Rtc.year < 2025 || Rtc.year > 2030) {
      snprintf(buf, sizeof(buf), "RTC ERR %02d:%02d T:%d",
              Rtc.hours, Rtc.minutes, (currentTemperature == -127) ? 0 : currentTemperature);
    } else {
      snprintf(buf, sizeof(buf), "%02d/%02d   %02d:%02d   T:%dC",
              Rtc.dayofmonth, Rtc.month, Rtc.hours, Rtc.minutes,
              (currentTemperature == -127) ? 0 : currentTemperature);
    }

    lcd.setCursor(0, LCD_ROW0);
    lcd.print(buf);
    for (uint8_t i = strlen(buf); i < 20; i++) lcd.print(' ');
  }

  // Handle adding a new custom schedule from web form
  void handleAddSchedule() {
    if (scheduleCount >= MAX_SCHEDULES) {
      server.send(200, "text/plain", "Max schedules reached");
      showNotification("MAX SCHEDULES!");
      return;
    }

    String timeStr = server.arg("time");
    if (timeStr.length() == 0) {
      server.send(200, "text/plain", "Empty time");
      showNotification("INVALID TIME!");
      return;
    }

    schedules[scheduleCount].hour = timeStr.substring(0, 2).toInt();
    schedules[scheduleCount].minute = timeStr.substring(3, 5).toInt();

    if (schedules[scheduleCount].hour > 23) schedules[scheduleCount].hour = 0;
    if (schedules[scheduleCount].minute > 59) schedules[scheduleCount].minute = 0;

    bool anyDay = false;
    for (int d = 0; d < 7; d++) {
      schedules[scheduleCount].days[d] = server.arg(
        d == 0 ? "sun" :
        d == 1 ? "mon" :
        d == 2 ? "tue" :
        d == 3 ? "wed" :
        d == 4 ? "thu" :
        d == 5 ? "fri" : "sat"
      ) == "on";
      if (schedules[scheduleCount].days[d]) anyDay = true;
    }

    if (!anyDay) {
      server.send(200, "text/plain", "Select at least one day");
      showNotification("NO DAYS SELECTED");
      return;
    }

    scheduleCount++;
    saveSettingsToEEPROM();
    showNotification("CUSTOM ADDED!");
    server.send(200, "text/plain", "Schedule added");
  }

  // Arduino setup function - runs once at startup
  void setup() {
    // kept in your preferred order
    digitalWrite(RELAY_PIN, HIGH);
    pinMode(RELAY_PIN, OUTPUT);
    delay(100);

    Serial.begin(115200);
    Serial.println("Booting up...");

    initLCDSafe();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Initializing...");
    delay(300);
    ESP.wdtFeed();

    loadSettingsFromEEPROM();

    delete timeClient;
    timeClient = new NTPClient(ntpUDP, ntpServer, utcOffsetInSeconds);
    Rtc.updateTime();
    if (Rtc.year < 2025 || Rtc.year > 2030) {
      int defaultYear = 2025;
      int defaultMonth = 1;
      int defaultDay = 1;
      int defaultDow = calculateDayOfWeek(defaultYear, defaultMonth, defaultDay);
      Rtc.setDS1302Time(0, 0, 12, defaultDow, defaultDay, defaultMonth, defaultYear);
      lastDayOfMonth = defaultDay;
    } else {
      int calculatedDow = calculateDayOfWeek(Rtc.year, Rtc.month, Rtc.dayofmonth);
      if (Rtc.dayofweek != calculatedDow) {
        Rtc.setDS1302Time(Rtc.seconds, Rtc.minutes, Rtc.hours, calculatedDow, Rtc.dayofmonth, Rtc.month, Rtc.year);
      }
      lastDayOfMonth = Rtc.dayofmonth;
    }

    WiFi.persistent(false);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    delay(100);

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(true);

    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(apSSID, apPassword, 6, 0, 4);

    Serial.print("AP SSID: ");
    Serial.println(apSSID);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, handleRoot);
    server.on("/saveDateTime", HTTP_POST, handleSaveDateTime);
    server.on("/saveBellSettings", HTTP_POST, handleSaveBellSettings);
    server.on("/saveStandby", HTTP_POST, handleSaveStandby);
    server.on("/addSchedule", HTTP_POST, handleAddSchedule);
    server.on("/editSchedule", HTTP_ANY, handleEditSchedule);
    server.on("/removeSchedule", HTTP_POST, handleRemoveSchedule);
    server.on("/syncNTP", HTTP_POST, handleSyncNTP);
    server.on("/syncMobile", HTTP_POST, handleSyncMobile);
    server.on("/saveCustomMessage", HTTP_POST, handleSaveCustomMessage);
    server.on("/saveUTCOffset", HTTP_POST, handleSaveUTCOffset);
    server.begin();

    wifiTaskState = WIFI_TASK_IDLE;
    wifiNetworkCursor = -1;
    wifiRetryAt = millis() + 1000UL;

    lastScrollTime = millis();
    lastSyncTime = 0;
    lastTempUpdate = 0;

    int len = strlen(greeting);
    scrollPos[LCD_ROW1] = (len <= 20) ? (20 - len) / 2 : 0;
    scrollLast[LCD_ROW1] = 0;

    lcd.clear();
    displayDateTime();
    displayGreetings();
    displayCustomMessage();

    showNotification("AP READY 192.168.4");
  }

  // Arduino main loop - runs repeatedly after setup
  void loop() {
    ESP.wdtFeed();

    Rtc.updateTime();
    if (Rtc.dayofmonth != lastDayOfMonth || (Rtc.hours == 0 && Rtc.minutes == 0 && Rtc.seconds == 0)) {
      lastDayOfMonth = Rtc.dayofmonth;
      int newDow = calculateDayOfWeek(Rtc.year, Rtc.month, Rtc.dayofmonth);
      if (Rtc.dayofweek != newDow) {
        Rtc.setDS1302Time(Rtc.seconds, Rtc.minutes, Rtc.hours, newDow, Rtc.dayofmonth, Rtc.month, Rtc.year);
        Serial.printf("DOW updated to %d for %04d-%02d-%02d\n", newDow, Rtc.year, Rtc.month, Rtc.dayofmonth);
      }
    }

    server.handleClient();
    serviceWiFi();
    serviceNetworkTasks();
    serviceLCDRecover();

    if (!isInStandby && !lcdNeedsRecover && millis() - lastScrollTime >= scrollInterval) {
      displayDateTime();
      displayGreetings();
      displayCustomMessage();
      lastScrollTime = millis();
    }

    checkStandby();

    if (bellEnabled && !isInStandby) {
      int currentHour = Rtc.hours;
      int currentMinute = Rtc.minutes;
      int currentDayOfWeek = (Rtc.dayofweek == 7) ? 0 : Rtc.dayofweek;

      if (!relayActive) {
        bool alreadyRangThisMinute = isSameBellMinuteNow();

        if (!alreadyRangThisMinute && currentProgramMatches(currentDayOfWeek, currentHour, currentMinute)) {
          digitalWrite(RELAY_PIN, LOW);
          relayStartTime = millis();
          relayActive = true;
          markBellMinuteNow();
          requestLCDRecover(150);
          Serial.print("Relay activated (");
          Serial.print(getProgramName(selectedProgram));
          Serial.println(")");
        }
      }

      if (relayActive && millis() - relayStartTime >= (unsigned long)bellDuration * 1000UL) {
        digitalWrite(RELAY_PIN, HIGH);
        relayActive = false;
        requestLCDRecover(150);
        Serial.println("Relay deactivated (OFF)");
      }
    } else {
      if (relayActive) {
        digitalWrite(RELAY_PIN, HIGH);
        relayActive = false;
        requestLCDRecover(150);
      } else {
        digitalWrite(RELAY_PIN, HIGH);
      }
    }

    delay(10);
  }
