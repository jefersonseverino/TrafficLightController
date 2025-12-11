#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

// --- CONFIGURAÇÕES DE REDE ---
const char *ssid = "";
const char *password = "";
const char *mqtt_server = "";
const char *mqtt_user = "";
const char *mqtt_password = "";
const char *topic_dashboard = "teste/arduino";
const char *client_id = "TrafficController_ESP32";

WiFiClient espClient;
PubSubClient client(espClient);

// --- PINOS ---
const int PIN_A_S1 = 23, PIN_A_S2 = 22, PIN_A_S3 = 21;
const int PIN_B_S1 = 34, PIN_B_S2 = 35, PIN_B_S3 = 32;
const int PIN_BTN_S1 = 14; 
const int PIN_BTN_S2 = 12; 

// --- TEMPOS (ms) ---
// Tempos dinâmicos para o Verde
const long T_LIVRE_MIN = 3000;    
const long T_LEVE = 4000;         
const long T_MODERADO = 7000;     
const long T_INTENSO = 10000;     // Máximo de 10s conforme solicitado

const long TEMPO_AMARELO = 2000;     
const long TEMPO_PEDESTRE = 5000;    
const long INTERVALO_MQTT = 1000;    

enum EstadoSinal { 
  S1_VERDE, 
  S1_AMARELO, 
  S2_VERDE, 
  S2_AMARELO, 
  VERMELHO_PEDESTRE 
};

EstadoSinal estadoAtual = S1_VERDE;

// --- MEMÓRIA ANTI-STARVATION ---
// Guarda quem foi o último a ter o sinal verde para forçar a vez do outro
EstadoSinal ultimoVerde = S1_VERDE; 

unsigned long tempoUltimaTroca = 0;
unsigned long tempoUltimaPublicacao = 0;

int trafegoA = 0;
int trafegoB = 0;

void setup_wifi() {
  Serial.print("Conectando WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
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
  trafegoB = calculateTrafficLevel(PIN_B_S1, PIN_B_S2, PIN_B_S3, PIN_B_S1);
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
  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(topic_dashboard, buffer);
}

void mudarPara(EstadoSinal novoEstado, bool porPedestre = false) {
  estadoAtual = novoEstado;
  tempoUltimaTroca = millis();
  sendMQTTState(porPedestre); 
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_A_S1, INPUT); pinMode(PIN_A_S2, INPUT); pinMode(PIN_A_S3, INPUT);
  pinMode(PIN_B_S1, INPUT); pinMode(PIN_B_S2, INPUT); pinMode(PIN_B_S3, INPUT);
  pinMode(PIN_BTN_S1, INPUT_PULLUP); pinMode(PIN_BTN_S2, INPUT_PULLUP);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  updateSensorInformation();
  
  // Decide inicial
  if (trafegoA > trafegoB) { mudarPara(S1_VERDE); ultimoVerde = S1_VERDE; }
  else { mudarPara(S2_VERDE); ultimoVerde = S2_VERDE; }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop(); 
  unsigned long agora = millis();
  
  bool pedestreEmS1 = digitalRead(PIN_BTN_S1) == LOW; 
  bool pedestreEmS2 = digitalRead(PIN_BTN_S2) == LOW; 
  if (pedestreEmS1) Serial.println("DEBUG: Btn S1");
  if (pedestreEmS2) Serial.println("DEBUG: Btn S2");

  switch (estadoAtual) {
    
    // === SINAL 1 VERDE ===
    case S1_VERDE:
      if (pedestreEmS1) {
         Serial.println("Pedestre S1 -> Amarelo");
         delay(1000); 
         ultimoVerde = S1_VERDE; // Memoriza quem estava verde
         mudarPara(S1_AMARELO, true);
      }
      else {
         updateSensorInformation();
         long tempoLimite = calcularTempoVerde(trafegoA);

         // Lógica Anti-Starvation na Saída do Verde
         if (agora - tempoUltimaTroca >= tempoLimite) {
             if (trafegoB > 0) {
                 Serial.println("Tempo S1 acabou. S2 tem fila. Trocando.");
                 ultimoVerde = S1_VERDE; // Memoriza
                 mudarPara(S1_AMARELO, false);
             } 
             // Se S2 vazio, mantém S1
         }
      }
      break;

    case S1_AMARELO:
      if (agora - tempoUltimaTroca >= TEMPO_AMARELO) {
        mudarPara(VERMELHO_PEDESTRE);
      }
      break;

    // === SINAL 2 VERDE ===
    case S2_VERDE:
      if (pedestreEmS2) {
         Serial.println("Pedestre S2 -> Amarelo");
         delay(1000); 
         ultimoVerde = S2_VERDE; // Memoriza quem estava verde
         mudarPara(S2_AMARELO, true);
      }
      else {
         updateSensorInformation();
         long tempoLimite = calcularTempoVerde(trafegoB);

         // Lógica Anti-Starvation na Saída do Verde
         if (agora - tempoUltimaTroca >= tempoLimite) {
             if (trafegoA > 0) {
                 Serial.println("Tempo S2 acabou. S1 tem fila. Trocando.");
                 ultimoVerde = S2_VERDE; // Memoriza
                 mudarPara(S2_AMARELO, false);
             }
             // Se S1 vazio, mantém S2
         }
      }
      break;

    case S2_AMARELO:
      if (agora - tempoUltimaTroca >= TEMPO_AMARELO) mudarPara(VERMELHO_PEDESTRE);
      break;

    // === MODO PEDESTRE (RETORNO ANTI-STARVATION) ===
    case VERMELHO_PEDESTRE:
      if (agora - tempoUltimaTroca >= TEMPO_PEDESTRE) {
        updateSensorInformation(); 
        Serial.println("Fim do Vermelho Total. Decidindo...");

        // AQUI ESTÁ A CORREÇÃO SOLICITADA:
        // Não compara mais densidade (trafegoA > trafegoB).
        // Apenas verifica quem foi o último e passa a vez (se o próximo tiver carros).

        if (ultimoVerde == S1_VERDE) {
            // A vez seria do S2.
            if (trafegoB > 0) {
                Serial.println("Vez do S2 (Rotação).");
                mudarPara(S2_VERDE);
            } else {
                Serial.println("Vez do S2, mas está vazio. Mantém S1.");
                mudarPara(S1_VERDE);
            }
        } 
        else { // ultimoVerde == S2_VERDE
            // A vez seria do S1.
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

  if (agora - tempoUltimaPublicacao >= INTERVALO_MQTT) {
    updateSensorInformation();
    sendMQTTState(false); 
    tempoUltimaPublicacao = agora;
  }
}