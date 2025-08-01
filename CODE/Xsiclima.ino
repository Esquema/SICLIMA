#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <time.h>
#include <PubSubClient.h>


//Aqui defino mis credenciales
#define WIFI_SSID "SEGUNDO-P 2.4"
#define WIFI_PASSWORD "segundop"
#define API_KEY " "
#define API_SECRET " "
#define ACCESS_TOKEN " "
#define ACCESS_TOKEN_SECRET " "
String apiKey = " "; 

const char* mqttServer = "mqtt3.thingspeak.com";
const int mqttPort = 1883;

const char* mqttClientID = " ";
const char* mqttUser = " ";
const char* mqttPassword = " 9";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// canal de ThingSpeak
const char* channelID = "3020804";
String topic = "channels/" + String(channelID) + "/publish";

#define MQ135PIN 35
#define CALIBRATION_SAMPLE_TIMES 50
#define CALIBRATION_SAMPLE_INTERVAL 500
#define READ_SAMPLE_INTERVAL 50
#define READ_SAMPLE_TIMES 5

// Constantes del MQ135 (valores típicos del datasheet)
#define RLOAD 10.0  
#define RZERO 76.63
#define PARA 116.6020682
#define PARB 2.769034857        

// Umbrales de detección (PPM)
#define CO2_THRESHOLD 1000
#define CO_THRESHOLD 30
#define NH4_THRESHOLD 10
#define ACETONA_THRESHOLD 10
#define ALCOHOL_THRESHOLD 10    

Adafruit_BMP280 bmp;
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Variables globales para MQ135
float  rzero = RZERO;
bool   mq135_calibrated = false;
unsigned long last_mq135_read = 0;
float  current_co2_ppm  = 0;
float  current_co_ppm   = 0;
float  current_nh4_ppm  = 0;

float alcohol_ppm = 0;
float acetona_ppm = 0;

unsigned long lastTweetTime      = 0;
unsigned long lastSensorReadTime = 0;
unsigned long lastThingSpeakTime = 0;

constexpr unsigned long TWEET_INTERVAL_MS   = 2UL * 60UL * 1000UL;
constexpr unsigned long SENSOR_READ_MS      = 5UL * 1000UL;
constexpr unsigned long THINGSPEAK_INTERVAL_MS = 20000;

void   conectarWiFi();
void   sincronizarTiempo();
void   inicializarSensores();
void   imprimirLecturas(float tempDHT, float hum, float tempBMP, float pres, float alt);

void   inicializarMQ135();
float  leerMQ135Analogico();
float  calcularResistencia(float raw_adc);
float  calibrarMQ135();
float  calcularPPM(float rs_gas, String gas);
void   leerMQ135();
void   analizarCalidadAire();
String obtenerEstadoMQ135();
void   detectarEventos();

String urlEncode(const String& str);
String calcularHMACSHA1(const String& key, const String& message);

String crearEncabezadoOAuth(const String& mensajeTweet);
bool   enviarTweet(const String& mensajeTweet);
String generarMensajeTweet(float tempDHT, float hum, float tempBMP, float pres, float alt);




void setup() {
  Serial.begin(115200);
  randomSeed(micros());

  conectarWiFi();
  sincronizarTiempo();
  inicializarSensores();
  inicializarMQ135();
  calibrarMQ135();

  mqttClient.setServer(mqttServer, mqttPort); // Establece el servidor MQTT

  Serial.println("Conectando al servidor MQTT...");
  if (mqttClient.connect(mqttClientID, mqttUser, mqttPassword)) {
    Serial.println(" Conectado a MQTT.");
  } else {
    Serial.print(" Falló conexión MQTT. Estado: ");
    Serial.println(mqttClient.state());
  }

  Serial.println(F("🔧 Setup listo."));
}

