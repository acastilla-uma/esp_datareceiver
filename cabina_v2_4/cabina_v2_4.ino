#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <Wire.h>
#include "FS.h"
#include "SD_MMC.h"
#include <SPI.h>
#include "SparkFun_ISM330DHCX.h"
#include <SparkFun_RV8803.h>
#include <Preferences.h>

#include <esp_task_wdt.h>
#include <esp_idf_version.h>

Preferences preferences;

constexpr uint8_t SD_POWER_ENABLE_PIN = 32;
constexpr int RXD2 = 35;
constexpr int TXD2 = 33;

RV8803 rtc;
constexpr char DEVICE_ID[] = "DEV001";
int anular = 1;

static SemaphoreHandle_t ptrMutex = xSemaphoreCreateMutex();

constexpr uint8_t IMU_CS = 5;
constexpr uint32_t TASK_PERIOD_MS = 20;
constexpr size_t LOG_BUFFER_SIZE = 2500;

SparkFun_ISM330DHCX_SPI myISM;
sfe_ism_data_t accelData;
sfe_ism_data_t gyroData;

float freemem;

String currentDate;
String currentTime;
unsigned long numarchivo;
String fileDate;
unsigned long sessionNumber;

float d1;
float coeff;
float alpha;
float alphav;
float s = 470.0f;  // ancho de vía completo del AGV, mm
float h = 250.0f;  // altura del centro de gravedad del AGV, mm
float phi1;
float phi1crit;
float SI;
bool hayreset = false;
bool initcabecera = true;
int contasd = 0;
char texto[LOG_BUFFER_SIZE];

void appendFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (!file.print(message)) {
    Serial.println("Append failed");
  }
  file.close();
}

unsigned long sdTiming[2] = {0, 0};
QueueHandle_t Q3;
QueueHandle_t Q4;

void enqueueTask3Message(const char *message) {
  char queueMessage[LOG_BUFFER_SIZE] = {};
  strncpy(queueMessage, message, sizeof(queueMessage) - 1);
  xQueueSend(Q3, queueMessage, 0);
}

void Task1(void *pvParameters);
void Task3(void *pvParameters);

void setup() {
#if ESP_IDF_VERSION_MAJOR >= 5
  const esp_task_wdt_config_t watchdogConfig = {
    .timeout_ms = 30000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = false,
  };
  esp_task_wdt_init(&watchdogConfig);
#else
  esp_task_wdt_init(30, false);  //pone el watchdog a 30 segundos y desactiva el reset del chip
#endif

  Wire.begin();
  Wire.setClock(100000);

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  if (esp_reset_reason() != ESP_RST_POWERON) {
    Serial.println("hubo un reset");
    hayreset = true;
  }

  pinMode(SD_POWER_ENABLE_PIN, OUTPUT);
  digitalWrite(SD_POWER_ENABLE_PIN, HIGH);

  Q3 = xQueueCreate(5, LOG_BUFFER_SIZE);
  Q4 = xQueueCreate(1, sizeof(sdTiming));

  xTaskCreatePinnedToCore(Task1, "Task Fisica", 20000, NULL, 4, NULL, 0);
  xTaskCreatePinnedToCore(Task3, "Task SD", 20000, NULL, 4, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);
}

// RTOS tasks: sensor acquisition and SD/RTC processing.

