//BASADO EN sur18
//intento vuelco frontal
//se modifica a 300000 el recibodatoswifi para spnow
//vuelco en dos sentidos
// k4 para distancia entre ejes
//servidor ok


#define EN_3V3_SW 32  // The 3.3V_SW regulator Enable pin is connected to D32
//#include <CAN.h>
#include "FS.h"
#include "SD_MMC.h"
#include "Qwiic_LED_Stick.h"
#include <SPI.h>
#include "SparkFun_ISM330DHCX.h"
#include <SparkFun_RV8803.h>
#include <Preferences.h>
//#include <SparkFun_Qwiic_OLED.h>

#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include "WiFi.h"
#include <esp_now.h>
//#include "driver/twai.h"
#include <ESPAsyncWebSrv.h>
#include <ESP32_FTPClient.h>

//QwiicNarrowOLED myOLED;
Preferences preferences;

#define EMPTY_FLOAT -1000.0  //para dongle

#define MAX_WIFI_NETWORKS 5

#define serverPort 9000

#define RXD2 35
#define TXD2 33

String timeDateString = "";

RV8803 rtc;
LED LEDStick;

//v2.1
String deviceId = "DEV001";  // ID por defecto, se actualizará desde config.csv

File configFile;
String configFileName = "config.csv";

const char *ssid;  // Asignamos un tamaño adecuado
const char *password;

bool errorLED = false;

//aqui tenia los taskhandle en el 07
//pero creo que no hacen falta
//asi que los quito

int estiloleds = 1;
int anular = 1;
char SIchar2[12];
String success;

struct WiFiConfig {
  String ssid;
  String password;
  bool configurada;
};

struct ServerConfig {
  String url;
  int port;
  String username;
  String password;
};

ServerConfig serverConfig;
WiFiConfig redes[MAX_WIFI_NETWORKS];
int numIntentos = 2;

class SimpleFileUploader {
private:
  char serverAddress[128];
  char username[32];
  char password[32];
  ESP32_FTPClient *ftp;
  bool isConnected;
  bool deviceFolderCreated;
  bool primeraConexion;

public:
  SimpleFileUploader() {
    // Copiar las cadenas a buffers mutables
    strncpy(serverAddress, serverConfig.url.c_str(), sizeof(serverAddress) - 1);
    serverAddress[sizeof(serverAddress) - 1] = '\0';

    strncpy(username, serverConfig.username.c_str(), sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';

    strncpy(password, serverConfig.password.c_str(), sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';

    // Crear el cliente FTP después de tener los buffers inicializados
    ftp = new ESP32_FTPClient(serverAddress, serverConfig.port, username, password, 1000, 2);
    isConnected = false;
    deviceFolderCreated = false;
    primeraConexion = false;
  }

  ~SimpleFileUploader() {
    if (ftp) {
      if (isConnected) {
        ftp->CloseConnection();
      }
      delete ftp;
      ftp = nullptr;
    }
  }

  bool connect() {
    if (!ftp) return false;  // Ensure FTP client is initialized

    if (!primeraConexion) {
      ftp->OpenConnection();
      ftp->InitFile("Type A");
      primeraConexion = true;
    }

    if (ftp->isConnected()) {
      isConnected = true;
      return isConnected;
    }

    isConnected = false;
    return isConnected;  // Connection failed
  }

  void close() {
    if (ftp && isConnected) {
      ftp->CloseConnection();
      isConnected = false;
    }
  }

  bool directoryExists(const char *dir) {
    String list[128];            // Create an empty String to store the list of directory contents
    ftp->ContentList("", list);  // List the contents of the root directory or a specific directory

    // Iterate over the content list and check if any entry matches the directory name
    for (int i = 0; i < 128; i++) {
      if (list[i].length() > 0 && list[i].indexOf(dir) != -1) {
        return true;  // Directory found in the list
      }
    }
    return false;  // Directory not found
  }

  bool ensureDeviceFolder() {
    if (deviceFolderCreated) return true;

    preferences.begin("folder", true);
    deviceFolderCreated = preferences.getBool("folderCreated", false);
    preferences.end();

    if (deviceFolderCreated) {
      ftp->ChangeWorkDir(deviceId.c_str());
      Serial.printf("Carpeta del dispositivo seleccionada: %s\n", deviceId);
    } else {
      ftp->MakeDir(deviceId.c_str());
      delay(100);  // Wait a bit to ensure the directory is created
      ftp->ChangeWorkDir(deviceId.c_str());
      deviceFolderCreated = true;
      preferences.begin("folder", false);
      preferences.putBool("folderCreated", deviceFolderCreated);
      preferences.end();

      Serial.printf("Carpeta del dispositivo creada y seleccionada: %s\n", deviceId);
    }

    if (connect()) {
      return true;
    } else {
      return false;
    }
  }

  bool uploadFile(const char *filename) {
    if (!SD_MMC.exists(filename)) {
      Serial.printf("El archivo %s no existe en la SD\n", filename);
      return false;
    }

    if (!connect() || !ensureDeviceFolder()) {
      return false;
    }

    File file = SD_MMC.open(filename);
    if (!file) {
      Serial.println("No se pudo abrir el archivo");
      return false;
    }

    // Obtener solo el nombre del archivo sin la ruta
    String baseName = String(filename);
    int lastSlash = baseName.lastIndexOf('/');
    if (lastSlash >= 0) {
      baseName = baseName.substring(lastSlash + 1);
    }

    // Crear nuevo archivo y subir
    ftp->InitFile("Type A");
    ftp->NewFile(baseName.c_str());

    Serial.println("Subiendo archivo...");
    while (file.available()) {
      uint8_t buf[128];
      int bytesRead = file.read(buf, sizeof(buf));
      ftp->WriteData(buf, bytesRead);
    }

    ftp->CloseFile();
    file.close();

    if (connect()) {
      //Serial.println("Archivo subido exitosamente");
      return true;
    } else {
      //Serial.println("Error al subir archivo");
      return false;
    }
  }
};

SimpleFileUploader uploader;

// Mutex para el uso de I2C entre tareas
static SemaphoreHandle_t ptrMutex = xSemaphoreCreateMutex();

// ************************************************* DEFINICIONES *************************************************

AsyncWebServer server(serverPort);
String html;

//****************************** DEFINIMOS HTML PARA EL SERVIDOR *************************//
// Función para decodificar URL
const char *htmlCode = R"(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>DOBACK SERVER</title>

  <style>

        .sidenav {
          height: 100%;
          width: 160px;
          position: fixed;
          z-index: 1;
          top: 0;
          left: 0;
          background-color: #111;
          overflow-x: hidden;
          padding-top: 20px;
          overflow-y: auto; 
        }

        .sidenav a.active {
            background-color: white;
            color: #111;
        }

        .sidenav a.active:hover {
            color: #818181;
        }

        .sidenav a {
          padding: 6px 8px 6px 16px;
          text-decoration: none;
          font-size: 14px;
          color: #818181;
          display: block;
        }

        .sidenav a:hover {
          color: #f1f1f1;
        }

        .main {
          margin-left: 160px; 
          font-size: 16px; 
          padding: 0px 10px;
        }

        @media screen and (max-height: 450px) {
          .sidenav {padding-top: 15px;}
          .sidenav a {font-size: 18px;}
        }

        body {
          background-color: #053F66;
          color: #053F66;
          margin: 0;
          font-family: Cambria;
        }

        .button{
          background-color: #053F66;
        }

        .spacer{
          margin-bottom: 20px;
        }


        .center{
          text-align: center;
          color: white;
          margin: auto;

        }

        .previous, .next {
            background-color: #191a1939;
            color: while;
            display: inline-block;
            padding: 6px 8px; 
            margin: 0 5px; 
        }

        .previous:hover, .next:hover {
            background-color: #f0f0f0;
            color: #111; 
        }

        .centered {
          text-align: center;
        }

        h1{
          font-size: 48px;
        }

        .file-list {
            column-count: 2;
            column-gap: 10px;
            margin-left: -30px;
        }

        .file-list-item {
            margin-bottom: 10px;
            display: flex; 
            justify-content: space-between; 
        }

        .button-container {
            display: flex; 
        }

        .button-container button {
            margin-left: 10px; 
        }

        li {
          background-color: #f0f0f0; /* Gris clarito */
          border: 2px solid #d2d2d2; /* Borde gris claro */
          margin-bottom: 10px;
          padding: 15px;
          border-radius: 5px;
          box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
          transition: box-shadow 0.3s ease; /* Agregar transición al sombreado */
          flex: 1 0 calc(30% - 20px); /* 30% de ancho con espacio entre columnas */
        }

  </style>

</head>
<body>
    <div class="main"> 
      <h2></h2>
      <h1 href="#" class="center spacer">Doback Server</h1>
      <h2></h2>
      <h2 href="#" class="center spacer">Archivos</h2>
      <h2></h2> 
      
)";

//******************************** FUNCIONES **********************************************//

//broad

//uint8_t broadcastAddress[] = { 0x94, 0xE6, 0x86, 0xB6, 0x8E, 0xD8 }; //bateria
//uint8_t broadcastAddress2[] = { 0x94, 0xE6, 0x86, 0xB8, 0xA3, 0xF8 }; //aluminio
//uint8_t broadcastAddress2[] = { 0xE8, 0x6B, 0xEA, 0xF6, 0x73, 0x2C };  //2
uint8_t broadcastAddress2[] = { 0xA0, 0xB7, 0x65, 0x67, 0x80, 0x74 };  //aqui manda los datos al dongle
//uint8_t broadcastAddress2[] = { 0xE4, 0x65, 0xB8, 0x77, 0x12, 0x0C };  //dongle 2
//uint8_t broadcastAddress[] = { 0x24, 0xDC, 0xC3, 0x4A, 0x79, 0x7C };   //pantallaca 24:DC:C3:4A:79:7C
uint8_t broadcastAddress[] = { 0x24, 0xDC, 0xC3, 0x49, 0x66, 0x0C };  //pantallaca 24:DC:C3:49:66:0C

typedef struct struct_message {
  float m1;
  float m2;
  float m3;
  float m4;
  float m5;
  float m6;
  char char1[3];
  char char2[12];
} struct_message;
struct_message BME280Readings;
struct_message incomingReadings;
struct_message outgoing;
esp_now_peer_info_t peerInfo;

int msi = 20;  //milisegundos de bucle de cálculo imu
int contasd = 0;
//#define IMU_CS SS // The ISM330 chip select is connected to D5
uint8_t IMU_CS = 5;

// SPI instance class call
SparkFun_ISM330DHCX_SPI myISM;

// Structs for X,Y,Z data
sfe_ism_data_t accelData;
sfe_ism_data_t gyroData;

//declaraciones varias
unsigned long timeant;

bool initcabecera = true;

float rolla;

float pitcha;

float yawa;

float espnowID;
float incomingax;
float incomingay;
float incomingaz;
char char1[3];
char char2[12];

float diferencia;
float diferenciap;
float psi;
float theta;
float phi;
float rpm;
int candata1;
float freemem;
//double speed;


int colorled1;
int colorled2;
int colorled3;
int chivatoColor1;
int chivatoColor2;
int chivatoColor3;
int vuelco;
int desbordes = 0;
int hits2;
int misses2;
int vacios2;
int contapantallita;

bool globalerror = false;
static bool driver_installed = false;

bool errorPANTALLITA = false;
bool watchdog = false;
bool actualizader;

bool actualizarchivo;
bool iniciando = true;
bool iniciando2 = true;
bool recibodatoswifi = false;
bool archivosFTP = false;

unsigned long previousMillis = 0;
unsigned long chivatoStartTime = 0;
unsigned long timeant5;
unsigned long timeant6;
unsigned long timeant7;
unsigned long timeant8;
unsigned long timeant9;
unsigned long timeant11;
unsigned long timeantwifi;

String pantallita;
byte candatos[8];
byte candatos2[8];

String currentDate;
String currentTime;
unsigned long numarchivo;
String fileDate;
unsigned long sessionNumber;

//************************************************* declaraciones modulo indice de estabilidad *******************************************

float d1;
float coeff;
float alpha;
float alphav;
float s = 1100;  // semivia mm
float h = 1300;  // altura centro de gravedad mm
float phi1;
float phi1crit;
float phi1F;      //F significa frontal
float phi1critF;  //F significa frontal
float SI;
float SIF;  // indice de estabilidad frontal
// int i;
float k3;  // tanto por 1 de estabilidad remanente al encenderse la primera luz
float leds;
float ledsF;                  // tanto por 1 de leds encendidos
float remanencialeds = 0.05;  // cuanto leds puede apagarse por iteración (0.01 significa un 1% de la barra por cada 10 ms, es decir, una barra por segundo)
float ledsant;
int numleds = 11;  //numero de leds de la barra de leds
bool vuelcoF;


//************************************************* ------------------------------------------- *******************************************



//*************************************************             declaraciones logger            *******************************************

//String texto = "";

char texto[2500];
char textoespecial[800];
char texto2[800];
char nombrearchivo[800];  //donde vas. Bajar a 80
bool hayarchivo;
bool hayreset = false;

//************************************************* ------------------------------------------- *******************************************



//*************************************************             FUNCIONES                       *******************************************
void espnowenviar(float v1, float v2, float v3, float v4, float v5, float v6) {
  outgoing.m1 = v1;
  outgoing.m2 = v2;
  outgoing.m3 = v3;
  outgoing.m4 = v4;
  outgoing.m5 = v5;
  outgoing.m6 = v6;

  esp_now_send(broadcastAddress2, (uint8_t *)&outgoing, sizeof(outgoing));
}

// Callback when data is sent
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void OnDataSent(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
#else
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
  //Serial.print("\r\nLast Packet Send Status:\t");
  //Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  if (status == 0) {
    success = "Delivery Success :)";
  } else {
    success = "Delivery Fail :(";
  }
}

