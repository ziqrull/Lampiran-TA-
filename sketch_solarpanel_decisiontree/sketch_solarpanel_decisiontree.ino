// ============================================================
//  SISTEM PEMBERSIH PANEL SURYA OTOMATIS
// ============================================================

#include <WiFi.h>
#include <Wire.h>
#include <Firebase_ESP_Client.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>
#include "RTClib.h"

// ============================================================
// 1. KREDENSIAL Wi-Fi & FIREBASE
// ============================================================
#define WIFI_SSID      "Kerjain_Tugas"
#define WIFI_PASSWORD  "MAUMASUKSURGAibadah"
#define API_KEY        "AIzaSyB35XekC0Yb2CHFQAiKblgERKYWximCpsQ"
#define DATABASE_URL   "https://solarpanelmonitor-e992c-default-rtdb.asia-southeast1.firebasedatabase.app"

// ============================================================
// 2. DEFINISI PIN
// ============================================================
#define VOLTAGE_PIN    32
#define ONE_WIRE_BUS    4
#define RELAY_POMPA     2
#define RPWM           25
#define LPWM           26
#define R_EN           27
#define L_EN           14
#define LIMIT_KIRI     33
#define LIMIT_KANAN    13

// ============================================================
// KONFIGURASI MOTOR JGY-370
// ============================================================

#define PWM_MAKS 255     

// ============================================================
// 3. KALIBRASI VOLTAGE SENSOR
// ============================================================
#define FAKTOR_PEMBAGI   7.2727
#define FAKTOR_KOREKSI   1.240
#define JUMLAH_SAMPEL_MEDIAN   9
#define ALPHA_EMA              0.2
#define ADC_MAX_VALUE           4095.0
#define ADC_REF_VOLTAGE          3.3

// ============================================================
// 3b. JAM OPERASIONAL SISTEM
// ============================================================
#define JAM_MULAI     8
#define JAM_SELESAI   17

// ============================================================
// 4. INISIALISASI OBJEK SENSOR
// ============================================================
OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
BH1750            bh1750(0x23);
RTC_DS3231        rtc;

// ============================================================
// 5. OBJEK FIREBASE
// ============================================================
FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;
bool           signupOK = false;

// ============================================================
// 6. VARIABEL STATE SISTEM
// ============================================================
unsigned long prevMillisSensor           = 0;
const long    intervalSensor             = 5000;
bool          statusOperasionalSebelumnya = true;

enum StateWiper {
  STANDBY,
  MAJU,
  BERHENTI_KANAN,
  MUNDUR,
  BERHENTI_KIRI
};

StateWiper    stateWiper         = STANDBY;
unsigned long prevMillisBerhenti = 0;
const long    durasiJeda         = 1000;

float emaTegangan  = -1.0;
bool  emaSudahInit = false;

// ============================================================
// 7. FUNGSI BACA TEGANGAN (Median Filter + EMA)
// ============================================================
int bandingkanInt(const void *a, const void *b) {
  return (*(int*)a - *(int*)b);
}

