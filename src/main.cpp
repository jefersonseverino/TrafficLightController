#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#define WDT_TIMEOUT 20

const char *ssid = "";
const char *password = "";
const char *mqtt_server = "";
const char *mqtt_user = "";
const char *mqtt_password = "";
const char *topic_dashboard = "dashboard/traffic_control";
const char *client_id = "TrafficController_ESP32";

WiFiClient espClient;
PubSubClient client(espClient);

const int PIN_A_S1 = 23, PIN_A_S2 = 22, PIN_A_S3 = 21; // PIN_A_S1 usado para contagem ISR
const int PIN_B_S1 = 34, PIN_B_S2 = 35, PIN_B_S3 = 32; // PIN_B_S1 usado para contagem ISR (INVERTIDO)
const int PIN_BTN_S1 = 14; 
const int PIN_BTN_S2 = 12; 

const long T_LIVRE_MIN = 3000;    
const long T_LEVE = 4000;         
const long T_MODERADO = 7000;     
const long T_INTENSO = 10000;     

const long TEMPO_AMARELO = 2000;     
const long TEMPO_PEDESTRE = 5000;    
const long INTERVALO_MQTT = 1000;    
const long DEBOUNCE_TIME_ISR = 200; // Tempo mínimo entre contagens (200ms)

enum EstadoSinal { 
  S1_VERDE, 
  S1_AMARELO, 
  S2_VERDE, 
  S2_AMARELO, 
  VERMELHO_PEDESTRE 
};

EstadoSinal estadoAtual = S1_VERDE;

EstadoSinal ultimoVerde = S1_VERDE; 
unsigned long tempoUltimaTroca = 0;
unsigned long tempoUltimaPublicacao = 0;
unsigned long lastPedestrePressTime = 0;
bool pedestreAtivo = false;
int trafegoA = 0;
int trafegoB = 0;

volatile long carCounter_A_ISR = 0;
volatile long carCounter_B_ISR = 0;
volatile unsigned long last_ISR_A = 0;
volatile unsigned long last_ISR_B = 0;

volatile bool contagemAtivaA = false;
volatile bool contagemAtivaB = false;

void IRAM_ATTR detectaCarroA() {
    if (contagemAtivaA) {
        unsigned long now = millis();
        if (now - last_ISR_A > DEBOUNCE_TIME_ISR) {
            carCounter_A_ISR++;
            last_ISR_A = now;
        }
    }
}

void IRAM_ATTR detectaCarroB() {
    if (contagemAtivaB) {
        unsigned long now = millis();
        if (now - last_ISR_B > DEBOUNCE_TIME_ISR) {
            carCounter_B_ISR++;
            last_ISR_B = now;
        }
    }
}

void setup_wifi() {
  Serial.print("Conectando WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); Serial.print("."); 
    esp_task_wdt_reset();
  }
  Serial.println("\nWiFi Conectado!");
}

void reconnect() {
  if (client.connect(client_id, mqtt_user, mqtt_password)) { Serial.println("MQTT Conectado!"); }
}

bool checkIR(int pin, bool inverted = false) {
  return digitalRead(pin) == (inverted ? LOW : HIGH);
}

int calculateTrafficLevel(int p1, int p2, int p3, int invertedIRPin = -1) {
  int cnt = 0;
  if (checkIR(p1, invertedIRPin == p1)) cnt++;
  if (checkIR(p2, invertedIRPin == p2)) cnt++;
  if (checkIR(p3, invertedIRPin == p3)) cnt++;
  return cnt;
}

void updateSensorInformation() {
  trafegoA = calculateTrafficLevel(PIN_A_S1, PIN_A_S2, PIN_A_S3);
  trafegoB = calculateTrafficLevel(PIN_B_S1, PIN_B_S2, PIN_B_S3, PIN_B_S3);
}

long calcularTempoVerde(int nivelTransito) {
  if (nivelTransito >= 3) return T_INTENSO;   
  if (nivelTransito == 2) return T_MODERADO;  
  if (nivelTransito == 1) return T_LEVE;      
  return T_LIVRE_MIN;                         
}

String translateTrafficLevel(int nivel) {
  if (nivel == 0) return "LIVRE";
  if (nivel == 1) return "LEVE";
  if (nivel == 2) return "MODERADO";
  return "INTENSO";
}

void sendMQTTState(bool pedestreAtivo = false) {
  StaticJsonDocument<256> doc;
  
  noInterrupts();
  long totalA = carCounter_A_ISR;
  long totalB = carCounter_B_ISR;
  interrupts();
  
  switch(estadoAtual) {
    case S1_VERDE: doc["estado"] = "S1_VERDE"; break;
    case S1_AMARELO: doc["estado"] = "S1_AMARELO"; break;
    case S2_VERDE: doc["estado"] = "S2_VERDE"; break;
    case S2_AMARELO: doc["estado"] = "S2_AMARELO"; break;
    default: doc["estado"] = "VERMELHO_TOTAL"; break;
  }
  if (estadoAtual == S1_VERDE || estadoAtual == S1_AMARELO) doc["transito"] = translateTrafficLevel(trafegoA);
  else if (estadoAtual == S2_VERDE || estadoAtual == S2_AMARELO) doc["transito"] = translateTrafficLevel(trafegoB);
  else doc["transito"] = "LIVRE"; 
  
  doc["ambulancia"] = false;
  doc["pedestre"] = pedestreAtivo;
  
  doc["carros_total"] = totalA + totalB;
  doc["carros_A"] = totalA;
  doc["carros_B"] = totalB; 

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(topic_dashboard, buffer);
}