// Callback when data is received
#if ESP_IDF_VERSION_MAJOR >= 5
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *incomingData, int len) {
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
  //Serial.print("Bytes received: ");
  //Serial.print(len);
  //Serial.print(" ");
  espnowID = incomingReadings.m1;
  incomingax = incomingReadings.m2;
  incomingay = incomingReadings.m3;
  incomingaz = incomingReadings.m4;
  //strcpy(char1, incomingReadings.char1);      creo que no se utiliza
  //Serial.println(espnowID);
  timeantwifi = micros();
  recibodatoswifi = true;
}


// void escribePANTALLITA(String mensaje) {

//   myOLED.erase();
//   int x0 = (myOLED.getWidth() - myOLED.getStringWidth(mensaje)) / 2;
//   int y0 = (myOLED.getHeight() - myOLED.getStringHeight(mensaje)) / 2;
//   myOLED.text(x0, y0, mensaje, 2);
//   myOLED.display();
// }

bool readFile(fs::FS &fs, const char *path) {
  bool hayarchivo;
  //Serial.printf("Reading file: %s\n", path);
  hayarchivo = true;
  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    hayarchivo = false;
    return hayarchivo;
  }
  return hayarchivo;
  //Serial.print("Read from file: ");
  //while(file.available()){
  //    Serial.write(file.read());
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    //  Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
}

void deleteFile(String filename) {
  SD_MMC.remove(filename);
}

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  static File uploadFile;

  if (!index) {
    uploadFile = SD_MMC.open("/" + filename, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("Failed to open file for writing");
      return request->send(500, "text/plain", "Failed to open file for writing");
    }
  }

  if (uploadFile.write(data, len) != len) {
    Serial.println("Write error during file upload");
    return request->send(500, "text/plain", "Write error during file upload");
  }

  if (final) {
    uploadFile.close();
    Serial.printf("Upload finished: %s, size: %u\n", filename.c_str(), index + len);
    request->send(200, "text/plain", "File uploaded successfully");
  }
}

void appendFile(fs::FS &fs, const char *path, const char *message) {

  //Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    // Serial.println("Message appended");
    file.close();
  } else {
    Serial.println("Append failed");
  }
}

bool sendFileToServer(const char *filename) {


  if (!filename) {
    Serial.println("Error: Nombre de archivo nulo");
    return false;
  }

  String fullPath = filename[0] == '/' ? String(filename) : String("/") + filename;

  // Intentar subir el archivo
  bool success = uploader.uploadFile(fullPath.c_str());

  if (success) {
    registerSentFile(fullPath.c_str());
  }

  return success;
}

// Función para verificar si un archivo ya fue enviado
bool wasFileSent(const char *filename) {
  // Abrir archivo de registro
  File sentLog = SD_MMC.open("/sent_files.txt", FILE_READ);
  if (!sentLog) {
    return false;  // Si no existe el archivo, asumimos que no se ha enviado
  }

  // Buscar el nombre del archivo en el registro
  bool found = false;
  while (sentLog.available()) {
    String line = sentLog.readStringUntil('\n');
    line.trim();
    // Remove the leading slash from the log line (if it exists)
    if (line.startsWith("/")) {
      line = line.substring(1);  // Remove the first character (slash)
    }
    if (line.equals(String(filename))) {
      found = true;
      break;
    }
  }
  sentLog.close();
  return found;
}

// Función para registrar un archivo enviado
void registerSentFile(const char *filename) {
  File sentLog = SD_MMC.open("/sent_files.txt", FILE_APPEND);
  if (sentLog) {
    sentLog.println(filename);
    sentLog.close();
  }
}

bool validateWiFiConfig() {
  Serial.println("\nValidando configuración WiFi:");
  bool hasValidNetwork = false;
  int validNetworks = 0;

  // Primero validamos la configuración de todas las redes
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
    if (redes[i].configurada) {
      Serial.printf("Red %d - SSID: %s, Longitud contraseña: %d\n",
                    i + 1,
                    redes[i].ssid.c_str(),
                    redes[i].password.length());

      if (redes[i].ssid.length() == 0) {
        Serial.printf("Error: SSID vacío en red %d\n", i + 1);
        redes[i].configurada = false;
        continue;
      }

      if (redes[i].password.length() < 8) {
        Serial.printf("Error: Contraseña demasiado corta en red %d\n", i + 1);
        redes[i].configurada = false;
        continue;
      }

      hasValidNetwork = true;
      validNetworks++;
    }
  }

  if (!hasValidNetwork) {
    Serial.println("Error: No hay redes WiFi válidas configuradas");
    return false;
  }

  Serial.printf("Redes WiFi válidas encontradas: %d\n", validNetworks);

  // Intentar conectar a cada red configurada
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
    if (!redes[i].configurada) continue;

    Serial.printf("\nIntentando conectar a red %d: %s\n", i + 1, redes[i].ssid.c_str());
    WiFi.disconnect(true);  // Desconectar completamente
    delay(1000);            // Aumentar el delay después de desconectar
    WiFi.begin(redes[i].ssid.c_str(), redes[i].password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < numIntentos) {
      delay(1000);
      Serial.print(".");
      attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("¡Conectado exitosamente a %s!\n", redes[i].ssid.c_str());
      Serial.printf("Dirección IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("Intensidad de señal: %d dBm\n", WiFi.RSSI());
      ssid = redes[i].ssid.c_str();
      password = redes[i].password.c_str();
      return true;  // Solo retornamos true si realmente nos conectamos
    } else {
      Serial.printf("No se pudo conectar a %s después de %d intentos\n",
                    redes[i].ssid.c_str(),
                    numIntentos);

      if (i < MAX_WIFI_NETWORKS - 1 && redes[i + 1].configurada) {
        Serial.println("Intentando con la siguiente red...");
      }
    }
  }

  // Si llegamos aquí, no pudimos conectarnos a ninguna red
  Serial.println("\nNo se pudo conectar a ninguna red WiFi configurada");
  return false;
}