float bacaTegangan() {
  int sampel[JUMLAH_SAMPEL_MEDIAN];
  for (int i = 0; i < JUMLAH_SAMPEL_MEDIAN; i++) {
    sampel[i] = analogRead(VOLTAGE_PIN);
    delay(2);
  }
  qsort(sampel, JUMLAH_SAMPEL_MEDIAN, sizeof(int), bandingkanInt);
  int   nilaiMedian = sampel[JUMLAH_SAMPEL_MEDIAN / 2];
  float voltADC     = (nilaiMedian / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
  float voltMentah  = voltADC * FAKTOR_PEMBAGI * FAKTOR_KOREKSI;

  if (!emaSudahInit) {
    emaTegangan  = voltMentah;
    emaSudahInit = true;
  } else {
    emaTegangan = (ALPHA_EMA * voltMentah) + ((1.0 - ALPHA_EMA) * emaTegangan);
  }
  return emaTegangan;
}

// ============================================================
// 8. FUNGSI MOTOR WIPER (BTS7960)
// ============================================================
void motorMaju(int pwm)
{
  if (pwm > PWM_MAKS)
    pwm = PWM_MAKS;

  analogWrite(LPWM, 0);
  analogWrite(RPWM, pwm);
}

void motorMundur(int pwm)
{
  if (pwm > PWM_MAKS)
    pwm = PWM_MAKS;

  analogWrite(RPWM, 0);
  analogWrite(LPWM, pwm);
}

void motorStop()
{
  analogWrite(RPWM, 0);
  analogWrite(LPWM, 0);
} 

// ============================================================
// // ===========================================================
// DECISION TREE HASIL TRAINING
// ===========================================================
String tentukanKeputusan(float tegangan, float suhu, float cahaya)
{
  // Rule 1
  if (tegangan > 21.38)
  {
    return "Tidak Perlu";
  }

  // Rule 2
  if (tegangan <= 19.35)
  {
    if (cahaya <= 27021.25)
    {
      return "Tidak Perlu";
    }
    else
    {
      return "Bersihkan";
    }
  }

  // Rule 3
  if (tegangan <= 21.38)
  {
    if (cahaya <= 12018.33)
    {
      return "Tidak Perlu";
    }
    else
    {
      if (cahaya >= 39283.75)
      {
        if (tegangan <= 20.95)
        {
          return "Bersihkan";
        }
        else
        {
         return "Tidak Perlu";
        }
    }
  }

  }
}

// ============================================================
// 9b. FUNGSI CEK JAM OPERASIONAL
// ============================================================
bool cekJamOperasional(DateTime waktu) {
  int jam = waktu.hour();
  return (jam >= JAM_MULAI && jam < JAM_SELESAI);
}

// ============================================================
// 9c. FUNGSI MATIKAN SEMUA AKTUATOR
// ============================================================
void matikanSemuaAktuator() {
  motorStop();
  digitalWrite(RELAY_POMPA, LOW);
  stateWiper = STANDBY;
}

// ============================================================
// 10. FUNGSI MANAJEMEN WIPER (NON-BLOCKING)
// ============================================================
void updateWiper() {
  // Baca limit switch dengan debouncing 3x
  bool kiri1  = (digitalRead(LIMIT_KIRI)  == LOW);
  delay(3);
  bool kiri2  = (digitalRead(LIMIT_KIRI)  == LOW);
  delay(3);
  bool kiri3  = (digitalRead(LIMIT_KIRI)  == LOW);
  bool limitKiri = (kiri1 && kiri2 && kiri3);

  bool kanan1 = (digitalRead(LIMIT_KANAN) == LOW);
  delay(3);
  bool kanan2 = (digitalRead(LIMIT_KANAN) == LOW);
  delay(3);
  bool kanan3 = (digitalRead(LIMIT_KANAN) == LOW);
  bool limitKanan = (kanan1 && kanan2 && kanan3);

  switch (stateWiper) {
    case STANDBY:
      break;
    case MAJU:
      if (limitKanan) {
        motorStop();
        Serial.println("[Wiper] Limit KANAN tertekan — berhenti sejenak.");
        prevMillisBerhenti = millis();
        stateWiper = BERHENTI_KANAN;
      }
      break;
    case BERHENTI_KANAN:
      if (millis() - prevMillisBerhenti >= durasiJeda) {
        Serial.println("[Wiper] Mulai mundur ke kiri.");
        motorMundur(PWM_MAKS);
        stateWiper = MUNDUR;
      }
      break;
    case MUNDUR:
      if (limitKiri) {
        motorStop();
        Serial.println("[Wiper] Limit KIRI tertekan — wiper kembali ke posisi awal.");
        prevMillisBerhenti = millis();
        stateWiper = BERHENTI_KIRI;
      }
      break;
    case BERHENTI_KIRI:
      if (millis() - prevMillisBerhenti >= durasiJeda) {
        Serial.println("[Wiper] Siklus selesai. Pompa dimatikan.");
        digitalWrite(RELAY_POMPA, LOW);
        stateWiper = STANDBY;
      }
      break;
  }
}

// ============================================================
// 11. SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire.setClock(100000);

  pinMode(LIMIT_KIRI,  INPUT_PULLUP);
  pinMode(LIMIT_KANAN, INPUT_PULLUP);
  Serial.println("[Limit Switch] Kiri:GPIO33 | Kanan:GPIO13 — OK");

  // --- Koneksi Wi-Fi ---
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Menghubungkan");
  int wifiRetry = 0;
  while (WiFi.status() != WL_CONNECTED && wifiRetry < 20) {
    delay(500);
    Serial.print(".");
    wifiRetry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Terhubung! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] GAGAL. Periksa SSID/Password.");
  }

  // --- Firebase ---
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = "kEXuMaTmfr0ei1mLju4dKvAFcQyC25g3iPqK0FZ1";
  signupOK = true;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("[Firebase] Inisialisasi selesai");

  // --- DS18B20 ---
  ds18b20.begin();
  Serial.println("[DS18B20] OK (GPIO4)");

  // --- BH1750 ---
  if (!bh1750.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("[BH1750] GAGAL! Cek wiring / alamat I2C.");
  } else {
    Serial.println("[BH1750] OK (0x23)");
  }

  // --- RTC DS3231 ---
  if (!rtc.begin()) {
    Serial.println("[RTC] GAGAL! Cek wiring SDA/SCL.");
  } else {
    Serial.println("[RTC] OK (0x68)");
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println("[RTC] Waktu di-reset ke waktu kompilasi.");
    }
  }

  // --- Voltage Sensor ---
  pinMode(VOLTAGE_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(VOLTAGE_PIN, ADC_11db);
  Serial.println("[Voltage Sensor] OK (GPIO32, ADC1, 12-bit, 11dB)");

  // --- Relay Pompa ---
  pinMode(RELAY_POMPA, OUTPUT);
  digitalWrite(RELAY_POMPA, LOW);

  // --- Motor Driver BTS7960 ---
  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  motorStop();

  Serial.printf("[Jam Operasional] Sistem aktif pukul %02d:00 - %02d:00\n",
                JAM_MULAI, JAM_SELESAI);
  Serial.println("[Setup] Selesai. Sistem siap.");

// --- Pastikan wiper di posisi kiri (awal) saat startup ---
Serial.println("[Init] Memastikan wiper di posisi awal (kiri)...");

if (digitalRead(LIMIT_KIRI) != LOW) {
  motorMundur(150);

  // Tambahkan jeda 500ms setelah motor mulai bergerak
  // sebelum mulai cek limit switch
  // Ini memberi waktu motor stabil & noise awal mereda
  delay(500);

  unsigned long startTime = millis();

  while (true) {
    // Debouncing: baca limit switch 3 kali berturut-turut
    // hanya dianggap tertekan jika ketiganya LOW
    bool baca1 = (digitalRead(LIMIT_KIRI) == LOW);
    delay(5);
    bool baca2 = (digitalRead(LIMIT_KIRI) == LOW);
    delay(5);
    bool baca3 = (digitalRead(LIMIT_KIRI) == LOW);

    if (baca1 && baca2 && baca3) {
      // Ketiga pembacaan LOW → limit switch benar-benar tertekan
      Serial.println("[Init] Limit kiri terdeteksi.");
      break;
    }

    // Timeout 15 detik
    if (millis() - startTime > 120000) {
      Serial.println("[Init] TIMEOUT! Limit kiri tidak terdeteksi. Cek wiring.");
      break;
    }

    delay(10);
  }

  motorStop();
}

Serial.println("[Init] Wiper siap di posisi awal (kiri).");
}