const bool HABILITAR_TWITTER = false;
void loop() {
  const unsigned long now = millis();
  // Verificar si toca leer sensores
  if (now - lastSensorReadTime >= SENSOR_READ_MS) {
    lastSensorReadTime = now;

    float temperaturaDHT = dht.readTemperature();
    float humedad        = dht.readHumidity();
    float temperaturaBMP = bmp.readTemperature();
    float presion        = bmp.readPressure() / 100.0F;
    float altitud        = bmp.readAltitude(1013.25);

    leerMQ135();
    detectarEventos();

    if (!isnan(temperaturaDHT) && !isnan(humedad) &&
        !isnan(temperaturaBMP) && !isnan(presion)) {
      imprimirLecturas(temperaturaDHT, humedad, temperaturaBMP, presion, altitud);
      // Verifica si toca enviar tweet
      if (now - lastTweetTime >= TWEET_INTERVAL_MS && verificarWiFi()) {
        String mensaje = generarMensajeTweet(temperaturaDHT, humedad, temperaturaBMP, presion, altitud);
        if (enviarTweet(mensaje)) {
          lastTweetTime = now;
        } else {
          Serial.println(F("Fallo al enviar tweet. Reintentando más tarde..."));
        }
      }

      if (now - lastThingSpeakTime >= THINGSPEAK_INTERVAL_MS) {
      if (verificarWiFi()) {
        if (!mqttClient.connected()) {
          reconnectMQTT();
        }
        mqttClient.loop();
        enviarDatosThingSpeak(
          temperaturaDHT,
          humedad,
          presion,
          current_co2_ppm,
          current_co_ppm,
          current_nh4_ppm,
          alcohol_ppm,
          obtenerEstadoMQ135()
        );
        lastThingSpeakTime = now;
      } else {
        Serial.println(F("WiFi no disponible. No se pudo enviar a ThingSpeak."));
      }
    }


    } else {
      Serial.println(F("Error en la lectura de sensores (NaN)."));
    }
  }
  delay(5);
}



void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return; // Ya está conectado

  Serial.println("Conectando a WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int intentos = 0;
  const int maxIntentos = 20;

  while (WiFi.status() != WL_CONNECTED && intentos < maxIntentos) {
    delay(500);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConectado a WiFi.");
    Serial.println("IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nNo se pudo conectar al WiFi.");
  }
}

bool verificarWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Reintentando...");
    conectarWiFi();
  }
  return WiFi.status() == WL_CONNECTED;
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando al broker MQTT de ThingSpeak.");
    if (mqttClient.connect("ESP32Client", mqttUser, mqttPassword)) {
      Serial.println("Conectado al broker MQTT.");
    } else {
      Serial.print("Error: ");
      Serial.print(mqttClient.state());
      Serial.println(" - Reintentando en 5 segundos - ");
      delay(5000);
    }
  }
}


//Protocolo de Tiempo de Red
void sincronizarTiempo() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("Sincronizando tiempo con NTP...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(F("\nTiempo sincronizado."));
}


void inicializarMQ135() {
  Serial.println("========== INICIALIZANDO MQ135 ==========");
  pinMode(MQ135PIN, INPUT);
  
  Serial.println("Calentando sensor MQ135...");
  Serial.println("IMPORTANTE: Mantener en aire limpio durante 24-48 horas para calibración óptima");
  Serial.println("Esperando 30 segundos para estabilización...");
  
  for(int i = 30; i > 0; i--) {
    Serial.printf("Calentando... %d segundos restantes\n", i);
    delay(1000);
  }
  
  Serial.println("MQ135 inicializado correctamente");
}

float leerMQ135Analogico() {
  int i;
  float rs = 0;
  
  for (i = 0; i < READ_SAMPLE_TIMES; i++) {
    rs += analogRead(MQ135PIN);
    delay(READ_SAMPLE_INTERVAL);
  }
  
  rs = rs / READ_SAMPLE_TIMES;
  return rs;
}

float calcularResistencia(float raw_adc) {
  const float voltage = (raw_adc * 3.3f) / 4095.0f;
  if (voltage == 0) return -1;
  const float rs = ((3.3f * RLOAD) / voltage) - RLOAD;
  return rs;
}

