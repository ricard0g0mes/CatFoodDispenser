//#######################################################################################
//##########################                            #################################
//#######################        CAT DISPENSER - V1.1      ##############################
//###########################      RICARDO GOMES      ###################################
//###########################      WEMOS D1 MINI      ###################################
//#######################################################################################
//#######################################################################################
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "HX711.h"

HX711 scale(0, 2);    // parameter "gain" is ommited; the default value 128 is used by the library :: 0:D3 2:D4

#define MQTT_CLIENT     "FoodDispenser"
#define MQTT_SERVER     "192.168.1.133"                      // servidor mqtt
#define MQTT_PORT       1883                                 // porta mqtt
#define MQTT_TOPIC      "fooddispenser"          // topic
#define MQTT_USER       "mqttusername"                               //user
#define MQTT_PASS       "mqttpassword"                               // password

#define WIFI_SSID       "ssid"                           // wifi ssid
#define WIFI_PASS       "password"                           // wifi password

bool sendStatus = false;
bool requestRestart = false;

int kUpdFreq = 1;                                            // Frequencia verificação connecção MQTT
int kRetries = 40;                                           // Repeticoes de tentativa de ligação WiFi

unsigned long TTasks;

extern "C" { 
  #include "user_interface.h" 
}

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient, MQTT_SERVER, MQTT_PORT);

const int Motor1 =  12;      // Controlo Motor 1 - D6
int peso_atual = 0, recipiente_cheio = 0, peso_recipiente_cheio = 50; // peso_recipiente_cheio tem ainda de ser medido
int autorizacao_enchimento_diario = 0, pedido_enchimento_manual = 0;

//#######################################################################################
//#######################################################################################
//#######################################################################################
void callback(const MQTT::Publish& pub) {
  if (pub.payload_string() == "stat") {
  }
  else if (pub.payload_string() == "request_status") {
    sendStatus = true; //pedido de status do sistema
  }
  else if (pub.payload_string() == "reset") {
    requestRestart = true; // pedido de reset ao sistema 
  }
  else if (pub.payload_string() == "autorizacao_diaria") {
    autorizacao_enchimento_diario = true; // autorização diária dada pelo home assistant, na teoria apenas 1x por dia
  }
  else if (pub.payload_string() == "pedido_enchimento_manual") {
    pedido_enchimento_manual = true; // pedido de enchimento manual pelo home assistant
  }
  sendStatus = true;
}
//#######################################################################################
//#######################################################################################
//#######################################################################################
void setup() {
  Serial.begin(115200);

  pinMode(Motor1, OUTPUT);
  digitalWrite(Motor1, LOW);
  
  Serial.print("read: \t\t");
  Serial.println(scale.read());                 // print a raw reading from the ADC
  Serial.print("get units: \t\t");
  Serial.println(scale.get_units(5), 1);        // print the average of 5 readings from the ADC minus tare weight, divided 
            // by the SCALE parameter set with set_scale
  Serial.println(scale.get_units(10), 1);
  
  mqttClient.set_callback(callback);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("\nUnit ID: ");
  Serial.print("esp8266-");
  Serial.print(ESP.getChipId(), HEX);
  Serial.print("\nConnecting to "); Serial.print(WIFI_SSID); Serial.print(" Wifi"); 
  while ((WiFi.status() != WL_CONNECTED) && kRetries --) {
    delay(500);
    Serial.print(" .");
  }
  if (WiFi.status() == WL_CONNECTED) {  
    Serial.println(" DONE");
    Serial.print("IP Address is: "); Serial.println(WiFi.localIP());
    Serial.print("Connecting to ");Serial.print(MQTT_SERVER);Serial.print(" Broker . .");
    delay(500);
    while (!mqttClient.connect(MQTT::Connect(MQTT_CLIENT).set_keepalive(90).set_auth(MQTT_USER, MQTT_PASS)) && kRetries --) {
      Serial.print(" .");
      delay(1000);
    }
    if(mqttClient.connected()) {
      Serial.println(" DONE");
      Serial.println("\n----------------------------  Logs  ----------------------------");
      Serial.println();
      mqttClient.subscribe(MQTT_TOPIC);
    }
    else {
      Serial.println(" FAILED!");
      Serial.println("\n----------------------------------------------------------------");
      Serial.println();
    }
  }
  else {
    Serial.println(" WiFi FAILED!");
    Serial.println("\n----------------------------------------------------------------");
    Serial.println();
    while(1);
  }
}
//#######################################################################################
//#######################################################################################
//####################################################################################### 
void loop() {
  mqttClient.loop();
  timedTasks();
  checkStatus();
  if(recipiente_cheio == 0 && autorizacao_enchimento_diario == 1 || pedido_enchimento_manual == 1){
	  dispensar();
	}
}
//############################################################################################
//############################################################################################
//############################################################################################
void checkConnection() {
  if (WiFi.status() == WL_CONNECTED)  {
    if (mqttClient.connected()) {
      Serial.println("mqtt broker . . . . . . . . . . OK");
    } 
    else {
      Serial.println("mqtt broker . . . . . . . . . . LOST");
      requestRestart = true;
    }
  }
  else { 
    Serial.println("WiFi . . . . . . . . . . LOST");
    requestRestart = true;
  }
}
//############################################################################################
//############################################################################################
void checkStatus() {
  if (sendStatus) {
    if(recipiente_cheio == 1)  {
      mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/stat", "cheio").set_retain().set_qos(1));
      Serial.println("Recipiente ainda tem comida");
    } 
    else if(recipiente_cheio == 0) {
      mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/stat", "vazio").set_retain().set_qos(1));
      Serial.println("Recipiente vazio");
    }
    
    mqttClient.publish(MQTT::Publish(MQTT_TOPIC"/quantidade_actual_recipiente", String(peso_atual, 2)).set_retain().set_qos(1));
    Serial.print("Quantidade atual de comida . . . . . . . . . . . . . . . . . .");
    Serial.println(String(peso_atual, 2));
    sendStatus = false;
  }
  if (requestRestart) {
    ESP.restart();
  }
}
//############################################################################################
//############################################################################################
void timedTasks() {
  if ((millis() > TTasks + (kUpdFreq*60000)) || (millis() < TTasks)) { 
    TTasks = millis();
    verificar_peso_atual();
    Serial.print("Quantidade atual: ");
    Serial.print(peso_atual);
    checkConnection();
    sendStatus = true;
  }

}
//############################################################################################
//############################################################################################
//############################################################################################
void verificar_peso_atual(){
  scale.power_up();
  peso_atual = scale.get_units(10);
  delay(1000);
  scale.power_down();              // put the ADC in sleep mode
  if(peso_atual < (peso_recipiente_cheio/10)) recipiente_cheio = 0;
}
//############################################################################################
//############################################################################################
//############################################################################################
//############################################################################################
void dispensar() {
  Serial.println("Comida...comida...comida...");
  digitalWrite(Motor1, HIGH);
  while(scale.get_units(10)<=peso_recipiente_cheio) delay(1000);
  digitalWrite(Motor1, LOW);
  recipiente_cheio = 1;
  pedido_enchimento_manual = 0;
}
