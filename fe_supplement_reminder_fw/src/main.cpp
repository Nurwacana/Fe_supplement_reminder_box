#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include "RTClib.h"
#include "esp_bt.h"
#include "time.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

// ==================== KONFIGURASI ====================

// Muat BOT_TOKEN dari secrets.h (jangan commit file secrets.h ke git)
#if __has_include("secrets.h")
#include "secrets.h"
#elif __has_include("include/secrets.h")
#include "include/secrets.h"
#else
#warning "File secrets.h tidak ditemukan! Salin include/secrets.h.example menjadi include/secrets.h dan isi BOT_TOKEN Anda."
#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN_HERE"
#endif

// Konfigurasi Kredensial AP WiFiManager
const char* AP_SSID = "Fe-Reminder-Setup";
const char* AP_PASS = "12345678";

// Pin ESP32-C3 Super Mini
#define BUZZER_PIN  3  // Pin I/O Modul Buzzer (Low-Level Trigger)
#define LED_PIN     1  // Indikator LED
#define RESET_PIN   2  // Tombol reset Wi-Fi

// Bus I2C Hardware bawaan ESP32-C3
#define I2C_SDA     8  
#define I2C_SCL     9  

// Konfigurasi LEDC PWM Buzzer
#define PWM_CHANNEL 0
#define PWM_RES     8

// Objek RTC
RTC_DS3231 rtc;
// RTC_DS1307 rtc;

const char* daysOfTheWeek[7] = {
    "Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu"
};

// NTP Server
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;
const int   daylightOffset_sec = 0;

// Client Telegram
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// Penyimpanan NVS untuk Chat ID user yang subscribe
Preferences prefs;
std::vector<String> subscriberList;

// Konfigurasi Jam Pengingat Tablet Fe (Contoh: Setiap hari pukul 20:00)
const int ALARM_HOUR = 20;
const int ALARM_MINUTE = 0;
bool alarmTriggeredToday = false;
int lastAlarmDay = -1;

// ==================== KONTROL AUDIO (BUZZER) ====================

void initBuzzer() {
    ledcSetup(PWM_CHANNEL, 2700, PWM_RES);
    ledcAttachPin(BUZZER_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);
}

void buzzerTone(unsigned int freq) {
    if (freq == 0) {
        ledcWrite(PWM_CHANNEL, 0);
        return;
    }
    ledcWriteTone(PWM_CHANNEL, freq);
    ledcWrite(PWM_CHANNEL, 128);
}

void buzzerNoTone() {
    ledcWrite(PWM_CHANNEL, 0);
}

void beep(int count, int durationMs, int pauseMs = 100, unsigned int freq = 2700) {
    for (int i = 0; i < count; i++) {
        buzzerTone(freq);
        delay(durationMs);
        buzzerNoTone();
        if (i < count - 1) {
            delay(pauseMs);
        }
    }
}

// Nada alarm khusus pengingat minum obat
void playAlarmTone() {
    for (int k = 0; k < 5; k++) {
        beep(2, 100, 80, 2700);
        delay(200);
    }
}

// ==================== MANAJEMEN SUBSCRIBER ====================

void loadSubscribers() {
    prefs.begin("fe_bot", false);
    String raw = prefs.getString("chats", "");
    prefs.end();

    subscriberList.clear();
    if (raw.length() > 0) {
        int start = 0;
        int end = raw.indexOf(',');
        while (end != -1) {
            subscriberList.push_back(raw.substring(start, end));
            start = end + 1;
            end = raw.indexOf(',', start);
        }
        subscriberList.push_back(raw.substring(start));
    }
    Serial.printf("[Storage] Total subscriber terdaftar: %d\n", subscriberList.size());
}

void saveSubscribers() {
    String raw = "";
    for (size_t i = 0; i < subscriberList.size(); i++) {
        raw += subscriberList[i];
        if (i < subscriberList.size() - 1) raw += ",";
    }
    prefs.begin("fe_bot", false);
    prefs.putString("chats", raw);
    prefs.end();
}