float calibrarMQ135() {
  Serial.println(F("========== CALIBRANDO MQ135 =========="));
  Serial.println(F("CRÍTICO: Asegúrate de estar en AIRE LIMPIO"));
  Serial.println(F("Alejado de gases, humos, alcohol, etc."));
  Serial.println(F("Calibrando..."));
  
  float rs = 0;
  
  for (int i = 0; i < CALIBRATION_SAMPLE_TIMES; i++) {
    float adc = leerMQ135Analogico();
    rs += calcularResistencia(adc);
    Serial.printf("Muestra %d/%d - ADC: %.0f\n", i+1, CALIBRATION_SAMPLE_TIMES, adc);
    delay(CALIBRATION_SAMPLE_INTERVAL);
  }
  
  rs = rs / CALIBRATION_SAMPLE_TIMES;
  const float rzero_calculated = rs / 3.6f;
  
  Serial.printf("Rs promedio: %.2f kΩ\n", rs);
  Serial.printf("RZero calculado: %.2f kΩ\n", rzero_calculated);
  
  if (rzero_calculated > 0 && rzero_calculated < 200) {
    rzero = rzero_calculated;
    mq135_calibrated = true;
    Serial.println("✅ CALIBRACIÓN EXITOSA");
  } else {
    Serial.println("⚠️ Calibración sospechosa, usando valor por defecto");
    rzero = RZERO;
    mq135_calibrated = false;
  }
  
  return rzero;
}

// Convertir a PPM para diferentes gases
float calcularPPM(float rs_gas, String gas) {
  if (rzero <= 0) return -1;
  
  float ratio = rs_gas / rzero;
  if (ratio <= 0) return -1;
  
  float ppm = 0;
  if (gas == "CO2") {
    // Fórmula específica para CO2
    ppm = PARA * pow((rs_gas / rzero), -PARB);
  } else if (gas == "CO") {
    // CO tiene diferente curva
    ppm = 664.7f * pow(ratio, -2.301f);
  } else if (gas == "NH4") {
    // Amoniaco
    ppm = 102.2f * pow(ratio, -2.473f);
  } else if (gas == "Acetona") {
    // Acetona
    ppm = 34.668f * pow(ratio, -3.369f);
  } else if (gas == "Alcohol") {
    // Etanol
    ppm = 77.255f * pow(ratio, -3.18f);
  }
  return ppm;
}

void leerMQ135() {
  if (millis() - last_mq135_read < 15000) return; //Leer cada 15s
  
  float adc_value = leerMQ135Analogico();
  float voltage = (adc_value * 3.3f) / 4095.0f;
  float rs = calcularResistencia(adc_value);
  
  if (rs < 0) {
    Serial.println(F("❌ Error en lectura MQ135"));
    return;
  }
  
  current_co2_ppm = calcularPPM(rs, "CO2");
  current_co_ppm = calcularPPM(rs, "CO");
  current_nh4_ppm = calcularPPM(rs, "NH4");
  acetona_ppm = calcularPPM(rs, "Acetona");
  alcohol_ppm = calcularPPM(rs, "Alcohol");
  
  /*
  Serial.println("========== LECTURA MQ135 ==========");
  Serial.printf("ADC: %.0f | Voltaje: %.2fV | Rs: %.2f kΩ\n", adc_value, voltage, rs);
  Serial.printf("RZero: %.2f kΩ | Ratio Rs/RZero: %.2f\n", rzero, rs/rzero);
  */
  Serial.println("--- CONCENTRACIONES (PPM) ---");
  Serial.printf("🏭 CO2: %.1f ppm %s\n", current_co2_ppm, (current_co2_ppm > CO2_THRESHOLD) ? "⚠️ ALTO" : "✅");
  Serial.printf("☠️ CO: %.1f ppm %s\n", current_co_ppm, (current_co_ppm > CO_THRESHOLD) ? "🚨 PELIGROSO" : "✅");
  Serial.printf("💨 NH4: %.1f ppm %s\n", current_nh4_ppm, (current_nh4_ppm > NH4_THRESHOLD) ? "⚠️ DETECTADO" : "✅");
  Serial.printf("🧪 Acetona: %.1f ppm %s\n", acetona_ppm, (acetona_ppm > ACETONA_THRESHOLD) ? "⚠️ DETECTADO" : "✅");
  Serial.printf("🍷 Alcohol: %.1f ppm %s\n", alcohol_ppm, (alcohol_ppm > ALCOHOL_THRESHOLD) ? "⚠️ DETECTADO" : "✅");
  
  analizarCalidadAire();
  
  last_mq135_read = millis();
}

