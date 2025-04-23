#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <Arduino_JSON.h>

//wifi
const char *ssid = "Speroto";
const char *password = "bielzin123";

// Pinagem
const int ledVermelho = 15;
const int ledVerde = 2;
const int ledAmarelo = 4;

const int trigg = 5;
const int echo = 18;

const int buttonOn = 19;
const int buttonOff = 21;

// Configurações do tanque
const int alturaTanque = 30;   // Altura total do tanque em cm
const int nivelOff = 25;       // Nível para desligar a bomba
const int nivelOn = 10;         // Nível para ligar a bomba
const int nivelAlerta = 20;    // Nível para alerta visual

// Variáveis de estado
float nivel = 0;
float distance = 0;
bool admOrder = false;
bool autoMode = false;
bool estadoAmarelo = false;

// Temporizadores
unsigned long ultimaMedicao = 0;
unsigned long ultimoDebounce = 0;
unsigned long ultimoPisca = 0;
const unsigned long intervaloMedicao = 250; //ms
const unsigned long intervaloAmarelo = 500; //ms
const unsigned long debounceDelay = 25; //ms


// Servidor Web e WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// JSON para armazenar os dados do sensor
JSONVar readings;

// Variáveis de tempo
unsigned long lastTime = 0;
unsigned long timerDelay = 500; // Atualiza a cada 1 segundo

void leds(int vermelho, int amarelo, int verde) {
  digitalWrite(ledVermelho, vermelho);
  digitalWrite(ledAmarelo, amarelo);
  digitalWrite(ledVerde, verde);
}

void pisca_amarelo(){
  if (millis() - ultimoPisca >= intervaloAmarelo) {
        estadoAmarelo = !estadoAmarelo;
        leds(1,estadoAmarelo,0);
        ultimoPisca = millis();
      }
}

void controleLEDs() {
  if (admOrder) {
    // Modo de operação (bomba ligada)
    leds(1,0,0);
    // Piscar amarelo se nível estiver em alerta
    if (nivel >= nivelAlerta) {
        pisca_amarelo();
     } else{
        leds(1,0,0);
     }  
    
  } else {
    // Modo normal (bomba desligada)
    leds(0, 0, 1);
  }
}

float getDistance() {
  digitalWrite(trigg, LOW);
  delayMicroseconds(2);
  digitalWrite(trigg, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigg, LOW);

  long duration = pulseIn(echo, HIGH);
  distance = duration * 0.034 / 2;
  nivel = alturaTanque - distance;
  
  // Serial.print("Distância: ");
  // Serial.print(distance);
  // Serial.print("cm | Nível: ");
  // Serial.print(nivel);
  // Serial.println("cm");

  return distance;
}

void verificarBotao() {
  if (millis() - ultimoDebounce > debounceDelay) {
    // Botão LIGAR pressionado (ativa modo automático)
    if (digitalRead(buttonOn) == LOW && !autoMode) {
      autoMode = true;
      admOrder = (nivel <= nivelOn); // Define estado inicial
      Serial.println("Modo automático ATIVADO");
    }
    
    // Botão DESLIGAR pressionado (desativa tudo)
    if (digitalRead(buttonOff) == LOW) {
      autoMode = false;
      admOrder = false;
      Serial.println("Sistema DESLIGADO");
    }
    
    ultimoDebounce = millis();
  }
}


// Função para obter leituras do sensor
String getSensorReadings() {
    readings["distancia"] = alturaTanque -  getDistance();
    String jsonString = JSON.stringify(readings);
    return jsonString;
}

// Inicializa o LittleFS
void initLittleFS() {
    if (!LittleFS.begin(true)) {
        Serial.println("Erro ao montar o LittleFS!");
    }
    Serial.println("LittleFS montado com sucesso.");
}

// Inicializa o Wi-Fi
void initWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Conectando ao WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }
    Serial.println("\nWi-Fi conectado!");
    Serial.print("IP do servidor: ");
    Serial.println(WiFi.localIP());
}

// Envia os dados do sensor para todos os clientes WebSocket
void notifyClients(String sensorReadings) {
    ws.textAll(sensorReadings);
}

// Manipula mensagens WebSocket
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        String message = (char*)data;
        if (strcmp((char*)data, "getReadings") == 0) {
            String sensorReadings = getSensorReadings();
            Serial.println(sensorReadings);
            notifyClients(sensorReadings);
        }
    }
}

// Evento WebSocket
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("Cliente WebSocket #%u conectado de %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("Cliente WebSocket #%u desconectado\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

// Inicializa WebSocket
void initWebSocket() {
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

void setup() {
    Serial.begin(115200);
    pinMode(ledVermelho, OUTPUT);
    pinMode(ledVerde, OUTPUT);
    pinMode(ledAmarelo, OUTPUT);
    pinMode(trigg, OUTPUT);
    pinMode(echo, INPUT);
    
    pinMode(buttonOn, INPUT_PULLUP);
    pinMode(buttonOff, INPUT_PULLUP);

    initWiFi();
    initLittleFS();
    initWebSocket();

    // Servidor Web - Página HTML
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index.html", "text/html");
    });

    server.serveStatic("/", LittleFS, "/");

    // Inicia o servidor
    server.begin();
}

void loop() {
    if ((millis() - lastTime) > timerDelay) {
        String sensorReadings = getSensorReadings();
        //Serial.println(sensorReadings);
        notifyClients(sensorReadings);
        lastTime = millis();
    }

     verificarBotao();
     controleLEDs();

    // Lógica de controle automático
    if (autoMode) {
      if (nivel <= nivelOn) {
        admOrder = true;
      } else if (nivel >= nivelOff) {
        admOrder = false;
      }
    }

    ws.cleanupClients();

    delay(500);
}