void mudarPara(EstadoSinal novoEstado, bool porPedestre = false) {
  estadoAtual = novoEstado;
  tempoUltimaTroca = millis();
  
  contagemAtivaA = (novoEstado == S1_VERDE);
  contagemAtivaB = (novoEstado == S2_VERDE);

  sendMQTTState(porPedestre); 
}

void setup() {
  Serial.begin(9600);

  esp_task_wdt_init(WDT_TIMEOUT, true);
  esp_task_wdt_add(NULL);

  pinMode(PIN_A_S1, INPUT); pinMode(PIN_A_S2, INPUT); pinMode(PIN_A_S3, INPUT);
  pinMode(PIN_B_S1, INPUT); pinMode(PIN_B_S2, INPUT); pinMode(PIN_B_S3, INPUT);
  pinMode(PIN_BTN_S1, INPUT_PULLUP); pinMode(PIN_BTN_S2, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(PIN_A_S1), detectaCarroA, FALLING); 
  attachInterrupt(digitalPinToInterrupt(PIN_B_S1), detectaCarroB, FALLING); 

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  updateSensorInformation();
  
  if (trafegoA > trafegoB) { mudarPara(S1_VERDE); ultimoVerde = S1_VERDE; }
  else { mudarPara(S2_VERDE); ultimoVerde = S2_VERDE; }
}

void loop() {
  esp_task_wdt_reset();

  if (!client.connected()){ 
    reconnect();
  }

  client.loop(); 
  
  unsigned long timeNow = millis();
  
  bool pedestreEmS1 = digitalRead(PIN_BTN_S1) == LOW; 
  bool pedestreEmS2 = digitalRead(PIN_BTN_S2) == LOW; 

  switch (estadoAtual) {
    case S1_VERDE:
      if (pedestreEmS1 && (timeNow - lastPedestrePressTime > 10000)) {
         ultimoVerde = S1_VERDE;
         pedestreAtivo = true;
         lastPedestrePressTime = timeNow;
         mudarPara(S1_AMARELO, true);
      } else {
         updateSensorInformation();
         long tempoLimite = calcularTempoVerde(trafegoA);
         if (timeNow - tempoUltimaTroca >= tempoLimite) {
             if (trafegoB > 0) {
                 ultimoVerde = S1_VERDE; 
                 mudarPara(S1_AMARELO, false);
             } 
         }
      }
      break;

    case S1_AMARELO:
      if (timeNow - tempoUltimaTroca >= TEMPO_AMARELO) {
        mudarPara(VERMELHO_PEDESTRE);
      }
      break;

    case S2_VERDE:
      if (pedestreEmS2 && (timeNow - lastPedestrePressTime > 10000)) {
         Serial.println("Pedestre S2 -> Amarelo");
         ultimoVerde = S2_VERDE; 
         pedestreAtivo = true;
         lastPedestrePressTime = timeNow;
         mudarPara(S2_AMARELO, true);
      }
      else {
         updateSensorInformation();
         long tempoLimite = calcularTempoVerde(trafegoB);
         if (timeNow - tempoUltimaTroca >= tempoLimite) {
             if (trafegoA > 0) {
                 Serial.println("Tempo S2 acabou. S1 tem fila. Trocando.");
                 ultimoVerde = S2_VERDE; 
                 mudarPara(S2_AMARELO, false);
             }
         }
      }
      break;

    case S2_AMARELO:
      if (timeNow - tempoUltimaTroca >= TEMPO_AMARELO) mudarPara(VERMELHO_PEDESTRE);
      break;

    case VERMELHO_PEDESTRE:
      if (timeNow - tempoUltimaTroca >= TEMPO_PEDESTRE) {
        updateSensorInformation(); 
        Serial.println("Fim do Vermelho Total. Decidindo...");

        if (pedestreAtivo) {
            EstadoSinal proximoEstado = (ultimoVerde == S1_VERDE) ? S1_VERDE : S2_VERDE;
            Serial.printf("Pedestre recente em S%d. Mantém Verde.\n", (proximoEstado == S1_VERDE ? 1 : 2));
            mudarPara(proximoEstado, false);
            pedestreAtivo = false;
            return;
        }

        if (ultimoVerde == S1_VERDE) {
            if (trafegoB > 0) {
                Serial.println("Vez do S2 (Rotação).");
                mudarPara(S2_VERDE);
            } else {
                Serial.println("Vez do S2, mas está vazio. Mantém S1.");
                mudarPara(S1_VERDE);
            }
        } 
        else {
          if (trafegoA > 0) {
                Serial.println("Vez do S1 (Rotação).");
                mudarPara(S1_VERDE);
            } else {
                Serial.println("Vez do S1, mas está vazio. Mantém S2.");
                mudarPara(S2_VERDE);
            }
        }
      }
      break;
  }

  if (timeNow - tempoUltimaPublicacao >= INTERVALO_MQTT) {
    updateSensorInformation();
    sendMQTTState(false); 
    tempoUltimaPublicacao = timeNow;
  }
}