void analizarCalidadAire() {
  Serial.println("--- ANÁLISIS DE CALIDAD DEL AIRE ---");
  
  int score = 100;
  String status = "EXCELENTE";
  String emoji = "🌟";
  
  // Penalizar según niveles
  if (current_co2_ppm > 5000) { score -= 50; status = "PELIGROSO"; emoji = "☠️"; }
  else if (current_co2_ppm > 2000) { score -= 30; status = "MALO"; emoji = "🔴"; }
  else if (current_co2_ppm > 1000) { score -= 15; status = "REGULAR"; emoji = "🟡"; }
  
  if (current_co_ppm > 50) { score -= 40; status = "TÓXICO"; emoji = "☠️"; }
  else if (current_co_ppm > 30) { score -= 20; }
  
  if (current_nh4_ppm > 25) { score -= 20; }
  else if (current_nh4_ppm > 10) { score -= 10; }
  
  score = max(0, score);
  
  Serial.printf("%s CALIDAD DEL AIRE: %s (%d/100)\n", emoji.c_str(), status.c_str(), score);
  
  if (score < 50) {
    Serial.println(F("RECOMENDACIÓN: Ventila el área inmediatamente"));
  } else if (score < 70) {
    Serial.println(F("RECOMENDACIÓN: Considera ventilar el área"));
  }
}

// Función para incluir en el tweet
String obtenerEstadoMQ135() {
  String estado;
  if (current_co2_ppm > CO2_THRESHOLD) estado += "CO2↑ ";
  if (current_co_ppm  > CO_THRESHOLD)  estado += "CO⚠️ ";
  if (current_nh4_ppm > NH4_THRESHOLD) estado += "NH4↑ ";

  if (estado.isEmpty()) estado = "Aire✅";

  return estado.substring(0, min((int)estado.length(), 20));
}

void detectarEventos() {
  static bool co2_alert_sent = false;
  static bool co_alert_sent = false;
  
  if (current_co2_ppm > 3000 && !co2_alert_sent) {
    Serial.println(F(" ALERTA CO2 CRÍTICO "));
    co2_alert_sent = true;
  } else if (current_co2_ppm < 2000) {
    co2_alert_sent = false;
  }
  
  if (current_co_ppm > 50 && !co_alert_sent) {
    Serial.println(F("☠️☠️☠️ PELIGRO: CO TÓXICO ☠️☠️☠️"));
    co_alert_sent = true;
  } else if (current_co_ppm < 30) {
    co_alert_sent = false;
  }
}

void inicializarSensores() {
  if (!bmp.begin(0x76)) {
    Serial.println(F("No se encontró el sensor BMP280."));
    while (1) delay(10);
  }
  
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, 
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X2, 
                  Adafruit_BMP280::FILTER_X4,
                  Adafruit_BMP280::STANDBY_MS_500);
  
  dht.begin();
}

void imprimirLecturas(float tempDHT, float hum, float tempBMP, float pres, float alt) {
  Serial.println("========== LECTURAS ==========");
  Serial.printf("📡 DHT22 - Temp: %.2f°C, Humedad: %.2f%%\n", tempDHT, hum);
  Serial.printf("📡 BMP280 - Temp: %.2f°C, Presión: %.2f hPa, Altitud: %.2f m\n", tempBMP, pres, alt);

  Serial.println("💨 MQ135 - Gases Detectados:");
  Serial.printf("🏭 CO2: %.1f ppm\n", current_co2_ppm);
  Serial.printf("☠️ CO: %.1f ppm\n", current_co_ppm);
  Serial.printf("💨 NH4: %.1f ppm\n", current_nh4_ppm);
}


