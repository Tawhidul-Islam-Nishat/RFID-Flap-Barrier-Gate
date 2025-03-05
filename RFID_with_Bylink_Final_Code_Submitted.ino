// Blynk Configuration
#define BLYNK_TEMPLATE_ID "TMPL6YNUPhf1C"
#define BLYNK_TEMPLATE_NAME "RFID Barrier Gate"
#define BLYNK_AUTH_TOKEN "wk2Ak1tJ7F7JqgZMYZqqkR3zHBpR9OVg"

#include <WiFi.h>
#include "time.h"
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FirebaseESP32.h>
#include <Preferences.h>
#include <BlynkSimpleEsp32.h>
#include <map>

// WiFi Credentials
const char *ssid = "Birmingham";    // Replace with your WiFi SSID
const char *password = "*12345*12345*";  // Replace with your WiFi Password

// Blynk Auth Token
char authn[] = BLYNK_AUTH_TOKEN;  // Your Blynk Auth Token

// Firebase configuration
#define FIREBASE_HOST "https://rfid-barrier-gate-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "Qi11Zo56X4ldlRbJF2nEECfvQVUD0HXUmiukw5T2"

// NTP Time Server Settings (Bangladesh Time - GMT+6)
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 6 * 3600;  // GMT+6 for Bangladesh
const int daylightOffset_sec = 0;

// RFID Module Pins
#define SS_PIN 5
#define RST_PIN 4

// LED, Servo & Buzzer Pins
#define LED_G 26
#define LED_R 32
#define SERVO_PIN 27
#define BUZZER_PIN 12

// OLED Display Setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C  // Default I2C address for SH1106

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
MFRC522 mfrc522(SS_PIN, RST_PIN);
Servo myServo;

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Buzzer control variables
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
unsigned long buzzerDuration = 0;
Preferences preferences;
std::map<String, bool> userStatus;  // Store UID and their inside status

// User Data Structure
struct User {
  String uid;
  String name;
  String category;  // "Student", "Teacher", "Staff"
};

// List of Authorized Users (UID in *uppercase* without spaces)
User authorizedUsers[] = {
  { "C3BC43A8", "Ismail Hossain", "Student" },
  { "E3E7924A", "Tawhid (Nishat)", "Student" },
  { "33ED17A6", "Tawhidul Islam", "Teacher" },
  { "33A9AE4A", "Anup Podder", "Staff" }
};

// User state variables
bool userInside = false;    // Tracks if a user is inside the system
String currentUserID = "";  // Tracks the UID of the current user inside

int studentCount = 0;
int teacherCount = 0;
int staffCount = 0;
int emptySeats = 10;  // Total available seats

bool showAccessMessage = false;
String accessMessage = "";
String userInfo = "";
unsigned long lastScanTime = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  //resetStoredData();  // Reset data every time the code is uploaded

  // Connect to WiFi
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED to WiFi!");

  // Initialize NTP Time for Bangladesh (BST)
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(2000);  // Wait for time synchronization

  loadStoredData();  // Load stored values from NVS
  loadUserStatus();  // Load user statuses from storage

  // Initialize Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(LED_G, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  myServo.attach(SERVO_PIN);
  myServo.write(0);

  // Initialize OLED Display
  if (!display.begin(OLED_ADDR, true)) {
    Serial.println(F("ERROR: OLED Display Not Found!"));
    while (1)
      ;
  }

  display.clearDisplay();
  display.display();
  delay(1000);
  showWelcomeScreen();

  // Initialize Blynk
  Blynk.begin(authn, ssid, password);
}

void loop() {
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Card Detected! Reading...");
    handleCardScan();
    lastScanTime = millis();
    showAccessMessage = true;
  }

  // Reset system if no card present for 3 seconds
  if (showAccessMessage && (millis() - lastScanTime > 3000)) {
    showAccessMessage = false;
    resetSystem();
  }

  // Handle buzzer timing
  if (buzzerActive && (millis() - buzzerStartTime >= buzzerDuration)) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
  }

  updateOLED();
  updateBlynkValues();  // Update Blynk app values
  delay(100);
}