bool addSubscriber(String chatId) {
    for (const auto& id : subscriberList) {
        if (id == chatId) return false; // Sudah terdaftar
    }
    subscriberList.push_back(chatId);
    saveSubscribers();
    return true;
}

bool removeSubscriber(String chatId) {
    for (auto it = subscriberList.begin(); it != subscriberList.end(); ++it) {
        if (*it == chatId) {
            subscriberList.erase(it);
            saveSubscribers();
            return true;
        }
    }
    return false;
}

// ==================== TELEGRAM BOT HANDLER ====================

void handleNewMessages(int numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
        String chat_id = String(bot.messages[i].chat_id);
        String text = bot.messages[i].text;
        String from_name = bot.messages[i].from_name;

        Serial.printf("[Bot] Pesan dari %s (%s): %s\n", from_name.c_str(), chat_id.c_str(), text.c_str());

        if (text == "/start" || text == "/subscribe") {
            if (addSubscriber(chat_id)) {
                String reply = "Halo " + from_name + "!\n";
                reply += "Anda telah *berlangganan* pengingat Tablet Tambah Darah (Fe).\n";
                reply += "Bot akan mengirim pesan pengingat setiap pukul " + String(ALARM_HOUR) + ":00 WIB.";
                bot.sendMessage(chat_id, reply, "Markdown");
            } else {
                bot.sendMessage(chat_id, "Anda sudah terdaftar dalam daftar pengingat.", "");
            }
        } 
        else if (text == "/stop" || text == "/unsubscribe") {
            if (removeSubscriber(chat_id)) {
                bot.sendMessage(chat_id, "Anda telah berhenti berlangganan pengingat.", "");
            } else {
                bot.sendMessage(chat_id, "Anda belum pernah terdaftar.", "");
            }
        } 
        else if (text == "/status") {
            DateTime now = rtc.now();
            char buffer[128];
            snprintf(buffer, sizeof(buffer), 
                     "Status Perangkat:\nWaktu RTC: %02d:%02d:%02d WIB\nJadwal Alarm: %02d:%02d WIB\nTotal Subscriber: %d",
                     now.hour(), now.minute(), now.second(), ALARM_HOUR, ALARM_MINUTE, subscriberList.size());
            bot.sendMessage(chat_id, String(buffer), "");
        } 
        else if (text == "/tes_alarm") {
            bot.sendMessage(chat_id, "Memulai tes buzzer dan notifikasi...", "");
            playAlarmTone();
            bot.sendMessage(chat_id, "Tes alarm selesai.", "");
        } 
        else {
            String help = "Perintah yang tersedia:\n";
            help += "/subscribe - Mulai terima pengingat tablet Fe\n";
            help += "/unsubscribe - Hentikan pengingat\n";
            help += "/status - Cek status RTC dan alarm\n";
            help += "/tes_alarm - Uji coba buzzer pengingat";
            bot.sendMessage(chat_id, help, "");
        }
    }
}

void broadcastReminder() {
    Serial.println("\n[Action] Mengirim notifikasi pengingat ke seluruh subscriber...");
    String message = " *PENGINGAT MINUM TABLET TAMBAH DARAH (Fe)* \n\n";
    message += "Waktunya minum Tablet Tambah Darah sekarang!\n";
    message += " *Tips:* Minum dengan air putih atau jus jeruk (Vitamin C membantu penyerapan). Hindari teh, kopi, atau susu saat meminumnya.";

    for (const auto& chatId : subscriberList) {
        bot.sendMessage(chatId, message, "Markdown");
        delay(100); // Hindari Telegram rate-limiting
    }
}

// ==================== CALLBACK & RESET ====================

void configModeCallback(WiFiManager *myWiFiManager) {
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    Serial.println("[Info] Mode AP Aktif. TX Power diset ke 8.5 dBm.");
    beep(2, 80, 80, 2000);
}

void checkResetButton() {
    pinMode(RESET_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_PIN) == LOW) {
        delay(2000);
        if (digitalRead(RESET_PIN) == LOW) {
            Serial.println("\n[Action] Reset ditekan! Menghapus data WiFi...");
            beep(3, 60, 60, 3200);
            WiFiManager wm;
            wm.resetSettings();
            delay(1000);
            ESP.restart();
        }
    }
}