bool validateServerConfig() {
  Serial.println("\nValidando configuración del servidor FTP...");

  // Verificar que los campos requeridos estén configurados
  if (serverConfig.url.length() == 0) {
    Serial.println("Error: URL del servidor no configurada");
    return false;
  }

  if (serverConfig.port <= 0 || serverConfig.port > 65535) {
    Serial.println("Error: Puerto inválido");
    return false;
  }

  if (serverConfig.username.length() == 0) {
    Serial.println("Error: Usuario FTP no configurado");
    return false;
  }

  if (serverConfig.password.length() == 0) {
    Serial.println("Error: Contraseña FTP no configurada");
    return false;
  }

  // Verificar conexión WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Error: No hay conexión WiFi");
    return false;
  }

  Serial.println("\nEstado de la red WiFi:");
  Serial.printf("IP local: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("Fuerza de señal: %d dBm\n", WiFi.RSSI());

  Serial.println("\nConfiguración FTP:");
  Serial.printf("URL: %s\n", serverConfig.url.c_str());
  Serial.printf("Puerto: %d\n", serverConfig.port);
  Serial.printf("Usuario: %s\n", serverConfig.username.c_str());

  return true;
}

bool readConfigFile() {

  Serial.println("Intentando abrir: " + configFileName);

  // Intentar abrir el archivo
  configFile = SD_MMC.open("/" + configFileName, FILE_READ);
  if (!configFile) {
    Serial.println("Error: No se pudo abrir " + configFileName);
    Serial.println("Verificando permisos y ruta...");

    // Intentar crear el archivo para verificar permisos
    File testFile = SD_MMC.open("/" + configFileName, FILE_WRITE);
    if (!testFile) {
      Serial.println("Error: No se pueden crear archivos en la SD");
    } else {
      testFile.close();
      SD_MMC.remove("/" + configFileName);
      Serial.println("Permisos de escritura OK");
    }

    return false;
  }

  Serial.println("Archivo de configuración abierto correctamente");
  Serial.println("Tamaño del archivo: " + String(configFile.size()) + " bytes");

  String section = "";

  // Leer el archivo línea por línea con diagnóstico
  while (configFile.available()) {
    String line = configFile.readStringUntil('\n');
    line.trim();

    // Ignorar líneas vacías y comentarios
    if (line.length() == 0 || line.startsWith("#")) {
      if (line.startsWith("#")) {
        section = line.substring(2);
        Serial.println("Procesando sección: " + section);
      }
      continue;
    }

    // Procesar la línea
    int commaIndex = line.indexOf(',');
    if (commaIndex == -1) {
      Serial.println("Advertencia: Línea mal formateada: " + line);
      continue;
    }

    String key = line.substring(0, commaIndex);
    String value = line.substring(commaIndex + 1);
    key.trim();
    value.trim();

    Serial.println("Leyendo: " + key + " = " + value);

    if (section == "CONFIGURACIÓN GENERAL") {
      if (key == "DEVICE_ID") {
        deviceId = value;
        Serial.println("ID del dispositivo configurado: " + deviceId);
      }
    } else if (section == "CONFIGURACIÓN WIFI") {
      if (key.startsWith("WIFI_")) {
        // Extraer el número después de WIFI_
        String numStr = key.substring(5);
        int netIndex = numStr.toInt() - 1;
        if (netIndex >= 0 && netIndex < MAX_WIFI_NETWORKS) {
          Serial.print("Procesando red WiFi ");
          Serial.print(netIndex + 1);
          Serial.print(": ");
          Serial.println(value);

          // Inicializar la red como no configurada
          redes[netIndex].configurada = false;

          // Verificar que el valor tenga el formato correcto (debe tener una coma)
          int commaIndex = value.indexOf(',');
          if (commaIndex != -1) {
            String ssid = value.substring(0, commaIndex);
            String password = value.substring(commaIndex + 1);

            // Validar SSID y password
            if (ssid.length() > 0 && password.length() >= 8) {
              redes[netIndex].ssid = ssid;
              redes[netIndex].password = password;
              redes[netIndex].configurada = true;

              Serial.print("  Red configurada - SSID: '");
              Serial.print(ssid);
              Serial.println("' (contraseña oculta)");
            } else {
              Serial.println("  Error: SSID vacío o contraseña demasiado corta");
            }
          }
        }
      }
      if (key == "WIFI_RETRY") {
        numIntentos = value.toInt();
        if (numIntentos <= 0) numIntentos = 2;
        Serial.printf("Número de intentos de conexión WiFi: %d\n", numIntentos);
      }
    }  // HTTP SERVER CONFIG
    else if (section == "CONFIGURACIÓN FTP") {
      if (key == "FTP_SERVER_URL") {
        serverConfig.url = value;
      } else if (key == "FTP_SERVER_PORT") {
        serverConfig.port = value.toInt();
      } else if (key == "FTP_USERNAME") {
        serverConfig.username = value;
      } else if (key == "FTP_PASSWORD") {
        serverConfig.password = value;
      }
    }
  }

  configFile.close();
  bool validar = validateWiFiConfig();
  if (validar) validateServerConfig();
  Serial.println("Archivo de configuración leído completamente");

  return validar;
}

void sendInitialFiles() {
  File root = SD_MMC.open("/");
  if (!root) {
    Serial.println("Error: No se pudo abrir el directorio raíz");
    return;
  }

  Serial.printf("Número de archivo más reciente: %04d\n", numarchivo);

  File file = root.openNextFile();
  int filesProcessed = 0;
  int filesSent = 0;

  while (file) {
    String fname = String(file.name());

    // Procesar solo archivos CSV y TXT, excluyendo sent_files.txt
    if ((fname.endsWith(".csv") || fname.endsWith(".txt")) && !fname.equals("sent_files.txt")) {
      // Extraer el número del archivo actual
      int currentFileNum = -1;
      int underscorePos = fname.indexOf('_');
      if (underscorePos > 0) {
        currentFileNum = fname.substring(1, underscorePos).toInt();
      }

      // No enviar si es el archivo más reciente
      if (currentFileNum == numarchivo) {
        Serial.printf("Saltando archivo más reciente: %s\n", fname.c_str());
        file = root.openNextFile();
        continue;
      }

      filesProcessed++;

      // Verificar si el archivo ya fue enviado
      if (!wasFileSent(fname.c_str())) {
        Serial.printf("\nEnviando archivo: %s (%d bytes)\n", fname.c_str(), file.size());
        if (sendFileToServer(fname.c_str())) {
          Serial.printf("Archivo %s enviado exitosamente\n", fname.c_str());
          filesSent++;
        } else {
          Serial.printf("Error al enviar archivo %s\n", fname.c_str());
        }
      } else {
        Serial.printf("Archivo %s ya fue enviado previamente\n", fname.c_str());
      }
    }
    file = root.openNextFile();
  }
  uploader.close();

  Serial.printf("\nResumen de envío inicial: %d archivos procesados, %d enviados exitosamente\n",
                filesProcessed, filesSent);

  root.close();
}

double tipomensaje1[3];  //este es mi mensaje típico para colas, que tiene tres dobles (si lo cambias aquí, hay que cambiarlo tambien en todas las tasks
unsigned long tipomensaje2[2];
//double tipomensaje3[4];
//declara tasks, recuerda crearla al final del setup
void Task1(void *pvParameters);  //fisica
//void Task2(void *pvParameters);  //can
void Task3(void *pvParameters);  //sd

//declara queues, recuerda crearla al inicio del setup
//QueueHandle_t Q1;  // cola para mensajes can a fisica
//QueueHandle_t Q2;  // cola para mensajes fisica a can
QueueHandle_t Q3;  // cola para fisica a sd
QueueHandle_t Q4;  //sd a fisica
QueueHandle_t Q5;  //sd a fisica 2

//******************************************************************************************************************************************************
//*************************************************                   SETUP                    *********************************************************
//******************************************************************************************************************************************************

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

  pinMode(EN_3V3_SW, OUTPUT);  // Enable power for the microSD card
  digitalWrite(EN_3V3_SW, HIGH);

  pinMode(25, OUTPUT);
  digitalWrite(25, HIGH);  //empieza a pitar

  pinMode(26, OUTPUT);  // 3 pines para seleccion de idioma
  digitalWrite(26, HIGH);

  pinMode(16, OUTPUT);
  digitalWrite(16, HIGH);

  pinMode(17, OUTPUT);
  digitalWrite(17, HIGH);

  //Q1 = xQueueCreate(1, sizeof(tipomensaje1));  //can a fisica
  //Q2 = xQueueCreate(1, sizeof(tipomensaje1));  //fisica a can
  Q3 = xQueueCreate(5, sizeof(texto));         //fisica a sd
  Q4 = xQueueCreate(1, sizeof(tipomensaje2));  //sd a fisica
  Q5 = xQueueCreate(3, sizeof(outgoing));

  delay(250);

  if (LEDStick.begin() == false) {
    Serial.println("Qwiic LED Stick failed to begin. Please check wiring and try again!");
    errorLED = true;
  }
  if (!errorLED) {
    for (int i = 0; i < 10; i++) {
      LEDStick.setLEDColor(i, 0, 0, 32);
      delay(20);
    }
  }

  xTaskCreatePinnedToCore(Task1, "Task Fisica", 20000, NULL, 4, NULL, 0);  //fisica
  //xTaskCreatePinnedToCore(Task2, "Task Can", 20000, NULL, 3, NULL, 0);     //can
  //xTaskCreate(Task2, "Task Can", 20000, NULL, 3, NULL);                //can
  xTaskCreatePinnedToCore(Task3, "Task Sd", 20000, NULL, 4, NULL, 1);  //sd

  digitalWrite(25, LOW);  //deja de pitar
}