void resetStoredData() {
  preferences.begin("rfid_data", false);
  preferences.clear();  // Erase all stored data
  preferences.end();

  preferences.begin("user_status", false);
  preferences.clear();  // Erase stored user statuses
  preferences.end();
}

void storeData() {
  preferences.begin("rfid_data", false);
  preferences.putInt("studentCount", studentCount);
  preferences.putInt("teacherCount", teacherCount);
  preferences.putInt("staffCount", staffCount);
  preferences.putInt("emptySeats", emptySeats);
  preferences.end();
}

void loadStoredData() {
  preferences.begin("rfid_data", true);
  studentCount = preferences.getInt("studentCount", 0);
  teacherCount = preferences.getInt("teacherCount", 0);
  staffCount = preferences.getInt("staffCount", 0);
  emptySeats = preferences.getInt("emptySeats", 10);
  preferences.end();
}

void setUserStatusToStorage(String uid, bool isInside) {
  preferences.begin("user_status", false);
  preferences.putBool(uid.c_str(), isInside);
  preferences.end();
}

// final segment

bool getUserStatusFromStorage(String uid) {
  preferences.begin("user_status", true);
  bool status = preferences.getBool(uid.c_str(), false);  // Default to false (not inside)
  preferences.end();
  return status;
}

void loadUserStatus() {
  for (User user : authorizedUsers) {
    userStatus[user.uid] = getUserStatusFromStorage(user.uid);
  }
}

void handleCardScan() {
  String uid = readUID();
  Serial.print("Card UID: ");
  Serial.println(uid);

  User user = checkUID(uid);

  if (user.uid != "") {
    if (userStatus[user.uid]) {
      // User is exiting
      Serial.println("Access Granted! User is exiting.");
      grantAccess(user);
      sendToFirebase(user.name, user.uid, "Exit");
      userStatus[user.uid] = false;
      setUserStatusToStorage(user.uid, false);  //  Save exit status
      updateUserCount(user, false);             // Decrease count
    } else {
      // Check for available seats if user is a student
      if (user.category == "Student" && emptySeats == 0) {
        Serial.println("No vacant seat available!");
        grantAccess(user);
        accessMessage = "No vacant seat";
        return;
      }

      // User is entering
      Serial.println("Access Granted! User is entering.");
      grantAccess(user);
      sendToFirebase(user.name, user.uid, "Entry");
      userStatus[user.uid] = true;
      setUserStatusToStorage(user.uid, true);  // Save entry status
      updateUserCount(user, true);             // Increase count
    }
  } else {
    // Unauthorized user
    Serial.println("Access Denied! Unauthorized user.");
    denyAccess(uid);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void updateUserCount(User user, bool entering) {
  if (user.category == "Student") {
    studentCount += (entering ? 1 : -1);
    emptySeats += (entering ? -1 : 1);
  } else if (user.category == "Teacher") {
    teacherCount += (entering ? 1 : -1);
  } else if (user.category == "Staff") {
    staffCount += (entering ? 1 : -1);
  }
  storeData();  // Save updated values persistently
}

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Show real-time clock at the top
  display.setCursor(0, 0);
  showTimeOnOLED();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(15, 22);
  display.print("Scan Your Card");

  if (showAccessMessage) {
    display.clearDisplay();
    display.setCursor(0, 0);
    showTimeOnOLED();
    // if (accessMessage == "No vacant seat" && emptySeats == 0) {
    //   display.setTextSize(1);
    //   display.setTextColor(SH110X_WHITE);
    //   display.setCursor(17, 22);
    //   display.println(accessMessage);
    if (accessMessage == "Access Denied") {
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(17, 22);
      //accessMessage = "Access Denied!";
      display.println(accessMessage);
    } else {
      display.setTextSize(1);
      display.setTextColor(SH110X_WHITE);
      display.setCursor(17, 22);
      //accessMessage = "Access Granted!";
      display.println(accessMessage);
      display.setCursor(0, 35);
      display.println(userInfo);
    }
  }

  // Display User count data
  else {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(10, 40);
    display.print("St  Emp  Th  Stf");

    display.setCursor(10, 50);
    display.printf("%2d  %2d  %2d   %2d", studentCount, emptySeats, teacherCount, staffCount);
  }

  display.display();
}

// Another segment

void grantAccess(User user) {
  // if (accessMessage == "No vacant seat" && emptySeats == 0) {
  //   digitalWrite(LED_G, LOW);
  //   digitalWrite(LED_R, HIGH);
  //   digitalWrite(BUZZER_PIN, HIGH);
  //   buzzerStartTime = millis();
  //   buzzerDuration = 2000;
  //   buzzerActive = true;

  //   // accessMessage = "Access Denied";
  //   // userInfo = "User: " + user.name + "\n\nID: " + user.uid;
  //   // Serial.println(accessMessage);
  //   // Serial.println(userInfo);
  // } else {
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_R, LOW);
  myServo.write(90);
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerStartTime = millis();
  buzzerDuration = 350;
  buzzerActive = true;

  accessMessage = "Access Granted";
  userInfo = "User: " + user.name + "\n\nID: " + user.uid;
  //Serial.println(accessMessage);
  Serial.println(userInfo);
  //}
}