void Task1(void *pvParameters) {
  byte entradaserie = 0;
  unsigned long timeant;
  float gx;
  float gy;
  float gz;
  float ax;
  float ay;
  float az;
  float roll;
  float pitch;
  float yaw;
  float gzzero = 0.0f;
  float gyzero = 0.0f;
  float gxzero = 0.0f;
  uint8_t calibrationSamples = 0;
  float k1 = 1.15;
  float k2 = 2.05;
  float k3 = 0.75f;
  float k4 = 1100;
  float rolla;
  float pitcha;
  float diferencia;
  float diferenciap;
  float theta;
  float phi;

  unsigned long usciclo[6] = {};
  bool errorSD = false;
  unsigned long microsSD2[2] = { 0, 0 };
  double valoresgy[10] = {};
  int contagy = 0;

  int contalog;
  bool init = true;
  roll = 0;

  SPI.begin();
  pinMode(IMU_CS, OUTPUT);
  digitalWrite(IMU_CS, HIGH);

  if (!myISM.begin(IMU_CS)) {
    Serial.println(F("IMU did not begin."));
  }

  myISM.deviceReset();

  while (!myISM.getDeviceReset()) {
    delay(1);
  }

  Serial.println(F("IMU has been reset."));
  Serial.println(F("Applying settings..."));
  delay(100);

  myISM.setDeviceConfig();
  myISM.setBlockDataUpdate();

  myISM.setAccelDataRate(ISM_XL_ODR_104Hz);
  myISM.setAccelFullScale(ISM_4g);


  myISM.setGyroDataRate(ISM_GY_ODR_104Hz);
  myISM.setGyroFullScale(ISM_125dps);

  myISM.setAccelFilterLP2();
  myISM.setAccelSlopeFilter(ISM_LP_ODR_DIV_100);

  myISM.setGyroFilterLP1();
  myISM.setGyroLP1Bandwidth(ISM_XTREME);  // ISM_MEDIUM ISM_XTREME

  delay(100);
  Serial.println(F(" "));

  // Estimate the gyro bias from stationary startup samples.
  for (int i = 0; i < 50; i++) {
    if (myISM.checkStatus()) {

      myISM.getAccel(&accelData);
      myISM.getGyro(&gyroData);

      gx = gyroData.xData;
      gy = gyroData.yData;
      gz = gyroData.zData;

      ax = accelData.xData;
      ay = accelData.yData;
      az = accelData.zData;

      gxzero = gxzero + gx;
      gyzero = gyzero + gy;
      gzzero = gzzero + gz;
      calibrationSamples++;
    }
    delay(10);
  }

  if (calibrationSamples > 0) {
    gxzero /= calibrationSamples;
    gyzero /= calibrationSamples;
    gzzero /= calibrationSamples;
  }

  if (hayreset) {
    gxzero = 472;
    gyzero = -154;
    gzzero = -10;
  }

  Serial.println("");
  Serial.print("zeros ");
  Serial.print(gxzero);
  Serial.print(" ");
  Serial.print(gyzero);
  Serial.print(" ");
  Serial.println(gzzero);

  roll = -atan(ax / az) * 360 / 6.28;
  pitch = atan(ay / az) * 360 / 6.28;
  yaw = 0;

  Serial.println("IMU inicializada");
  Serial.print("Roll inicial: ");
  Serial.println(roll);

  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  contalog = 0;
  timeant = micros() - 20000;  //para que la primera lectura salga 20k

  preferences.begin("k", true);  // aqui carga todos los parametros de la epprom

  k1 = preferences.getFloat("1", 1.15);
  k2 = preferences.getFloat("2", 2.05);
  k3 = preferences.getFloat("3", 0.75f);
  k4 = preferences.getFloat("4", 1100);
  anular = preferences.getInt("6", 0);

  preferences.end();

  preferences.begin("fisica", true);
  d1 = preferences.getFloat("d1", 0.34311077f);
  coeff = preferences.getFloat("coeff", 54.8466f);
  alpha = preferences.getFloat("alpha", 46.77157f);
  alphav = preferences.getFloat("alphav", 49.77157f);  // Estimación: Alfa + 3°; el Excel indica +2° a +4°
  s = preferences.getFloat("s", 470.0f);
  preferences.end();

  // Load persisted stability parameters.
  for (;;) {
    usciclo[contalog] = micros() - timeant;
    timeant = micros();


    if (Serial.available() > 0) {
      entradaserie = static_cast<uint8_t>(Serial.read());
    }

    if (entradaserie != 0) {
      const uint8_t command = entradaserie;
      entradaserie = 0;

      switch (command) {
        case 149:
          enqueueTask3Message("Enviar fechahora");
          break;

        case 150:
          // Preserve the established response layout for connected clients.
          Serial.println(0);
          Serial.println(k1);
          Serial.println(k2);
          Serial.println(k3);
          Serial.println(k4);
          Serial.println(0);
          Serial.println(anular);
          break;

        case 151:
          Serial.println(d1);
          Serial.println(coeff);
          Serial.println(alpha);
          Serial.println(alphav);
          Serial.println(s);
          break;

        case 152:
          Serial.println(roll);
          Serial.println(pitch);
          Serial.println(ax);
          Serial.println(ay);
          Serial.println(gz);
          break;

        case 200: {
          uint8_t payload[7] = {};
          if (Serial.readBytes(reinterpret_cast<char *>(payload), sizeof(payload)) != sizeof(payload)) {
            Serial.println("Error: trama incompleta para el comando 200");
            break;
          }

          // Keep the established frame layout; removed fields are ignored.
          k1 = payload[1] / 100.0f;
          k2 = payload[2] / 100.0f;
          k3 = payload[3] / 100.0f;
          k4 = payload[4] * 10.0f;
          anular = payload[6];

          preferences.begin("k", false);
          preferences.putFloat("1", k1);
          preferences.putFloat("2", k2);
          preferences.putFloat("3", k3);
          preferences.putFloat("4", k4);
          preferences.putInt("6", anular);
          preferences.end();
          break;
        }

        case 201: {
          uint8_t payload[10] = {};
          if (Serial.readBytes(reinterpret_cast<char *>(payload), sizeof(payload)) != sizeof(payload)) {
            Serial.println("Error: trama incompleta para el comando 201");
            break;
          }

          d1 = payload[0] + payload[1] / 100.0f;
          coeff = payload[2] + payload[3] / 100.0f;
          alpha = payload[4] + payload[5] / 100.0f;
          alphav = payload[6] + payload[7] / 100.0f;
          s = (payload[8] + payload[9] / 100.0f) * 100.0f;

          preferences.begin("fisica", false);
          preferences.putFloat("d1", d1);
          preferences.putFloat("coeff", coeff);
          preferences.putFloat("alpha", alpha);
          preferences.putFloat("alphav", alphav);
          preferences.putFloat("s", s);
          preferences.end();
          break;
        }

        case 202: {
          char dateTime[30] = {};
          const size_t length = Serial.readBytesUntil('\n', dateTime, sizeof(dateTime) - 1);
          dateTime[length] = '\0';
          enqueueTask3Message("Recibir fechahora");
          enqueueTask3Message(dateTime);
          break;
        }

        default:
          break;
      }
    }
    xQueuePeek(Q4, microsSD2, 0);
    if (microsSD2[1] == 1) {
      errorSD = true;
    }

    if (myISM.checkStatus()) {  //espera a que los sensores estén actualizados

      contalog++;
      myISM.getAccel(&accelData);
      myISM.getGyro(&gyroData);

      gx = gyroData.xData - gxzero;
      gy = gyroData.yData - gyzero;
      gz = gyroData.zData - gzzero;

      ax = accelData.xData;
      ay = accelData.yData;
      az = accelData.zData;

      theta = -pitch * 6.28 / 360;
      phi = -roll * 6.28 / 360;

      if (!init) {

        yaw = yaw + ((gx * (sin(phi) / cos(theta)) + gz * (cos(phi) / cos(theta))) * TASK_PERIOD_MS / 1000000) * usciclo[contalog] / 15000;
        pitch = pitch + ((gx * cos(phi) - gz * sin(phi)) * TASK_PERIOD_MS / 1000000) * usciclo[contalog] / 15000;
        roll = roll + ((gy + gx * (sin(phi) * sin(theta) / cos(theta)) + gz * (cos(phi) * sin(theta) / cos(theta))) * TASK_PERIOD_MS / 1000000) * usciclo[contalog] / 15000;
      }
      rolla = -atan(ax / az) * 360 / 6.28;

      double accmag = sqrt(ax * ax + ay * ay + az * az);

      if (accmag < 1030 && accmag > 980) {
        diferencia = roll - rolla;
        if (diferencia > 60) {
          diferencia = 60;
        }
        if (diferencia < -60) {
          diferencia = -60;
        }

        roll = roll - 0.0015 * diferencia;

        pitcha = atan(ay / az) * 360 / 6.28;

        diferenciap = pitch - pitcha;
        if (diferenciap > 60) {
          diferenciap = 60;
        }
        if (diferenciap < -60) {
          diferenciap = -60;
        }
        pitch = pitch - 0.0015 * diferenciap;
      }

      valoresgy[contagy] = gy;
      contagy++;
      if (contagy > 9) {
        contagy = 0;
      }

      float gyavg = 0;

      for (int i = 0; i < 10; i++) {
        gyavg = gyavg + valoresgy[i];
      }

      gyavg = gyavg / 10;

      h = sqrt(sq(d1 * 1000) - sq(s / 2));
      phi1crit = atan((s / 2) / h);
      phi1 = abs(atan(ax / az));
      float wcrit = sqrt(coeff * s / 1000 * alphav / 4) * 360 / 6.28 * 1000;
      SI = 1 - k1 * (phi1 / phi1crit) - k2 * (gyavg / wcrit) * (gyavg / wcrit);
      if (contalog == 5) {  // dispara tarea sd

        char SIchar[12];
        contasd++;
        contalog = 0;
        if (initcabecera) {
          contasd++;
          // Keep the CSV schema stable; external acceleration is no longer available, so its age stays zero.
          strcpy(texto, "ax; ay; az; gx; gy; gz; roll; pitch; yaw; external_accel_age_us; usciclo1; usciclo2; usciclo3; usciclo4; usciclo5; si; accmag; microsds; k3");
          strcat(texto, "\n");
          initcabecera = false;
        }
        dtostrf(ax, 6, 2, SIchar);
        if (contasd == 1) {
          strcpy(texto, SIchar);
        } else {
          strcat(texto, SIchar);
        }

        strcat(texto, "; ");
        dtostrf(ay, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(az, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(gx, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(gy, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(gz, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(roll, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(pitch, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(yaw, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(0, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(usciclo[0], 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(usciclo[1], 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");  //tmed
        dtostrf(usciclo[2], 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");  //coeffsi
        dtostrf(usciclo[3], 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(usciclo[4], 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");  //signo
        dtostrf(SI, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");  //signo
        dtostrf(accmag, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(microsSD2[0], 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        dtostrf(k3, 6, 2, SIchar);
        strcat(texto, SIchar);
        strcat(texto, "; ");
        strcat(texto, "\n");
        if (contasd == 10) {
          contasd = 0;
          enqueueTask3Message(texto);
        }
      }

    }  //fin de los sensores actualizados
    init = false;
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_MS));
  }

}  // Infinite loop


void Task3(void *pvParameters) {  //sd
  unsigned long timeant;
  char mensajerecibido[LOG_BUFFER_SIZE];
  bool errorSD;
  bool errorRTC;
  char SIchar2[12];
  char texto3[800];
  unsigned long microsSD[2] = { 0, 0 };

  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
    errorSD = true;
  } else {
    errorSD = false;
    Serial.println("Card Mount OK");
  }

  // Continue SD initialization only when the card is available.
  if (!errorSD) {

    uint8_t cardType = SD_MMC.cardType();

    if (cardType == CARD_NONE) {
      Serial.println("No SD_MMC card attached");
      errorSD = true;
    }

    Serial.print("SD_MMC Card Type: ");
    if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);
  }

  errorRTC = false;
  if (rtc.begin() == false) {
    Serial.println("Error RTC");
    errorRTC = true;
  } else {
    Serial.println("RTC online!");
    if (rtc.updateTime() == true) {

      currentDate = rtc.stringDate();
      currentTime = rtc.stringTime();
    }
  }

  preferences.begin("my-app", false);
  numarchivo = preferences.getULong("numarchivo", 0);
  fileDate = preferences.getString("fileDate", "01/01/2020");
  sessionNumber = preferences.getULong("sessionNumber", 0);
  preferences.end();

  if (fileDate != currentDate) {
    numarchivo++;
    sessionNumber = 0;
    fileDate = currentDate;
    preferences.begin("my-app", false);
    preferences.putULong("numarchivo", numarchivo);
    preferences.putString("fileDate", fileDate);
    preferences.putULong("sessionNumber", sessionNumber);
    preferences.end();
  } else {
    sessionNumber++;
    preferences.begin("my-app", false);
    preferences.putULong("sessionNumber", sessionNumber);
    preferences.end();
  }


  dtostrf(numarchivo, 6, 0, SIchar2);
  strcpy(texto3, SIchar2);
  strcat(texto3, "; ");
  strcat(texto3, currentDate.c_str());
  strcat(texto3, "; ");
  strcat(texto3, currentTime.c_str());
  strcat(texto3, "; ");
  dtostrf(sessionNumber, 6, 0, SIchar2);
  strcat(texto3, SIchar2);
  strcat(texto3, "; ");
  strcat(texto3, "\n");

  if (!errorSD) {
    freemem = (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024);
    appendFile(SD_MMC, "/tabla.txt", texto3);

    dtostrf(numarchivo, 6, 0, SIchar2);
    strcpy(texto3, "/");
    strcat(texto3, SIchar2);
    strcat(texto3, "_ESTABILIDAD_");
    strcat(texto3, DEVICE_ID);
    strcat(texto3, "_");
    currentDate.replace("/", "-");
    strcat(texto3, currentDate.c_str());
    currentDate.replace("-", "/");
    strcat(texto3, ".txt");

    for (int i = 0; i < 6; i++) {
      if (texto3[i] == ' ') {
        texto3[i] = '0';
      }
    }

    strcpy(mensajerecibido, "ESTABILIDAD;");
    strcat(mensajerecibido, currentDate.c_str());
    strcat(mensajerecibido, " ");
    strcat(mensajerecibido, currentTime.c_str());
    strcat(mensajerecibido, ";");
    strcat(mensajerecibido, DEVICE_ID);
    strcat(mensajerecibido, ";");
    dtostrf(numarchivo, 6, 0, SIchar2);
    char *trimmedStr = SIchar2;
    while (*trimmedStr == ' ') {
      trimmedStr++;
    }
    strcat(mensajerecibido, trimmedStr);
    strcat(mensajerecibido, ";");
    dtostrf(sessionNumber, 6, 0, SIchar2);
    trimmedStr = SIchar2;
    while (*trimmedStr == ' ') {
      trimmedStr++;
    }
    strcat(mensajerecibido, trimmedStr);
    strcat(mensajerecibido, ";");
    strcat(mensajerecibido, "\n");

    appendFile(SD_MMC, texto3, mensajerecibido);
    Serial2.println(mensajerecibido);
    Serial.println(mensajerecibido);
  }

  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {

    if (xQueueReceive(Q3, mensajerecibido, pdMS_TO_TICKS(10000)) != pdTRUE) {
      continue;
    }
    timeant = micros();

    if (strcmp(mensajerecibido, "Enviar fechahora") == 0) {
      Serial.println(currentTime);
      Serial.println(currentDate);
      Serial.println(numarchivo);
      Serial.println(freemem);

    } else if (strcmp(mensajerecibido, "Recibir fechahora") == 0) {  //recibe fecha y hora para configurar rtc
      if (xQueueReceive(Q3, mensajerecibido, pdMS_TO_TICKS(10000)) == pdTRUE) {
        int year, month, day, hour, minute, second;
        if (xSemaphoreTake(ptrMutex, portMAX_DELAY) == pdTRUE) {
          if (sscanf(mensajerecibido, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6 && !errorRTC) {
            vTaskDelay(10);
            rtc.setYear(year);
            rtc.setMonth(month);
            rtc.setDate(day);
            rtc.setHours(hour);
            rtc.setMinutes(minute);
            rtc.setSeconds(second);
          }
          xSemaphoreGive(ptrMutex);
        }
      }
    } else if (!errorSD) {
      appendFile(SD_MMC, texto3, mensajerecibido);  //ESTA ES LA LINEA QUE ESCRIBE EN LA SD
      Serial2.println(mensajerecibido);
      Serial.println(mensajerecibido);
      if (xSemaphoreTake(ptrMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (!errorRTC && rtc.updateTime()) {
          currentTime = rtc.stringTime();
          currentDate = rtc.stringDate();

        }
        xSemaphoreGive(ptrMutex);
      }
      strcpy(mensajerecibido, currentTime.c_str());
      strcat(mensajerecibido, "\n");

      appendFile(SD_MMC, texto3, mensajerecibido);
      Serial2.println(mensajerecibido);
      Serial.println(mensajerecibido);
    }

    microsSD[0] = micros() - timeant;

    microsSD[1] = errorSD ? 1 : 0;

    xQueueOverwrite(Q4, microsSD);

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TASK_PERIOD_MS));
  }  // Infinite loop
}