void loop() {
  vTaskDelete(NULL);
}

/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

void Task1(void *pvParameters) {  //fisica

  while (!archivosFTP) {
    vTaskDelay(2);
  }

  byte entradaserie = 0;
  unsigned long timeant;
  unsigned long tiempociclo;
  unsigned long i;
  double mensajeFAC[3];
  double mensajerecibido[3];

  bool errorIMU = false;
  float gx;
  float gy;
  float gz;
  float ax;
  float axant;
  float ay;
  float az;
  float roll;
  float pitch;
  float yaw;
  float gzzero;
  float gyzero;
  float gxzero;
  float diag = 0;
  float k1 = 1.15;
  float k2 = 2.05;
  float k3 = 1;
  float k4 = 1100;
  int asonora = 5;

  int idioma = 1;  // 1-español, 2-ingles, 3-frances, 4-aleman , 5-japones

  float d1a;
  float d1b;
  float d2a;
  float d2b;
  float d3a;
  float d3b;
  float d4a;
  float d4b;
  float d5a;
  float d5b;

  unsigned long usciclo[6];  //solo uso 5
  //double microslimpiarcan;

  bool blink1;
  bool blink2;
  bool blink3;
  bool blink4;
  bool blink5;
  bool errorSD = false;
  bool modoserver = false;


  unsigned long timeant1;
  unsigned long timeant2;
  unsigned long timeant3;
  unsigned long timeant4;
  unsigned long canspd;

  unsigned long microsmax;

  unsigned long microsSD[2];
  unsigned long microsSD2[2] = { 0, 0 };
  double valoresgy[10];
  int contagy = 0;

  int contalog;
  int contador = 0;

  //double steer = 0;

  bool init = true;

  roll = 0;

  int numledsmax = 0;
  int numledsmaxInv = 0;
  WiFi.mode(WIFI_STA);
  //WiFi.mode(WIFI_AP_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
  }

  esp_now_register_send_cb(OnDataSent);

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  // Register peer2
  memcpy(peerInfo.peer_addr, broadcastAddress2, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer 2");
    return;
  }
  // Register for a callback function that will be called when data is received
  esp_now_register_recv_cb(OnDataRecv);

  SPI.begin();
  pinMode(IMU_CS, OUTPUT);
  digitalWrite(IMU_CS, HIGH);

  if (!myISM.begin(IMU_CS)) {
    Serial.println(F("IMU did not begin."));
    errorIMU = true;
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

  // delay(1000);
  delay(100);
  Serial.println(F(" "));

  // el siguiente bucle es para hacer ceros en los 3 giroscopos.v
  // coge la media de 10 medidas cada 0.05s

  for (int i = 1; i < 50; i++) {
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

      //Serial.print(gxzero);
      // Serial.print(F(" "));
    }
    delay(10);
  }

  gzzero = gzzero / 50;
  gyzero = gyzero / 50;
  gxzero = gxzero / 50;

  if (hayreset) {  //pero si hay reset no hace ceros porque asume estar parado y si hay reset se estará moviendo
    //if (true) {
    gxzero = 472;
    gyzero = -154;
    gzzero = -10;
  }
  /* gxzero = 472;
    gyzero = -154;
    gzzero = -10;*/

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

  Serial.print("MAC:");
  Serial.println(WiFi.macAddress());

  //***************************INICIO LED*******************************
  if (xSemaphoreTake(ptrMutex, portMAX_DELAY) == pdTRUE) {
    if (!errorLED) {
      for (int i = 1; i < 11; i++) {
        LEDStick.setLEDColor(i - 1, 0, 0, 32);
        LEDStick.setLEDColor(i - 2, 0, 0, 0);
        delay(20);
      }
      //LEDStick.LEDOff();
    }
    xSemaphoreGive(ptrMutex);
  }

  Serial.println("Qwiic LED Stick ready!");

  //***************************INICIO PANTALLITA*******************************

  // if (!myOLED.begin()) {
  //   Serial.println("Device failed to initialize");
  //   //while(1);  // halt execution, pero está comentado
  //   errorPANTALLITA = true;
  // }

  //Serial.println("Pantallita is initialized");

  //dtostrf(numarchivo, 6, 0, SIchar2); strcpy (texto , SIchar2);
  //strcpy (texto2 , "/"); strcat (texto2 , SIchar2); strcat (texto2 , ".txt");
  // String hello = texto; // our message


  // if (errorPANTALLITA == false) {
  //   Serial.println(numarchivo);
  //   dtostrf(numarchivo, 6, 0, SIchar2);
  //   strcpy(texto2, "Archivo n ");
  //   strcat(texto2, SIchar2);
  //   escribePANTALLITA(texto2);
  //   delay(30);

  //   strcpy(texto2, currentDate.c_str());
  //   escribePANTALLITA(texto2);
  //   delay(30);

  //   strcpy(texto2, currentTime.c_str());
  //   escribePANTALLITA(texto2);
  //   delay(30);

  //   dtostrf(freemem, 6, 0, SIchar2);
  //   strcpy(texto2, SIchar2);
  //   strcat(texto2, "Mb libres");
  //   escribePANTALLITA(texto2);
  //   delay(30);
  // }
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  contalog = 0;
  timeant = micros() - 20000;  //para que la primera lectura salga 20k

  preferences.begin("k", true);  // aqui carga todos los parametros de la epprom

  k1 = preferences.getFloat("1", 1.15);
  k2 = preferences.getFloat("2", 2.05);
  k3 = preferences.getFloat("3", .99);
  k4 = preferences.getFloat("4", 1100);
  asonora = preferences.getInt("5", 5);
  anular = anular = preferences.getInt("6", 0);

  preferences.end();

  preferences.begin("fisica", true);
  d1 = preferences.getFloat("d1", 4.2);
  coeff = preferences.getFloat("coeff", 7.14);
  alpha = preferences.getFloat("alpha", 60);
  alphav = preferences.getFloat("alphav", 64);
  s = preferences.getFloat("s", 1100);
  preferences.end();

  preferences.begin("idioma", true);
  idioma = preferences.getInt("idioma", 1);
  preferences.end();

  //*************************************INICIO BUCLE TASK1****************************
  for (;;) {

    //mira la Q5 para mandar la fecha y hora por espnow
    if (xQueueReceive(Q5, &outgoing, 0) == pdTRUE) {
      esp_now_send(broadcastAddress2, (uint8_t *)&outgoing, sizeof(outgoing));
      vTaskDelay(100);
    }

    //Serial.println("tick");
    usciclo[contalog] = micros() - timeant;
    timeant = micros();
    //Serial.println(usciclo[contalog]);


    bool pc = false;
    bool pcserial = false;

    if (recibodatoswifi && strcmp(incomingReadings.char1, "PC") == 0) {
      recibodatoswifi = false;
      pc = true;
      entradaserie = incomingReadings.m1;
    }
    //primero, los bloques que controlan la comunicación serie por si hay conexion con configurador

    if (Serial.available() > 0) {
      entradaserie = Serial.read();
      pcserial = true;
    }

    if (pcserial || pc) {

      if (entradaserie == 55) {  //pasa a modo server, para descargar archivo, detendrá el espnow y la escritura en sd

        Serial.println("servidor iniciado");
        entradaserie = 0;
        espnowID = 0;
        modoserver = true;
        if (esp_now_deinit() != ESP_OK) {
          Serial.println("error parando espnow");
        } else {
          Serial.println("detenido espnow");
        }
      }

      if (entradaserie == 149) {  //PC solicita fecha hora
        entradaserie = 0;
        strcpy(textoespecial, "Enviar fechahora");  //solicita que la tarea3 (sd) escriba la fecha y la hora en el puerto serie
        xQueueSend(Q3, textoespecial, 0);           //fisica a sd
      }

      if (entradaserie == 150) {  //PC solicita canspd y ki
        entradaserie = 0;
        preferences.begin("can", true);
        canspd = preferences.getULong("spd", 0);
        preferences.end();
        Serial.println(canspd);
        Serial.println(k1);
        Serial.println(k2);
        Serial.println(k3);
        Serial.println(k4);
        Serial.println(asonora);
        Serial.println(anular);
        if (pc) {
          espnowenviar(canspd, k1, k2, k3, k4, (float)asonora);
          espnowenviar((float)anular, EMPTY_FLOAT, EMPTY_FLOAT, EMPTY_FLOAT, EMPTY_FLOAT, EMPTY_FLOAT);
        }
      }

      if (entradaserie == 151) {  //PC solicita param fisicos
        entradaserie = 0;

        Serial.println(d1);
        Serial.println(coeff);
        Serial.println(alpha);
        Serial.println(alphav);
        Serial.println(s);
        if (pc) {
          espnowenviar(d1, coeff, alpha, alphav, s, EMPTY_FLOAT);
        }
      }

      if (entradaserie == 152) {  //PC solicita sensores
        entradaserie = 0;

        Serial.println(roll);
        Serial.println(pitch);
        Serial.println(ax);
        Serial.println(ay);
        Serial.println(gz);
        if (pc) {
          espnowenviar(roll, pitch, ax, ay, gz, EMPTY_FLOAT);
        }
      }

      if (entradaserie == 200) {  //PC envia canspd y ki
        entradaserie = 0;
        if (Serial.available() > 0) {
          canspd = Serial.read();
        }
        if (Serial.available() > 0) {
          k1 = Serial.read();
          k1 = k1 / 100;
        }
        if (Serial.available() > 0) {
          k2 = Serial.read();
          k2 = k2 / 100;
        }
        if (Serial.available() > 0) {
          k3 = Serial.read();
          k3 = k3 / 100;
        }
        if (Serial.available() > 0) {
          k4 = Serial.read();
          k4 = k4 * 10;
        }
        if (Serial.available() > 0) {
          asonora = Serial.read();
        }
        if (Serial.available() > 0) {
          anular = Serial.read();
        }

        if (pc) {

          while (!recibodatoswifi) {
            vTaskDelay(2);
          }
          canspd = incomingReadings.m1;
          k1 = incomingReadings.m2 / 100;
          k2 = incomingReadings.m3 / 100;
          k3 = incomingReadings.m4 / 100;
          k4 = incomingReadings.m5 * 10;
          asonora = incomingReadings.m6;

          recibodatoswifi = false;

          while (!recibodatoswifi) {
            vTaskDelay(2);
          }
          anular = incomingReadings.m1;
          recibodatoswifi = false;
        }

        //y las guarda en el archivo preferences.
        digitalWrite(25, HIGH);  //pita mientras escribe
        preferences.begin("can", false);
        preferences.putULong("spd", canspd);
        preferences.end();

        preferences.begin("k", false);
        preferences.putFloat("1", k1);
        preferences.putFloat("2", k2);
        preferences.putFloat("3", k3);
        preferences.putFloat("4", k4);
        preferences.putInt("5", asonora);
        preferences.putInt("6", anular);
        preferences.end();
        digitalWrite(25, LOW);
      }

      if (entradaserie == 201) {  //PC envia parametros fisica
        entradaserie = 0;

        if (Serial.available() > 0) {
          d1a = Serial.read();
        }
        if (Serial.available() > 0) {
          d1b = Serial.read();
        }
        if (Serial.available() > 0) {
          d2a = Serial.read();
        }
        if (Serial.available() > 0) {
          d2b = Serial.read();
        }
        if (Serial.available() > 0) {
          d3a = Serial.read();
        }
        if (Serial.available() > 0) {
          d3b = Serial.read();
        }
        if (Serial.available() > 0) {
          d4a = Serial.read();
        }
        if (Serial.available() > 0) {
          d4b = Serial.read();
        }
        if (Serial.available() > 0) {
          d5a = Serial.read();
        }
        if (Serial.available() > 0) {
          d5b = Serial.read();
        }

        if (pc) {
          while (!recibodatoswifi) {
            vTaskDelay(2);
          }
          d1a = incomingReadings.m1;
          d1b = incomingReadings.m2;
          d2a = incomingReadings.m3;
          d2b = incomingReadings.m4;
          d3a = incomingReadings.m5;
          d3b = incomingReadings.m6;
          recibodatoswifi = false;

          while (!recibodatoswifi) {
            vTaskDelay(2);
          }
          d4a = incomingReadings.m1;
          d4b = incomingReadings.m2;
          d5a = incomingReadings.m3;
          d5b = incomingReadings.m4;
          recibodatoswifi = false;
        }

        d1 = d1a + d1b / 100;
        coeff = d2a + d2b / 100;
        alpha = d3a + d3b / 100;
        alphav = d4a + d4b / 100;
        s = (d5a + d5b / 100) * 100;

        //y las guarda en el archivo preferences.
        digitalWrite(25, HIGH);  //pita mientras escribe

        preferences.begin("fisica", false);
        preferences.putFloat("d1", d1);
        preferences.putFloat("coeff", coeff);
        preferences.putFloat("alpha", alpha);
        preferences.putFloat("alphav", alphav);
        preferences.putFloat("s", s);
        preferences.end();

        digitalWrite(25, LOW);
      }
      if (entradaserie == 202) {  //PC envia fecha y hora
        entradaserie = 0;

        strcpy(textoespecial, "Recibir fechahora");  //solicita cambio hora por tarea 3

        if (Serial.available() > 0) {
          digitalWrite(25, HIGH);  //pita mientras recibe

          static char fechahora[30];  // Buffer
          Serial.readBytesUntil('\n', fechahora, sizeof(fechahora));
          xQueueSend(Q3, textoespecial, 0);  //fisica a sd
          xQueueSend(Q3, fechahora, 0);
          digitalWrite(25, LOW);
        }


        if (pc) {
          static char fechahorawifi[30];
          while (!recibodatoswifi) {
            vTaskDelay(2);
          }
          recibodatoswifi = false;
          memcpy(fechahorawifi, incomingReadings.char2, sizeof(incomingReadings.char2));
          while (!recibodatoswifi) {
            vTaskDelay(2);
          }
          recibodatoswifi = false;
          strcat(fechahorawifi, incomingReadings.char2);

          xQueueSend(Q3, textoespecial, 0);  //fisica a sd
          xQueueSend(Q3, fechahorawifi, 0);
          //digitalWrite(25, LOW);
        }
      }
      if (entradaserie == 203) {  //PC envia idioma
        entradaserie = 0;

        if (Serial.available() > 0) {
          idioma = Serial.read();
        }

        if (pc) {
          while (!recibodatoswifi) {
            vTaskDelay(2);
          }
          idioma = incomingReadings.m1;
          recibodatoswifi = false;
        }

        digitalWrite(25, HIGH);  //pita mientras escribe

        preferences.begin("idioma", false);
        preferences.putInt("idioma", idioma);
        preferences.end();

        digitalWrite(25, LOW);
      }
    }


    /*Serial.print("task1 ");
      Serial.print(usciclo);
      Serial.print(" ");
      Serial.println(contalog);*/


    /*mensajeFAC[0] = roll;
    mensajeFAC[1] = pitch;
    mensajeFAC[2] = yaw;*/

    //xQueueOverwrite(Q2, mensajeFAC);  //fisica a can

    /*xQueuePeek(Q1, mensajerecibido, 0);  //can a fisica
    steer = mensajerecibido[0];
    speed = mensajerecibido[1];
    microslimpiarcan = mensajerecibido[2];
    // diag = mensajerecibido[3];*/

    xQueuePeek(Q4, microsSD2, 0);  //sd a fisica
    // Serial.print(microsSD2[1]);
    //Serial.print(" ");
    if (microsSD2[1] == 1) {
      errorSD = true;
    }

    if (myISM.checkStatus()) {  //espera a que los sensores estén actualizados

      contalog++;
      //Serial.println(contalog);
      myISM.getAccel(&accelData);
      myISM.getGyro(&gyroData);

      gx = gyroData.xData - gxzero;
      //Serial.print(gyroData.xData);
      //Serial.print(" ");
      //Serial.println(gx);
      gy = gyroData.yData - gyzero;
      gz = gyroData.zData - gzzero;

      ax = accelData.xData;
      ay = accelData.yData;
      az = accelData.zData;

      //bool recibodatoswifi = ((micros() - timeantwifi) < 30000);

      if ((micros() - timeantwifi) > 300000) {
        recibodatoswifi = false;
      }
      if (recibodatoswifi) {  //pisa los datos del acelerómetro si una unidad externa los envia por espNOW
        ax = incomingax;
        ay = incomingay;
        az = incomingaz;
      }


      if (!modoserver && true) {  //bloque emitir
        BME280Readings.m1 = 99;
        //BME280Readings.m2 = ax;
        BME280Readings.m2 = roll;
        BME280Readings.m3 = pitch;
        BME280Readings.m4 = az;
        esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&BME280Readings, sizeof(BME280Readings));

        if (result == ESP_OK) {
          // Serial.println("Sent with success");
        } else {
          // Serial.println("Error sending the data");
        }
      }

      if (false) {  //bloque recibir
      }

      psi = yaw * 6.28 / 360;
      theta = -pitch * 6.28 / 360;
      phi = -roll * 6.28 / 360;

      if (!init) {

        yaw = yaw + ((gy * 0 + (gx * (sin(phi) / cos(theta))) + (gz * (cos(phi) / cos(theta)))) * msi / 1000000) * usciclo[contalog] / 15000;  //20000
        pitch = pitch + ((gy * 0 + (gx * (cos(phi))) - (gz * (sin(phi)))) * msi / 1000000) * usciclo[contalog] / 15000;
        roll = roll + ((gy * 1 + (gx * (sin(phi) * sin(theta) / cos(theta))) + (gz * (cos(phi) * sin(theta) / cos(theta)))) * msi / 1000000) * usciclo[contalog] / 15000;
      }
      //lo siguiente es para COMPENSAR EL DRIFT de los giroscopos

      rolla = -atan(ax / az) * 360 / 6.28;


      /*Serial.print(roll);
        Serial.print(" ");
        Serial.print(pitch);
        Serial.print(" ");
        Serial.println(yaw);
      */

      double accmag = sqrt(ax * ax + ay * ay + az * az);
      //Serial.println(accmag);

      if (accmag < 1030 && accmag > 980) {
        diferencia = roll - rolla;
        if (diferencia > 60) {
          diferencia = 60;
        }
        if (diferencia < -60) {
          diferencia = -60;
        }

        //roll = roll - 0.0001 * diferencia;
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

      /*Serial.print(pitch);
        Serial.print(" ");
        Serial.println(pitcha);*/
      // hay que hacer una media móvil de gy

      valoresgy[contagy] = gy;  //en cada vuelta del bucle de 20ms sube contagy y pisa el valor de hace 10 vueltas
      contagy++;
      if (contagy > 9) {
        contagy = 0;
      }

      float gyavg = 0;

      for (int i = 0; i < 10; i++) {  //y aqui los suma todos
        gyavg = gyavg + valoresgy[i];
      }

      gyavg = gyavg / 10;

      /*Serial.print(gy);
        Serial.print(" ");
        Serial.println(gyavg);*/
      h = sqrt(sq(d1 * 1000) - sq(s / 2));
      //Serial.println(h);
      /*Serial.println(s/2);
        Serial.println(sq(d1));
        Serial.println(sq(s/2));*/
      phi1crit = atan((s / 2) / h);
      phi1 = abs(atan(ax / az));
      phi1critF = atan((k4 / 2) / h);
      phi1F = abs(atan(ay / az));
      //wcrit=sqrt(9.81*m*s*alphav/(2*Ixxv));
      float wcrit = sqrt(coeff * s / 1000 * alphav / 4) * 360 / 6.28 * 1000;  //*1000 porque wcrit está en milésimas de grado
      /*Serial.println(phi1crit);
        Serial.println(h);
        Serial.println(d1);
        Serial.println(phi1);
        Serial.println(gyavg);
        Serial.println("");*/

      // wcrit = 50000; //50 grados por segundo

      //SI = 1 - (phi1 / phi1crit);  // indice estabilidad
      SI = 1 - k1 * (phi1 / phi1crit) - k2 * (gyavg / wcrit) * (gyavg / wcrit);
      SIF = 1 - k1 * (phi1F / phi1critF);
      //Serial.println(SIF);
      if (anular == 1) SIF = 1;

      if ((micros() - timeant1) > 400000) {
        blink1 = !blink1;
        timeant1 = micros();
      }

      if ((micros() - timeant2) > 200000) {
        blink2 = !blink2;
        timeant2 = micros();
      }

      if ((micros() - timeant3) > 40000) {
        blink3 = !blink3;
        timeant3 = micros();
      }

      if ((micros() - timeant4) > 250000) {
        blink4 = true;
        timeant4 = micros();
      }
      if (millis() - previousMillis >= 50) {  // Cambiar a 50 ms para hacer parpadeo muy rápido
        blink5 = !blink5;                     // Alternar el valor de `blink1` entre `true` y `false`
        previousMillis = millis();
      }

      //*************************************************                 BLOQUE LED                 *********************************************************




      // aqui pinta los leds que hay que encender
      if (!errorLED && (contalog == 5 || contalog == 3 || contalog == 1)) {
        //float umbralesLeds[10] = {0.75, 0.70, 0.65, 0.55, 0.50, 0.40, 0.35, 0.30, 0.20, 0.10};
        //SI_k= - SI *(1 /k3) +1;
        //SIF_k= - SIF *(1 /k3) +1;
        leds = -SI * (1 / k3) + 1;
        ledsF = -SIF * (1 / k3) + 1;
        //        Serial.println (k3);
        if (leds < 0) {
          leds = 0;
        };
        if (leds > 1) {
          leds = 1;
        };
        if (ledsF < 0) {
          ledsF = 0;
        };
        if (ledsF > 1) {
          ledsF = 1;
        };
        int leds_encender = leds * numleds;
        int leds_encenderF = ledsF * numleds;
        int leds_encenderC = (1 - leds) * numleds;  //el complementario a leds encender para derecha a izquierda

        //Serial.println(k4);
        if (SI > SIF) {
          if (xSemaphoreTake(ptrMutex, 0) == pdTRUE) {
            LEDStick.LEDOff();
            //int parpadeo=0;
            if (leds_encenderF > 10) {
              digitalWrite(25, HIGH);
              for (int i = 0; i < 10; i++) {
                if (blink5) {
                  LEDStick.setLEDColor(i, 128, 0, 0);
                } else {
                  LEDStick.setLEDColor(i, 128, 128, 128);
                }
              }
            }
            if (leds_encenderF > 8 && leds_encenderF <= 10) {
              for (int i = 1; i < 9; i++) {
                LEDStick.setLEDColor(i, 128, 0, 0);  // Rojo
              }
              if (blink2) {
                digitalWrite(25, HIGH);
              } else {
                digitalWrite(25, LOW);
              }
            }
            if (leds_encenderF > 7 && leds_encenderF < 9) {
              for (int i = 3; i < 7; i++) {
                LEDStick.setLEDColor(i, 128, 0, 0);  // Rojo
              }
              if (blink1) {
                digitalWrite(25, HIGH);
              } else {
                digitalWrite(25, LOW);
              }
            }
            if (leds_encenderF < 8) {
              digitalWrite(25, LOW);
            }
            xSemaphoreGive(ptrMutex);
          }


          if (SIF < 0.17) {
            switch (idioma) {
              case 1:
                digitalWrite(26, LOW);
                break;
              case 2:
                digitalWrite(17, LOW);
                break;
              case 3:
                digitalWrite(26, LOW);
                digitalWrite(17, LOW);
                break;
              case 4:
                digitalWrite(16, LOW);
                break;
              case 5:
                digitalWrite(16, LOW);
                digitalWrite(26, LOW);
                break;
            }
          } else {
            digitalWrite(26, HIGH);
            digitalWrite(17, HIGH);
            digitalWrite(16, HIGH);
          }
        } else if (SI < SIF && ax > 0) {  //***vuelco lateral******************* //del 0 al 10
          if (axant < 0) {
            numledsmax = 10 - numledsmax;
          }

          switch (asonora) {
            case 3:
              if (leds_encender <= 2) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 2) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 4) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 6) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 4:
              if (leds_encender <= 3) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 3) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 5) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 7) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 5:
              if (leds_encender <= 4) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 4) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 6) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 8) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 6:
              if (leds_encender <= 5) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 5) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 7) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 9) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 7:
              if (leds_encender <= 6) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 6) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 8) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 10) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 8:
              if (leds_encender <= 7) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 7) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 8) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 9) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 9:
              if (leds_encender <= 8) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 8) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 9) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 10) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 10:
              if (leds_encender <= 9) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 9) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 10) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
          }

          if (leds_encender > 0) {
            colorled1 = 25;
            colorled2 = 25;
            colorled3 = 25;
          }

          if (leds_encender > 4) {
            colorled1 = 254;
            colorled2 = 254;
            colorled3 = 0;
          }

          if (leds_encender > 7) {
            colorled1 = 128;
            colorled2 = 0;
            colorled3 = 0;
          }

          if (leds_encender > 10) {
            // parpadeo rojo y blanco
            if (blink5) {
              colorled1 = 128;
              colorled2 = 0;
              colorled3 = 0;
            } else {
              colorled1 = 128;
              colorled2 = 128;
              colorled3 = 128;
            }
          }
          if (xSemaphoreTake(ptrMutex, 0) == pdTRUE) {
            LEDStick.LEDOff();
            for (int i = 0; i < 10; i++) {
              // Serial.println(leds);
              if (leds_encender > i) {
                // Serial.print("X");
                if (i > numledsmax) {
                  numledsmax = i;
                  microsmax = micros();
                  chivatoColor1 = colorled1;
                  chivatoColor2 = colorled2;
                  chivatoColor3 = colorled3;
                  chivatoStartTime = millis();  // Guarda el tiempo en el que se enciende el chivato
                }

                LEDStick.setLEDColor(i, colorled1, colorled2, colorled3);
                //Serial.println(SI);
              }
            }
            // Mantener el LED del chivato encendido durante 3 segundos
            if (numledsmax != -1 && (millis() - chivatoStartTime <= 2000)) {
              LEDStick.setLEDColor(numledsmax, chivatoColor1, chivatoColor2, chivatoColor3);
            } else if (millis() - chivatoStartTime > 3000) {
              // Después de 3 segundos, resetear el chivato
              numledsmax = 0;
            }
            xSemaphoreGive(ptrMutex);
          }

          axant = ax;
          if (SI < 0.17) {
            switch (idioma) {
              case 1:
                digitalWrite(26, LOW);
                break;
              case 2:
                digitalWrite(17, LOW);
                break;
              case 3:
                digitalWrite(26, LOW);
                digitalWrite(17, LOW);
                break;
              case 4:
                digitalWrite(16, LOW);
                break;
              case 5:
                digitalWrite(16, LOW);
                digitalWrite(26, LOW);
                break;
            }
          } else {
            digitalWrite(26, HIGH);
            digitalWrite(17, HIGH);
            digitalWrite(16, HIGH);
          }
        } else if (SI < SIF && ax < 0) {  //****************************************Vuelco lateral del 10 al 0.
          if (axant > 0) {
            numledsmaxInv = 9 - numledsmaxInv;
          }
          //Primero definimos zumbador y voz
          switch (asonora) {
            case 3:
              if (leds_encender <= 2) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 2) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 4) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 6) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 4:
              if (leds_encender <= 3) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 3) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 5) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 7) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 5:
              if (leds_encender <= 4) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 4) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 6) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 8) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 6:
              if (leds_encender <= 5) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 5) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 7) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 9) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 7:
              if (leds_encender <= 6) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 6) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 8) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 10) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 8:
              if (leds_encender <= 7) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 7) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 8) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 9) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 9:
              if (leds_encender <= 8) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 8) {
                if (blink1) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 9) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 10) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
            case 10:
              if (leds_encender <= 9) {
                // No hay bip
                digitalWrite(25, LOW);
              }
              if (leds_encender > 9) {
                if (blink2) {
                  digitalWrite(25, HIGH);
                } else {
                  digitalWrite(25, LOW);
                }
              }
              if (leds_encender > 10) {
                // Bip continuo
                digitalWrite(25, HIGH);
              }
              break;
          }

          //aquí definimos colores para cada rango
          if (leds_encender > 0) {
            colorled1 = 25;
            colorled2 = 25;
            colorled3 = 25;
          }

          if (leds_encender > 4) {
            colorled1 = 254;
            colorled2 = 254;
            colorled3 = 0;
          }

          if (leds_encender > 7) {
            colorled1 = 128;
            colorled2 = 0;
            colorled3 = 0;
          }

          if (leds_encender > 10) {
            // Parpadeo rojo y blanco
            if (blink5) {
              colorled1 = 128;
              colorled2 = 0;
              colorled3 = 0;
            } else {
              colorled1 = 128;
              colorled2 = 128;
              colorled3 = 128;
            }
            // Bip continuo
            // digitalWrite(25, HIGH);
          }
          if (xSemaphoreTake(ptrMutex, 0) == pdTRUE) {
            LEDStick.LEDOff();               // Apaga todos los LEDs inicialmente
            for (int i = 0; i < 10; i++) {   // Recorre los LEDs en orden inverso
              if (leds_encenderC < i + 1) {  // Ajusta la lógica para encender LEDs de derecha a izquierda
                if (i < numledsmaxInv) {
                  numledsmaxInv = i;
                  microsmax = micros();
                  chivatoColor1 = colorled1;
                  chivatoColor2 = colorled2;
                  chivatoColor3 = colorled3;
                  chivatoStartTime = millis();  // Guarda el tiempo en el que se enciende el chivato
                }
                //Serial.println(i);
                //LEDStick.setLEDColor(0, 128, 0, 0);
                LEDStick.setLEDColor(i, colorled1, colorled2, colorled3);
              }
            }

            // Mantener el LED del chivato encendido durante 3 segundos
            if (numledsmaxInv != -1 && (millis() - chivatoStartTime <= 2000)) {
              LEDStick.setLEDColor(numledsmaxInv, chivatoColor1, chivatoColor2, chivatoColor3);
            } else if (millis() - chivatoStartTime > 3000) {
              // Después de 3 segundos, resetear el chivato
              numledsmaxInv = 10;
            }
            xSemaphoreGive(ptrMutex);
          }

          axant = ax;
          if (SI < 0.17) {
            switch (idioma) {
              case 1:
                digitalWrite(26, LOW);
                break;
              case 2:
                digitalWrite(17, LOW);
                break;
              case 3:
                digitalWrite(26, LOW);
                digitalWrite(17, LOW);
                break;
              case 4:
                digitalWrite(16, LOW);
                break;
              case 5:
                digitalWrite(16, LOW);
                digitalWrite(26, LOW);
                break;
            }
          } else {
            digitalWrite(26, HIGH);
            digitalWrite(17, HIGH);
            digitalWrite(16, HIGH);
          }
        }
      }
      ////////*************************///////////////

      if (contalog == 5) {  // dispara tarea sd

        char SIchar[12];
        contasd++;
        contalog = 0;
        if (initcabecera) {
          contasd++;
          strcpy(texto, "ax; ay; az; gx; gy; gz; roll; pitch; yaw; timeantwifi; usciclo1; usciclo2; usciclo3;usciclo4; usciclo5; si; accmag; microsds; k3");
          strcat(texto, "\n");
          initcabecera = false;
        }
        dtostrf(ax, 6, 2, SIchar);
        if (contasd == 1) {
          strcpy(texto, SIchar);
        } else {
          strcat(texto, SIchar);
        }

        if (hayreset) {
          diag = 99;
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
        dtostrf((micros() - timeantwifi), 6, 2, SIchar);
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
        // Serial.println(rpm);

        if (contasd == 10) {
          //Serial.println("contasd 10");
          contasd = 0;
          if (modoserver) {
            strcpy(texto, "Detener envio");  //en modo server escribo este mensaje especial para no acceder a la sd
          }
          xQueueSend(Q3, texto, 0);  //fisica a sd
          //Serial.println(uxTaskGetStackHighWaterMark(NULL));
        }
      }

      // if (contalog == 2 && !errorPANTALLITA) {  //pantallita
      //   contador++;

      /*if (contador < 33) {

          dtostrf(speed, 6, 0, SIchar2);
            strcpy(texto2, SIchar2);
            strcat(texto, " ");

          dtostrf(steer, 6, 0, SIchar2);

          strcpy(texto2, SIchar2);
          escribePANTALLITA(texto2);
        }*/

      // if (contador > 32 && contador < 102) {
      //   if (true) {
      //     int x0 = 30 + (myOLED.getWidth()) / 2;
      //     int y0 = (myOLED.getHeight()) / 2;
      //     myOLED.erase();
      //     // myOLED.rectangleFill(x0 - 16, y0 - 16, 36, 32, 0);
      //     float roll2 = roll + 2.5;
      //     if (roll2 > -1.5 && roll2 < 1.5) {
      //       myOLED.line(x0 - 16, y0, x0 + 16, y0);
      //     } else {
      //       myOLED.line(x0 - 16 * cos(roll2 * 628 / 36000), y0 - 16 * sin(roll2 * 628 / 36000), x0 + 16 * cos(roll2 * 628 / 36000), y0 + 16 * sin(roll2 * 628 / 36000));
      //     }
      //     myOLED.display();
      //     actualizader = false;
      //   } else {

      //     int x0 = -20 + (myOLED.getWidth()) / 2;
      //     int y0 = (myOLED.getHeight()) / 2;
      //     //myOLED.erase();
      //     myOLED.rectangleFill(x0 - 16, y0 - 16, 32, 32, 0);
      //     float roll2;
      //     if (pitch > -1.5 && pitch < 1.5) {
      //       myOLED.line(x0 - 16, y0, x0 + 16, y0);
      //     } else {
      //       myOLED.line(x0 - 16 * cos(pitch * 628 / 36000), y0 - 16 * sin(pitch * 628 / 36000), x0 + 16 * cos(pitch * 628 / 36000), y0 + 16 * sin(pitch * 628 / 36000));
      //     }
      //     myOLED.display();
      //     actualizader = true;
      //   }
      // }

      // if (contador > 65 && errorSD) {
      //   escribePANTALLITA("no sd");
      // }
      // if (contador == 100) {
      //   contador = 0;
      // }
      // }

    }  //fin de los sensores actualizados
    //Serial.print("-");
    //Serial.println(micros() - timeant);
    // Serial.print("%");
    //Serial.println(uxTaskGetStackHighWaterMark(NULL));
    init = false;
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
  }

}  // Infinite loop