void denyAccess(String uid) {
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, LOW);
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerStartTime = millis();
  buzzerDuration = 2000;
  buzzerActive = true;

  accessMessage = "Access Denied";
  userInfo = " ID: " + uid;
  // Serial.println(accessMessage);
  Serial.println(userInfo);
}

String readUID() {
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

User checkUID(String uid) {
  for (User u : authorizedUsers) {
    if (uid == u.uid) return u;
  }
  return { "", "", "" };
}

void updateBlynkValues() {
  Blynk.virtualWrite(V1, studentCount);  // Virtual Pin 1 for studentCount
  Blynk.virtualWrite(V2, teacherCount);  // Virtual Pin 2 for teacherCount
  Blynk.virtualWrite(V3, staffCount);    // Virtual Pin 3 for staffCount
  Blynk.virtualWrite(V4, emptySeats);    // Virtual Pin 4 for emptySeats
}

void showTimeOnOLED() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    display.setCursor(10, 0);
    display.println("Time Unavailable");
    return;
  }

  int hour = timeinfo.tm_hour;
  String ampm = "AM";
  if (hour >= 12) {
    ampm = "PM";
    if (hour > 12) hour -= 12;
  } else if (hour == 0) {
    hour = 12;
  }

  char timeStr[10];
  sprintf(timeStr, "%02d:%02d %s", hour, timeinfo.tm_min, ampm.c_str());

  display.setTextSize(2);
  display.setCursor(10, 0);
  display.println(timeStr);
}

void showWelcomeScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(" RFID Access System");
  display.setCursor(22, 24);
  display.println("Developed By:");
  display.setCursor(0, 37);
  display.println("Tawhid,Jannat,Ismail");
  display.display();
  delay(5000);
}

void resetSystem() {
  Serial.println("Resetting system...");
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_R, LOW);
  myServo.write(0);
}

void sendToFirebase(String userName, String userID, String eventType) {
  if (Firebase.ready()) {
    String path = "/access_logs/" + userID + "/" + eventType;  // Path for the user's logs
    String timestamp = getTimestamp();

    FirebaseJson json;
    json.set("User-name", userName.c_str());
    json.set("User-ID", userID.c_str());
    json.set("Event-Time", timestamp.c_str());

    // Use pushJSON to append data instead of overwriting
    if (Firebase.pushJSON(fbdo, path, json)) {
      Serial.println("Data sent to Firebase successfully!");
    } else {
      Serial.print("Failed to send data to Firebase. Error: ");
      Serial.println(fbdo.errorReason());
    }
  }
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Unavailable";
  }

  char timestamp[20];
  sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d",
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(timestamp);
}
