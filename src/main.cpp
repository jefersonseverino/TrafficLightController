#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstdlib>

const char *ssid = "";
const char *password = "";

const char *mqtt_server = "";
const char *mqtt_user = "";
const char *mqtt_password = "";

const char *topic_dashboard = "teste/arduino";
const char *client_id = "TrafficController_ESP32";

WiFiClient espClient;
PubSubClient client(espClient);

const int PIN_A_S1 = 23;
const int PIN_A_S2 = 22;
const int PIN_A_S3 = 21;

const int PIN_B_S1 = 34; // inverted
const int PIN_B_S2 = 35;
const int PIN_B_S3 = 32;

const int PIN_BUTTON_A = 14;
const int PIN_BUTTON_B = 12;

const long TEMPO_PADRAO = 10000;
const long TEMPO_AMARELO = 2000;
const long TEMPO_SEGURANCA = 1000;

enum Faixa { FAIXA_A, FAIXA_B, NENHUMA };
Faixa faixaVerdeAtual = NENHUMA;

String estadoString = "S1_VERDE"; // Estado atual para enviar no JSON
int trafegoA = 0;                 // 0 a 3
int trafegoB = 0;                 // 0 a 3

void setup_wifi() {
  delay(10);
  Serial.print("Conecting to: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Trying MQTT connection...");
    if (client.connect(client_id, mqtt_user, mqtt_password)) {
      Serial.println("Connected!");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 2s.");
      delay(2000);
    }
  }
}

bool checkIR(int pin, bool inverted = false) {
  return digitalRead(pin) == (inverted ? LOW : HIGH);
}

int calculateTrafficLevel(int p1, int p2, int p3, int invertedIRPin = -1) {
  int cnt = 0;
  if (checkIR(p1, invertedIRPin == p1))
    cnt++;
  if (checkIR(p2, invertedIRPin == p2))
    cnt++;
  if (checkIR(p3, invertedIRPin == p3))
    cnt++;
  return cnt;
}

void updateSensorInformation() {
  trafegoA = calculateTrafficLevel(PIN_A_S1, PIN_A_S2, PIN_A_S3);
  trafegoB = calculateTrafficLevel(PIN_B_S1, PIN_B_S2, PIN_B_S3, PIN_B_S1);
}

String translateTrafficLevel(int nivel) {
  if (nivel == 0)
    return "LIVRE";
  if (nivel == 1)
    return "LEVE";
  if (nivel == 2)
    return "MODERADO";
  return "INTENSO";
}

void sendMQTTState() {
  if (!client.connected())
    reconnect();

  StaticJsonDocument<256> doc;

  doc["estado"] = estadoString;

  if (faixaVerdeAtual == FAIXA_A) {
    doc["transito"] = translateTrafficLevel(trafegoA);
  } else {
    doc["transito"] = translateTrafficLevel(trafegoB);
  }

  doc["ambulancia"] = false;
  doc["pedestre"] = false;

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish(topic_dashboard, buffer);
}

void waitMQTT(long tempoMs) {
  long tempoInicio = millis();
  while (millis() - tempoInicio < tempoMs) {
    client.loop();
    delay(10);
  }
}

void defineTrafficLights(Faixa verde, Faixa amarelo, bool vermelhoTotal) {
  if (vermelhoTotal) {
    estadoString = "VERMELHO_TOTAL";
  } else if (amarelo == FAIXA_A) {
    estadoString = "S1_AMARELO";
  } else if (amarelo == FAIXA_B) {
    estadoString = "S2_AMARELO";
  } else if (verde == FAIXA_A) {
    estadoString = "S1_VERDE";
  } else if (verde == FAIXA_B) {
    estadoString = "S2_VERDE";
  }

  sendMQTTState();
}

void closeCurrentLane() {
  if (faixaVerdeAtual == NENHUMA)
    return;

  defineTrafficLights(NENHUMA, faixaVerdeAtual, false);
  waitMQTT(TEMPO_AMARELO);

  defineTrafficLights(NENHUMA, NENHUMA, true);
  waitMQTT(TEMPO_SEGURANCA);
}