void syncNTPtoRTC() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    Serial.print("[NTP] Menghubungi NTP server...");
    
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
        Serial.print(".");
        delay(500);
        retry++;
    }

    if (retry < 10) {
        Serial.println("\n[NTP] Waktu NTP berhasil didapatkan.");
        rtc.adjust(DateTime(timeinfo.tm_year + 1900, 
                            timeinfo.tm_mon + 1, 
                            timeinfo.tm_mday, 
                            timeinfo.tm_hour, 
                            timeinfo.tm_min, 
                            timeinfo.tm_sec));
        Serial.println("[RTC] Waktu RTC berhasil disinkronkan!");
    } else {
        Serial.println("\n[NTP] Gagal mengambil waktu NTP.");
    }
}

// ==================== SETUP ====================

void setup() {
    Serial.begin(115200);
    delay(2000);

    initBuzzer();
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    esp_bt_controller_disable();

    Serial.println("\n--- Memulai Fe-Reminder ESP32-C3 ---");
    beep(1, 100, 0, 2700);

    // Baca daftar subscriber dari flash memory
    loadSubscribers();

    // Inisialisasi I2C & RTC
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    if (!rtc.begin(&Wire)) {
        Serial.println("[Error] Modul RTC tidak terdeteksi pada pin 8 & 9!");
    } else {
        Serial.println("[Sukses] Modul RTC terhubung.");
        if (rtc.lostPower()) {
            Serial.println("[Peringatan] Baterai RTC habis/baru dipasang! Menyetel ke waktu kompilasi...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
    }

    checkResetButton();

    WiFi.mode(WIFI_AP_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    WiFiManager wm;
    wm.setAPCallback(configModeCallback);
    wm.setConnectTimeout(15);
    wm.setConfigPortalTimeout(180);

    bool res = wm.autoConnect(AP_SSID, AP_PASS);

    if (!res) {
        Serial.println("[Error] WiFi timeout / gagal connect.");
        beep(1, 1000, 0, 1200);
        delay(2000);
        ESP.restart();
    } else {
        Serial.println("\n[Sukses] Berhasil terhubung ke WiFi!");
        Serial.print("IP Address : ");
        Serial.println(WiFi.localIP());

        beep(1, 80, 40, 2200);
        beep(1, 150, 0, 3000);
        digitalWrite(LED_PIN, HIGH);

        syncNTPtoRTC();

        // Lewati validasi sertifikat SSL agar hemat RAM dan koneksi cepat
        secured_client.setInsecure();
    }
}

// ==================== LOOP ====================

void loop() {
    checkResetButton();

    static unsigned long lastBotCheck = 0;
    static unsigned long lastClockPrint = 0;

    // 1. Cek Pesan Masuk Telegram setiap 1.5 detik
    if (WiFi.status() == WL_CONNECTED && millis() - lastBotCheck >= 1500) {
        lastBotCheck = millis();
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        while (numNewMessages) {
            handleNewMessages(numNewMessages);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
    }

    // 2. Cek Jadwal Alarm dari RTC setiap 1 detik
    if (millis() - lastClockPrint >= 1000) {
        lastClockPrint = millis();
        DateTime now = rtc.now();

        Serial.printf("[%s] %04d-%02d-%02d %02d:%02d:%02d\n",
                      daysOfTheWeek[now.dayOfTheWeek()],
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());

        // Reset flag alarm ketika hari telah berganti
        if (now.day() != lastAlarmDay) {
            alarmTriggeredToday = false;
        }

        // Pengecekan Jam & Menit Alarm
        if (now.hour() == ALARM_HOUR && now.minute() == ALARM_MINUTE && !alarmTriggeredToday) {
            alarmTriggeredToday = true;
            lastAlarmDay = now.day();

            Serial.println("\n>>> [ALARM AKTIF] WAKTU MINUM TABLET Fe! <<<");

            // Bunyikan buzzer
            playAlarmTone();

            // Kirim notifikasi Telegram ke semua subscriber
            if (WiFi.status() == WL_CONNECTED) {
                broadcastReminder();
            }
        }
    }
}