// ============================================================
// 12. LOOP UTAMA
// ============================================================
void loop() {
  unsigned long currentMillis = millis();
  DateTime now = rtc.now();

  // --- Cek Jam Operasional ---
  bool statusOperasional = cekJamOperasional(now);

  if (!statusOperasional && statusOperasionalSebelumnya) {
    Serial.println("[Jam Operasional] Di luar jam — semua aktuator dimatikan.");
    matikanSemuaAktuator();
  }
  if (statusOperasional && !statusOperasionalSebelumnya) {
    Serial.println("[Jam Operasional] Memasuki jam operasional — sistem aktif.");
  }
  statusOperasionalSebelumnya = statusOperasional;

  // --- Update State Wiper (hanya saat jam operasional) ---
  if (statusOperasional) {
    updateWiper();
  }

  // --- Baca Sensor & Kirim Firebase (tiap 5 detik) ---
  if (currentMillis - prevMillisSensor >= intervalSensor) {
    prevMillisSensor = currentMillis;

    char waktuBuf[20], tanggalBuf[20];
    sprintf(waktuBuf,   "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    sprintf(tanggalBuf, "%04d-%02d-%02d", now.year(), now.month(),  now.day());

    // Di luar jam operasional — hanya lapor status
    if (!statusOperasional) {
      Serial.printf("[%s %s] SISTEM NONAKTIF (di luar jam %02d:00-%02d:00)\n",
                    tanggalBuf, waktuBuf, JAM_MULAI, JAM_SELESAI);
      if (signupOK && Firebase.ready()) {
        Firebase.RTDB.setString(&fbdo, "/Monitoring/Keputusan",          "Sistem Nonaktif");
        Firebase.RTDB.setString(&fbdo, "/Monitoring/Waktu",              String(waktuBuf));
        Firebase.RTDB.setString(&fbdo, "/Monitoring/Tanggal",            String(tanggalBuf));
        Firebase.RTDB.setBool(&fbdo,   "/Monitoring/Status_Pembersih",   false);
        Firebase.RTDB.setBool(&fbdo,   "/Monitoring/Status_Operasional", false);
      }
      return;
    }

    // --- Baca Sensor ---
    float tegangan_V = bacaTegangan();
    ds18b20.requestTemperatures();
    float suhu_C     = ds18b20.getTempCByIndex(0);
    float cahaya_lux = bh1750.readLightLevel();

    bool limitKiri  = (digitalRead(LIMIT_KIRI)  == LOW);
    bool limitKanan = (digitalRead(LIMIT_KANAN) == LOW);

    // --- Validasi Data ---
    bool dataValid = true;
    if (tegangan_V < 0 || tegangan_V > 30.0) {
      Serial.println("[Voltage] Data tidak valid! Cek wiring GPIO32.");
      dataValid = false;
    }
    if (suhu_C == -127.0 || isnan(suhu_C)) {
      Serial.println("[DS18B20] Data tidak valid! Cek wiring & pull-up 4.7kΩ.");
      dataValid = false;
    }
    if (cahaya_lux < 0 || isnan(cahaya_lux)) {
      Serial.println("[BH1750] Data tidak valid! Cek wiring.");
      dataValid = false;
    }

    // --- Keputusan Decision Tree ---
    // Urutan parameter: tegangan (utama), cahaya (kedua), suhu (terakhir)
    String keputusan = "Tidak Perlu";
    if (dataValid) {
      keputusan = tentukanKeputusan(tegangan_V, cahaya_lux, suhu_C);
    }

    // --- Eksekusi Aktuator ---
    if (stateWiper == STANDBY) {
      if (keputusan == "Bersihkan") {
        Serial.println("[Aktuator] BERSIHKAN — Pompa & wiper diaktifkan.");
        digitalWrite(RELAY_POMPA, HIGH);
        motorMaju(PWM_MAKS);
        stateWiper = MAJU;
      } else {
        Serial.println("[Aktuator] TIDAK PERLU — Sistem standby.");
      }
    } else {
      Serial.println("[Aktuator] Wiper sedang bergerak, keputusan baru ditunda.");
    }

    // --- Serial Monitor ---
    Serial.printf("[%s %s] V:%.2fV | T:%.2fC | Cahaya:%.2flux | "
                  "LS_Kiri:%s | LS_Kanan:%s | %s\n",
                  tanggalBuf, waktuBuf,
                  tegangan_V, suhu_C, cahaya_lux,
                  limitKiri  ? "ON" : "OFF",
                  limitKanan ? "ON" : "OFF",
                  keputusan.c_str());

    // --- Kirim ke Firebase /Monitoring ---
    if (signupOK && Firebase.ready() && dataValid) {
      Serial.println("[Firebase] Mengirim data...");
      bool ok = true;
      ok &= Firebase.RTDB.setFloat(&fbdo,  "/Monitoring/Tegangan_V",          tegangan_V);
      ok &= Firebase.RTDB.setFloat(&fbdo,  "/Monitoring/Suhu_C",              suhu_C);
      ok &= Firebase.RTDB.setFloat(&fbdo,  "/Monitoring/Intensitas_Cahaya",   cahaya_lux);
      ok &= Firebase.RTDB.setString(&fbdo, "/Monitoring/Keputusan",           keputusan);
      ok &= Firebase.RTDB.setString(&fbdo, "/Monitoring/Waktu",               String(waktuBuf));
      ok &= Firebase.RTDB.setString(&fbdo, "/Monitoring/Tanggal",             String(tanggalBuf));
      ok &= Firebase.RTDB.setBool(&fbdo,   "/Monitoring/Status_Pembersih",    (stateWiper != STANDBY));
      ok &= Firebase.RTDB.setBool(&fbdo,   "/Monitoring/Limit_Switch_Kiri",   limitKiri);
      ok &= Firebase.RTDB.setBool(&fbdo,   "/Monitoring/Limit_Switch_Kanan",  limitKanan);
      ok &= Firebase.RTDB.setBool(&fbdo,   "/Monitoring/Status_Operasional",  true);

      if (ok) {
        Serial.println("[Firebase] Data monitoring terkirim!");
      } else {
        Serial.println("[Firebase] Gagal kirim monitoring: " + fbdo.errorReason());
      }

      // --- Kirim ke /DataLog/{timestamp} untuk keperluan training ---
      long   unixTime = now.unixtime();
      String logPath  = "/DataLog/" + String(unixTime);

      bool okLog = true;
      okLog &= Firebase.RTDB.setFloat(&fbdo,  (logPath + "/tegangan").c_str(), tegangan_V);
      okLog &= Firebase.RTDB.setFloat(&fbdo,  (logPath + "/suhu").c_str(),     suhu_C);
      okLog &= Firebase.RTDB.setFloat(&fbdo,  (logPath + "/cahaya").c_str(),   cahaya_lux);
      okLog &= Firebase.RTDB.setString(&fbdo, (logPath + "/waktu").c_str(),    String(waktuBuf));
      okLog &= Firebase.RTDB.setString(&fbdo, (logPath + "/tanggal").c_str(),  String(tanggalBuf));
      okLog &= Firebase.RTDB.setString(&fbdo, (logPath + "/label").c_str(),    keputusan);

      if (okLog) {
        Serial.println("[Firebase] Data log tersimpan di /DataLog/" + String(unixTime));
      } else {
        Serial.println("[Firebase] Gagal simpan log: " + fbdo.errorReason());
      }

    } else if (!dataValid) {
      Serial.println("[Firebase] Data tidak valid, kirim dibatalkan.");
    } else if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[Firebase] WiFi terputus, mencoba reconnect...");
      WiFi.reconnect();
    }
  }
}