void setup() {
  Serial.begin(9600);

  pinMode(PIN_A_S1, INPUT);
  pinMode(PIN_A_S2, INPUT);
  pinMode(PIN_A_S3, INPUT);
  pinMode(PIN_B_S1, INPUT);
  pinMode(PIN_B_S2, INPUT);
  pinMode(PIN_B_S3, INPUT);
  
  pinMode(PIN_BUTTON_A, INPUT_PULLUP); 
  pinMode(PIN_BUTTON_B, INPUT_PULLUP);

  setup_wifi();
  client.setServer(mqtt_server, 1883);

  updateSensorInformation();

  if (trafegoA == trafegoB) {
    faixaVerdeAtual = random(0, 2) == 0 ? FAIXA_A : FAIXA_B;
  } else {
    faixaVerdeAtual = (trafegoA > trafegoB) ? FAIXA_A : FAIXA_B;
  }

  defineTrafficLights(faixaVerdeAtual, NENHUMA, false);
}

void loop() {
  client.loop();

  long tempoDecorrido = 0;

  defineTrafficLights(faixaVerdeAtual, NENHUMA, false);

  while (tempoDecorrido < TEMPO_PADRAO) {
    waitMQTT(1000);
    tempoDecorrido += 1000;
    
    int buttonValA = digitalRead(PIN_BUTTON_A);
    Serial.println("Button A value: " + String(buttonValA));
    updateSensorInformation();
    if (buttonValA == LOW) {
      closeCurrentLane();

      int trafegoOposto = trafegoB;

      if (trafegoOposto > 0) {
        Faixa faixaOposta = FAIXA_B;
        defineTrafficLights(faixaOposta, NENHUMA, false);
        waitMQTT(TEMPO_PADRAO / 2);
        defineTrafficLights(NENHUMA, faixaOposta, false);
        waitMQTT(TEMPO_AMARELO);
      }

      defineTrafficLights(faixaVerdeAtual, NENHUMA, false);
      tempoDecorrido = 0;
      continue;
    }

   /* int buttonValB = digitalRead(PIN_BUTTON_B);
    updateSensorInformation();
    if (buttonValB == LOW) {
      closeCurrentLane();
      int trafegoOposto = trafegoA;
      if (trafegoOposto > 0) {
        Faixa faixaOposta = FAIXA_A;
        defineTrafficLights(faixaOposta, NENHUMA, false);
        waitMQTT(TEMPO_PADRAO / 2);
        defineTrafficLights(NENHUMA, faixaOposta, false);
        waitMQTT(TEMPO_AMARELO);
      }
      defineTrafficLights(faixaVerdeAtual, NENHUMA, false);
      tempoDecorrido = 0;
      continue;
    }*/

    sendMQTTState();
    if (abs(trafegoA - trafegoB) < 2 && trafegoA > 0 && trafegoB > 0) {
      Serial.println(
          "-> Equilibrio detectado. Encerrando ciclo verde antecipadamente.");
      break;
    }
  }

  updateSensorInformation();
  Faixa faixaOposta = (faixaVerdeAtual == FAIXA_A) ? FAIXA_B : FAIXA_A;
  int qtdOposta = (faixaVerdeAtual == FAIXA_A) ? trafegoB : trafegoA;
  int qtdAtual = (faixaVerdeAtual == FAIXA_A) ? trafegoA : trafegoB;

  Faixa proximaFaixa = faixaOposta;

  if (qtdOposta == 0 && qtdAtual > 0 || (qtdAtual == 0 && qtdOposta == 0)) {
    proximaFaixa = faixaVerdeAtual;
    Serial.println("Decisao: Oposta vazia. MANTENDO ATUAL.");
  } else {
    Serial.println("Decisao: TROCANDO de faixa.");
  }

  if (proximaFaixa != faixaVerdeAtual) {
    closeCurrentLane();
    faixaVerdeAtual = proximaFaixa;
    defineTrafficLights(faixaVerdeAtual, NENHUMA, false);
  } else {
    Serial.println(">>> Sinal continua VERDE na mesma faixa <<<");
  }
}