/*void Task2(void *pvParameters) {  //can
  unsigned long timeant;
  unsigned long tiempociclo;
  unsigned long i;
  unsigned long timeant9;   //tiempo sin recibir nada
  unsigned long timeant11;  //tiempo desde ultimo reset preventivo
  double mensajerecibido[3];
  double mensajeCAF[3];
  bool errorCAN = false;
  bool hay;
  bool huboerrorCAN = false;

  int packetSize;
  long id;
  long idant;
  int hits = 0;
  int misses = 0;
  int vacios = 0;
  byte candatos[8];
  byte candatos2[8];

  twai_message_t message;

  //CAN.setPins(36, 33);

  delay(20);
  Serial.println("starting CAN task");

  /* if (!CAN.begin(1000E3)) {

    Serial.println("Starting CAN failed!");
    errorCAN = true;
    } else {
    Serial.println("CAN started");
    errorCAN = false;
    timeant9 = micros();
    }
  *

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();  //Look in the api-reference for other speed sets.
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  g_config.rx_queue_len = 1000;

  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("Driver installed");
  } else {
    Serial.println("Failed to install driver");
    return;
  }

  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    Serial.println("Driver started");
  } else {
    Serial.println("Failed to start driver");
    return;
  }

  // Reconfigure alerts to detect frame receive, Bus-Off error and RX queue full states
  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("CAN Alerts reconfigured");
  } else {
    Serial.println("Failed to reconfigure alerts");
    return;
  }

  // TWAI driver is now successfully installed and started
  driver_installed = true;


  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {

    tiempociclo = (micros() - timeant);
    timeant = micros();



    while (twai_receive(&message, 0) == ESP_OK) {
      //Serial.printf("ID: %lx\nByte:", message.identifier);
      //for (int i = 0; i < message.data_length_code; i++) {
      //  Serial.printf(" %d = %02x,", i, message.data[i]);
      //}
      if (message.identifier == 0x2F) {
        mensajeCAF[0] = (message.data[3] * 256 + message.data[2]) - (60 * 256) - 9225;
      }
      if (message.identifier == 0x98) {
        mensajeCAF[1] = (message.data[4] * 256 + message.data[3]) / 150;
      }

      // mensajeCAF[2] = (micros() - timeant);
    }

    if ((micros() - timeant9) > 1000000) {  //monitoriza estado de la linea una vez por segundo

      twai_status_info_t twaistatus;
      twai_get_status_info(&twaistatus);
      /*Serial.print("Buffer entrada, errores, perdidos, buserrors: ");
        Serial.print(twaistatus.msgs_to_rx);
        Serial.print(" ");
        Serial.print(twaistatus.rx_error_counter);
        Serial.print(" ");
        Serial.print(twaistatus.rx_missed_count);
        Serial.print(" ");
        Serial.println(twaistatus.bus_error_count);
        Serial.print(" ");
        Serial.println(twaistatus.rx_missed_count + twaistatus.bus_error_count);*
      mensajeCAF[2] = (twaistatus.rx_missed_count + twaistatus.bus_error_count);

      timeant9 = micros();
    }
    //Serial.println("");
    //Serial.print("limpiacan en us: ");
    //Serial.println(micros() - timeant);


    xQueueOverwrite(Q1, mensajeCAF);
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2));

  }  // Infinite loop
} */