String generarMensajeTweet(float tempDHT, float hum, float tempBMP, float pres, float alt) {
  String tweet = "Reporte de sensores 🌡️☀️:\n";
  tweet += "DHT22 - Temp: " + String(tempDHT, 1) + "°C, Hum: " + String(hum, 1) + "%\n";
  tweet += "BMP280 - Temp: " + String(tempBMP, 1) + "°C, Presión: " + String(pres, 1) + " hPa\n";
  tweet += "MQ135 - " + obtenerEstadoMQ135() + "\n";

  // Alertas segun umbral
  String alertaCO2 = (current_co2_ppm > CO2_THRESHOLD) ? "⚠️" : "✅";
  String alertaCO  = (current_co_ppm  > CO_THRESHOLD)  ? "🚨" : "✅";
  String alertaNH4 = (current_nh4_ppm > NH4_THRESHOLD) ? "⚠️" : "✅";
  String alertaAce = (acetona_ppm     > ACETONA_THRESHOLD) ? "⚠️" : "✅";
  String alertaAlc = (alcohol_ppm     > ALCOHOL_THRESHOLD) ? "⚠️" : "✅";
  // Lectura de Gases
  tweet += "PPM: CO₂:" + String(current_co2_ppm, 1) + alertaCO2;
  tweet += " CO:" + String(current_co_ppm, 1) + alertaCO;
  tweet += " NH₄:" + String(current_nh4_ppm, 1) + alertaNH4;
  tweet += " Ace:" + String(acetona_ppm, 1) + alertaAce;
  tweet += " Alc:" + String(alcohol_ppm, 1) + alertaAlc + "\n";

  tweet += "#RoboticaII #Episi";
  return tweet;
}

/* Enviar a TS solo con wifi
void enviarDatosThingSpeak(float tempDHT, float hum, float pres, float current_co2_ppm, float current_co_ppm, 
                           float current_nh4_ppm, float alcohol_ppm, String estadoMQ135) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi no conectado. No se envían datos.");
    return;
  }

  HTTPClient http;
  String url = "http://api.thingspeak.com/update?api_key=" + apiKey;

  url += "&field1=" + String(tempDHT, 1);                       // Temp DHT22
  url += "&field2=" + String(hum, 1);                           // Humedad
  url += "&field3=" + String(pres, 1);                          // Presión BMP280
  url += "&field4=" + String(current_co2_ppm, 1);              // CO₂ ppm
  url += "&field5=" + String(current_co_ppm, 1);               // CO ppm
  url += "&field6=" + String(current_nh4_ppm, 1);              // NH₄ ppm
  url += "&field7=" + String(alcohol_ppm, 1);                  // Alcohol ppm
  url += "&field8=" + String((obtenerEstadoMQ135().indexOf("alta") >= 0) ? 1 : 0); // 1 = alerta

  Serial.println("📡 Enviando a ThingSpeak...");
  Serial.println(url);

  http.begin(url);
  int httpCode = http.GET();
  http.end();

  if (httpCode == 200) {
    Serial.println("✅ Datos enviados correctamente a ThingSpeak.");
  } else if (httpCode > 0) {
    Serial.print("⚠️ Error HTTP: ");
    Serial.println(httpCode);
  } else {
    Serial.print("❌ Error de conexión: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }
}
*/

void enviarDatosThingSpeak(float tempDHT, float hum, float pres,
                            float current_co2_ppm, float current_co_ppm,
                            float current_nh4_ppm, float alcohol_ppm,
                            String estadoMQ135) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi no conectado. No se envían datos por MQTT.");
    return;
  }

  if (!mqttClient.connected()) {
    Serial.println("Reconectando al servidor MQTT...");
    if (mqttClient.connect(mqttClientID, mqttUser, mqttPassword)) {
      Serial.println("Conectado a MQTT.");
    } else {
      Serial.print("Falló la conexión MQTT. Código: ");
      Serial.println(mqttClient.state());
      return;
    }
  }

  // Topic y payload para publicar
  String topic = "channels/" + String(channelID) + "/publish";

  String payload = 
      "field1=" + String(tempDHT, 1) +
      "&field2=" + String(hum, 1) +
      "&field3=" + String(pres, 1) +
      "&field4=" + String(current_co2_ppm, 1) +
      "&field5=" + String(current_co_ppm, 1) +
      "&field6=" + String(current_nh4_ppm, 1) +
      "&field7=" + String(alcohol_ppm, 1) +
      "&field8=" + String((estadoMQ135.indexOf("alta") >= 0) ? 1 : 0);

  Serial.println("Publicando por MQTT:");
  Serial.println("Topic: " + topic);
  Serial.println("Payload: " + payload);

  if (mqttClient.publish(topic.c_str(), payload.c_str())) {
    Serial.println("✅ Datos enviados correctamente por MQTT a ThingSpeak.");
  } else {
    Serial.println("❌ Error al publicar datos por MQTT.");
  }

  mqttClient.loop(); // Procesa la conexión

    // Después del envío por MQTT
  String url = "https://script.google.com/macros/s/AKfycbxaUWYJiL43mYr47kxj4BohwBDGk9hBCHxeUZdwPp2YhYF3a5Hp2608LXiGlFBZiYIA/exec";
  url += "?temp=" + String(tempDHT);
  url += "&hum=" + String(hum);
  url += "&pres=" + String(pres);
  url += "&co2=" + String(current_co2_ppm);
  url += "&co=" + String(current_co_ppm);
  url += "&nh4=" + String(current_nh4_ppm);
  url += "&alcohol=" + String(alcohol_ppm);
  url += "&estado=" + estadoMQ135;

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.println("✅ Datos también enviados a Google Sheets.");
  } else {
    Serial.println("❌ Error al enviar datos a Sheets. Código HTTP: " + String(httpCode));
  }
  http.end();

}


