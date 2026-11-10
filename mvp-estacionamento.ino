#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Pinos dos Servos e Sensores Atuais
#define SERVO_IN_PIN 4
#define SERVO_OUT_PIN 13
#define IR_ENTRADA_PIN 14
#define IR_SAIDA_PIN 27
#define IR_VAGA_1_PIN 26
#define IR_VAGA_2_PIN 25

Servo servoIn;
Servo servoOut;

// Variáveis de Estado do Estacionamento
int vagasTotais = 2;
int vagasOcupadas = 0;
int vagasLivres = 2;
String mensagemPainel = "Bem-vindo! Aproxime-se da cancela.";
bool entradaAberta = false;
bool saidaAberta = false;

// Controle do WebSocket
String ultimaMensagem = "";
int ultimasVagasOcupadas = -1;

// Função de movimento realista
void moverCancelaSuave(Servo &meuServo, int anguloInicial, int anguloFinal) {
  if (anguloInicial < anguloFinal) {
    for (int pos = anguloInicial; pos <= anguloFinal; pos++) {
      meuServo.write(pos);
      delay(15);
    }
  } else {
    for (int pos = anguloInicial; pos >= anguloFinal; pos--) {
      meuServo.write(pos);
      delay(15);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Configura os Sensores IR
  pinMode(IR_ENTRADA_PIN, INPUT);
  pinMode(IR_SAIDA_PIN, INPUT);
  pinMode(IR_VAGA_1_PIN, INPUT);
  pinMode(IR_VAGA_2_PIN, INPUT);

  // Configura os Servos
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoIn.setPeriodHertz(50);
  servoOut.setPeriodHertz(50);
  servoIn.attach(SERVO_IN_PIN, 500, 2400);
  servoOut.attach(SERVO_OUT_PIN, 500, 2400);
  
  // Posição inicial fechada
  servoIn.write(0);
  servoOut.write(0);

  // Criar própria rede Wi-Fi (Modo Access Point)
  Serial.println("\nCriando rede Wi-Fi do Estacionamento...");
  
  // Nome da rede e Senha (mínimo 8 caracteres)
  WiFi.softAP("Estacionamento_MVP", "12345678"); 
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Ponto de acesso criado! Conecte o tablet no Wi-Fi 'Estacionamento_MVP' e acesse o IP: ");
  Serial.println(IP);

  // Inicia Servidores Web e WebSocket
  server.on("/", handleRoot);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  monitorarVagas();
  controleEntrada();
  controleSaida();
  atualizarPainelTablet();
}

void monitorarVagas() {
  bool v1Ocupada = (digitalRead(IR_VAGA_1_PIN) == LOW);
  bool v2Ocupada = (digitalRead(IR_VAGA_2_PIN) == LOW);

  vagasOcupadas = (v1Ocupada ? 1 : 0) + (v2Ocupada ? 1 : 0);
  vagasLivres = vagasTotais - vagasOcupadas;
}

void controleEntrada() {
  int sensorEntrada = digitalRead(IR_ENTRADA_PIN);

  // Carro chegou na cancela
  if (sensorEntrada == LOW && !entradaAberta) {
    if (vagasLivres > 0) {
      mensagemPainel = "Acesso Liberado. Siga para a vaga.";
      moverCancelaSuave(servoIn, 0, 90);
      entradaAberta = true;
    } else {
      mensagemPainel = "Estacionamento Lotado!";
    }
  }
  
  // Carro terminou de passar
  if (sensorEntrada == HIGH && entradaAberta) {
    delay(2000);
    if (digitalRead(IR_ENTRADA_PIN) == HIGH) {
      moverCancelaSuave(servoIn, 90, 0);
      entradaAberta = false;
      mensagemPainel = (vagasLivres > 0) ? "Bem-vindo! Aproxime-se da cancela." : "Estacionamento Lotado!";
    }
  }
  
  // Atualiza painel caso lote por carro ocupando vaga sem estar no portão
  if (!entradaAberta && vagasLivres == 0) {
    mensagemPainel = "Estacionamento Lotado!";
  } else if (!entradaAberta && vagasLivres > 0) {
    mensagemPainel = "Bem-vindo! Aproxime-se da cancela.";
  }
}

void controleSaida() {
  int sensorSaida = digitalRead(IR_SAIDA_PIN);

  // Carro quer sair
  if (sensorSaida == LOW && !saidaAberta) {
    moverCancelaSuave(servoOut, 0, 90);
    saidaAberta = true;
  }
  
  // Carro terminou de passar na saída
  if (sensorSaida == HIGH && saidaAberta) {
    delay(2000);
    if (digitalRead(IR_SAIDA_PIN) == HIGH) {
      moverCancelaSuave(servoOut, 90, 0);
      saidaAberta = false;
    }
  }
}

void atualizarPainelTablet() {
  if (mensagemPainel != ultimaMensagem || vagasOcupadas != ultimasVagasOcupadas) {
    String json = "{\"mensagem\":\"" + mensagemPainel + "\",\"livres\":" + String(vagasLivres) + ",\"ocupadas\":" + String(vagasOcupadas) + "}";
    webSocket.broadcastTXT(json);
    ultimaMensagem = mensagemPainel;
    ultimasVagasOcupadas = vagasOcupadas;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    String json = "{\"mensagem\":\"" + mensagemPainel + "\",\"livres\":" + String(vagasLivres) + ",\"ocupadas\":" + String(vagasOcupadas) + "}";
    webSocket.broadcastTXT(json);
  }
}

void handleRoot() {
  // Construção do HTML usando Raw String Literal [R"rawliteral(...)rawliteral"]
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang='pt-BR'>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f4f7f6; text-align: center; padding: 20px; color: #333; }
    .painel { background: white; padding: 40px; border-radius: 12px; box-shadow: 0 8px 16px rgba(0,0,0,0.1); display: inline-block; width: 90%; max-width: 700px; margin-top: 5vh; }
    h1 { color: #2c3e50; margin-bottom: 30px; font-size: 32px; }
    .mensagem { font-size: 28px; font-weight: bold; margin-bottom: 30px; padding: 20px; background: #eef2f5; border-radius: 8px; border-left: 5px solid #3498db; min-height: 40px; }
    .vagas { display: flex; justify-content: space-around; font-size: 24px; background: #fafafa; padding: 20px; border-radius: 8px; border: 1px solid #ddd; }
    .livre { color: #27ae60; font-weight: bold; }
    .ocupada { color: #e74c3c; font-weight: bold; }
  </style>
  <script>
    var connection = new WebSocket('ws://' + location.hostname + ':81/');
    connection.onmessage = function (event) {
      var data = JSON.parse(event.data);
      var msgDiv = document.getElementById('msg');
      msgDiv.innerText = data.mensagem;
      if(data.mensagem.includes('Lotado')) { msgDiv.style.borderLeftColor = '#e74c3c'; msgDiv.style.color = '#e74c3c'; }
      else if(data.mensagem.includes('Liberado')) { msgDiv.style.borderLeftColor = '#27ae60'; msgDiv.style.color = '#27ae60'; }
      else { msgDiv.style.borderLeftColor = '#3498db'; msgDiv.style.color = '#333'; }
      document.getElementById('livres').innerText = 'Livres: ' + data.livres;
      document.getElementById('ocupadas').innerText = 'Ocupadas: ' + data.ocupadas;
    };
  </script>
</head>
<body>
  <div class='painel'>
    <h1>EstaDificil Estacionamento</h1>
    <div id='msg' class='mensagem'>Conectando ao sistema...</div>
    <div class='vagas'>
      <div>Total: 2</div>
      <div id='livres' class='livre'>Livres: -</div>
      <div id='ocupadas' class='ocupada'>Ocupadas: -</div>
    </div>
  </div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}