void Task3(void *pvParameters) {  //sd
  unsigned long timeant;
  unsigned long i;
  char mensajerecibido[2500];
  bool errorSD;
  bool errorRTC;
  char SIchar2[12];
  char texto3[800];
  unsigned long microsSD[2];
  bool modoserver = false;
  bool wifiiniciada = false;
  char SIchar3[12];

  //static SimpleFileUploader uploader;

  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
    errorSD = true;
    //return;
  } else {
    errorSD = false;
    Serial.println("Card Mount OK");
  }

  // el siguiente bloque solo se ejecuta si no hay errorSD
  if (!errorSD) {

    uint8_t cardType = SD_MMC.cardType();

    if (cardType == CARD_NONE) {
      Serial.println("No SD_MMC card attached");
      errorSD = true;
      //return;
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

  //miro la hora
  errorRTC = false;
  if (rtc.begin() == false) {
    Serial.println("Error RTC");
    errorRTC = true;
  } else {
    Serial.println("RTC online!");
    if (rtc.updateTime() == true) {

      currentDate = rtc.stringDate();  //Get the current date in dd/mm/yyyy format
      currentTime = rtc.stringTime();

      timeDateString = currentDate + " " + currentTime;
      //Serial2.println(timeDateString);
    }
  }

  // descomentar las siguientes lineas para poner en hora

  // if (rtc.setToCompilerTime() == false)
  // Serial.println("Something went wrong setting the time");
  // else
  // Serial.println("New time set!");

  // if (rtc.setTime(0, 32, 11, 4, 16, 1, 2025)) {  // seg, min, hora, día_semana (0=domingo), día, mes, año
  // Serial.println("Hora y fecha configuradas correctamente.");
  // } else {
  // Serial.println("Error al configurar la hora y fecha.");
  // }


  // LEE DE LA MEMORIA INTERNA QUE NÚMERO DE ORDEN DE ARCHIVO TOCA ESCRIBIR

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

    // Leer configuración
    if (!readConfigFile()) {
      Serial.println("Error: Fallo en la lectura de la configuración o ninguna red wifi");
    } else {
      sendInitialFiles();
    }

    //preparo  texto3 para cuando llegue mensaje
    dtostrf(numarchivo, 6, 0, SIchar2);
    strcpy(texto3, "/");
    strcat(texto3, SIchar2);
    strcat(texto3, "_ESTABILIDAD_");
    strcat(texto3, deviceId.c_str());
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
    strcat(mensajerecibido, deviceId.c_str());
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

  //desconectar wifi
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  archivosFTP = true;

  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {

    xQueueReceive(Q3, mensajerecibido, 10000);
    timeant = micros();

    if (strcmp(mensajerecibido, "Enviar fechahora") == 0) {
      Serial.println(currentTime);
      Serial.println(currentDate);
      Serial.println(numarchivo);
      Serial.println(freemem);

      currentTime.toCharArray(SIchar3, 12);
      SIchar3[sizeof(SIchar3) - 1] = '\0';
      strcpy(outgoing.char2, SIchar3);
      outgoing.m1 = EMPTY_FLOAT;
      outgoing.m2 = EMPTY_FLOAT;
      outgoing.m3 = EMPTY_FLOAT;
      outgoing.m4 = EMPTY_FLOAT;
      outgoing.m5 = EMPTY_FLOAT;
      outgoing.m6 = EMPTY_FLOAT;
      xQueueSend(Q5, &outgoing, 0);
      currentDate.toCharArray(SIchar3, 12);
      SIchar3[sizeof(SIchar3) - 1] = '\0';
      strcpy(outgoing.char2, SIchar3);
      xQueueSend(Q5, &outgoing, 0);
      outgoing.char2[0] = '\0';
      outgoing.m1 = numarchivo;
      outgoing.m2 = freemem;
      xQueueSend(Q5, &outgoing, 0);
    } else if (strcmp(mensajerecibido, "Recibir fechahora") == 0) {  //recibe fecha y hora para configurar rtc
      if (xQueueReceive(Q3, mensajerecibido, 10000) == pdTRUE) {
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
    } else if (strcmp(mensajerecibido, "Detener envio") == 0 && !wifiiniciada) {

      modoserver = true;
      //***************************************************** INICIO WEBSERVER **************************************//
      // Conéctate a la red WiFi

      WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Conectando WiFi...");
      }
      Serial.println("WiFi CONECTADO");

      // Imprimir la dirección IP asignada
      Serial.print("IP asignada: ");
      Serial.print(WiFi.localIP());
      Serial.print(":");
      Serial.println(serverPort);
      /* preparado para mandar si espnow no estuviese parado
      IPAddress ip = WiFi.localIP();
      dtostrf(ip[3], 6, 2, SIchar3);
      strcpy(outgoing.char2, SIchar3);
      outgoing.m1 = EMPTY_FLOAT;
      outgoing.m2 = EMPTY_FLOAT;
      outgoing.m3 = EMPTY_FLOAT;
      outgoing.m4 = EMPTY_FLOAT;
      outgoing.m5 = EMPTY_FLOAT;
      outgoing.m6 = EMPTY_FLOAT;
      xQueueSend(Q5, &outgoing, 0);
      outgoing.char2[0] = '\0';
      */
      digitalWrite(25, HIGH);
      delay(100);
      digitalWrite(25, LOW);
      delay(100);
      digitalWrite(25, HIGH);
      delay(100);
      digitalWrite(25, LOW);
      delay(100);
      digitalWrite(25, HIGH);
      delay(100);
      digitalWrite(25, LOW);

      // Definir rutas y manejo de solicitudes HTTP
      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = String(htmlCode);
        const int filesPerPage = 20;

        fs::File root = SD_MMC.open("/");
        int totalFiles = 0;
        while (root.openNextFile()) {
          totalFiles++;
        }

        int numPages = (totalFiles + filesPerPage - 1) / filesPerPage;
        int currentPage = 0;

        if (request->hasParam("page")) {
          currentPage = request->getParam("page")->value().toInt();
          int startFileIndex = currentPage * filesPerPage;
          int endFileIndex = startFileIndex + filesPerPage;

          root = SD_MMC.open("/");
          int fileIndex = 0;
          File file = root.openNextFile();
          html += "<ul class=\"file-list\">";
          while (file) {
            if (file.isDirectory()) {
              file = root.openNextFile();
              continue;
            }
            if (fileIndex >= startFileIndex && fileIndex < endFileIndex) {
              html += "<li class='file-list-item'>" + String(file.name()) + "<div class='button-container'><a href='" + String(file.name()) + "' download><button>Descargar archivo</button></a><button onclick=\"deleteFile('" + String(file.name()) + "')\">Borrar archivo</button></div></li>";
            }
            fileIndex++;
            file = root.openNextFile();
          }
          html += "</ul>";
        }

        html += "<footer class='centered'>";
        html += "<h2></h2>";

        if (currentPage > 0) {
          html += "<a href='/?page=" + String(currentPage - 1) + "' class='previous center'>Página anterior</a>";
        }

        if (currentPage < numPages - 1) {
          html += "<a href='/?page=" + String(currentPage + 1) + "' class='next center'>Página siguiente</a>";
        }
        html += "<h2></h2>";
        html += "<p class=\"center\">Desarrollado por CSG INGENIERÍA</p>";
        html += "<h2></h2>";
        html += "</footer>";
        html += "<div class='sidenav centered'>";
        html += "<h2 class='center spacer' >Navegación</h2>";
        html += "<h2></h2>";

        for (int i = 0; i < numPages; i++) {
          html += "<a href='/?page=" + String(i) + "' class='menu-item" + (currentPage == i ? " active" : "") + "'>Página №" + String(i + 1) + "</a>";
        }

        html += "</div></body></html>";
        request->send(200, "text/html", html);
      });

      server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
        String filename = request->getParam("filename")->value();
        if (SD_MMC.exists(filename)) {
          if (SD_MMC.remove(filename)) {
            request->send(200, "text/plain", "File deleted successfully");
          } else {
            request->send(500, "text/plain", "Error deleting file");
          }
        } else {
          request->send(404, "text/plain", "File not found");
        }

        request->redirect("/");
      });


      server.on(
        "/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
          request->send(200, "text/plain", "File upload handler");
        },
        handleFileUpload);

      // Configurar una ruta para la descarga de archivos
      server.onNotFound([](AsyncWebServerRequest *request) {
        String path = request->url();
        // Verificar si el archivo existe
        if (SD_MMC.exists(path)) {
          request->send(SD_MMC, path, "application/octet-stream");
        } else {
          request->send(404, "text/plain", "Archivo no encontrado");
        }
      });
      // Iniciar el servidor
      server.begin();
      //Serial.println("modo server en sd");
      wifiiniciada = true;
    } else if (!errorSD && !modoserver) {
      appendFile(SD_MMC, texto3, mensajerecibido);  //ESTA ES LA LINEA QUE ESCRIBE EN LA SD
      Serial2.println(mensajerecibido);
      Serial.println(mensajerecibido);
      if (xSemaphoreTake(ptrMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (!errorRTC && rtc.updateTime()) {
          currentTime = rtc.stringTime();
          currentDate = rtc.stringDate();

          //timeDateString = currentDate + " " + currentTime;
          //Serial2.println(timeDateString);
        }
        xSemaphoreGive(ptrMutex);
      }
      //añadir linea con hora
      strcpy(mensajerecibido, currentTime.c_str());
      strcat(mensajerecibido, "\n");

      appendFile(SD_MMC, texto3, mensajerecibido);
      Serial2.println(mensajerecibido);
      Serial.println(mensajerecibido);
    }

    microsSD[0] = micros() - timeant;

    if (errorSD) {
      microsSD[1] = 1;
    }

    xQueueOverwrite(Q4, microsSD);

    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
  }  // Infinite loop
}