String urlEncode(const String& str) {
  String encoded;
  encoded.reserve(str.length() * 3);
  for (int i = 0; i < str.length(); i++) {
    char c = str[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') 
        || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      encoded += '%';
      char hex[3];
      sprintf(hex, "%02X", c);
      encoded += hex;
    }
  }
  return encoded;
}

String calcularHMACSHA1(const String& key, const String& message) {
  unsigned char hmac[20];
  unsigned char base64Hmac[64];
  size_t base64Len = 0;

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)message.c_str(), message.length());
  mbedtls_md_hmac_finish(&ctx, hmac);
  mbedtls_md_free(&ctx);

  mbedtls_base64_encode(base64Hmac, sizeof(base64Hmac), &base64Len, hmac, sizeof(hmac));
  base64Hmac[base64Len] = '\0';

  return String((char*)base64Hmac);
}


String crearEncabezadoOAuth(const String& mensajeTweet) {
  // Generate nonce and timestamp
  String nonce = String(random(1000000000));
  String timestamp = String(time(nullptr));

  String paramString = 
    "oauth_consumer_key=" + urlEncode(API_KEY) + 
    "&oauth_nonce=" + urlEncode(nonce) +
    "&oauth_signature_method=HMAC-SHA1" +
    "&oauth_timestamp=" + urlEncode(timestamp) +
    "&oauth_token=" + urlEncode(ACCESS_TOKEN) +
    "&oauth_version=1.0";

  String baseString = 
    "POST&" + 
    urlEncode("https://api.twitter.com/2/tweets") + 
    "&" + 
    urlEncode(paramString);

  String signingKey = 
    urlEncode(API_SECRET) + 
    "&" + 
    urlEncode(ACCESS_TOKEN_SECRET);


  String signature = calcularHMACSHA1(signingKey, baseString);

  // Construct OAuth header
  String header = "OAuth ";
  header += "oauth_consumer_key=\"" + urlEncode(API_KEY) + "\", ";
  header += "oauth_nonce=\"" + urlEncode(nonce) + "\", ";
  header += "oauth_signature=\"" + urlEncode(signature) + "\", ";
  header += "oauth_signature_method=\"HMAC-SHA1\", ";
  header += "oauth_timestamp=\"" + urlEncode(timestamp) + "\", ";
  header += "oauth_token=\"" + urlEncode(ACCESS_TOKEN) + "\", ";
  header += "oauth_version=\"1.0\"";

  return header;
}


bool enviarTweet(const String& mensajeTweet) {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
    return false;
  }

  HTTPClient http;
  const char* url = "https://api.twitter.com/2/tweets";
  http.begin(url);

  String authHeader = crearEncabezadoOAuth(mensajeTweet);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", authHeader);


  String escapedTweet = mensajeTweet;
  escapedTweet.replace("\\", "\\\\"); 
  escapedTweet.replace("\"", "\\\"");
  escapedTweet.replace("\n", "\\n");

  String payload = "{\"text\":\"" + escapedTweet + "\"}";

  Serial.println("Debug - OAuth Header: " + authHeader);
  Serial.println("Debug - Payload: " + payload);

  int httpResponseCode = http.POST(payload);
  String response = http.getString();

  Serial.println("Código de respuesta: " + String(httpResponseCode));
  Serial.println("Respuesta: " + response);

  http.end();
  return httpResponseCode == 201;
}

