#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

//Librery to DHT
#include <DHT.h>
#include <DHT_U.h>

String dId = "XXXXX";
String webhook_pass = "XXX";
String webhook_endpoint = "http://XXX:3001/api/getdevicecredentials";
const char* mqtt_server = "XXX";

//WiFi
const char* wifi_ssid = "XXX";
const char* wifi_password = "XXX";

//PINS
#define led D3

//Functions definitions
bool get_mqtt_credentials();
void check_mqtt_connection();
bool reconnect();
void process_sensors();
void process_actuators();
void send_data_to_broker();
void callback(char* topic, byte* payload, unsigned int length);
void process_incoming_msg(String topic, String incoming);
void print_stats();

//Global Vars
WiFiClient  espclient;
PubSubClient client(espclient);
long lastReconnectAttemp = 0;
long varsLastSend[20];
String last_received_msg = "";
String last_received_topic = "";
int prev_temp = 0;
int prev_hum = 0;


DHT dht (D1,DHT11);
float temp, hum, hi;


DynamicJsonDocument mqtt_data_doc(2048);

void setup() {
  
  Serial.begin(9600);
  pinMode(led, OUTPUT);

  delay(3000);
  
  Serial.print("══════════════════════════════════════════════════════════════" );

  Serial.print("\n\n");
  Serial.print("\n╔══════════════════════════╗" );
  Serial.print("\n║ 1   STARTING SYSTEM    1 ║" );
  Serial.print("\n╚══════════════════════════╝" );
  
  dht.begin();

  Serial.print("\n\n");
  Serial.print("\n╔══════════════════════════╗" );
  Serial.print("\n║ 2    WIFI CONNECTION   2 ║" );
  Serial.print("\n╚══════════════════════════╝" );
  Serial.print("\n\n");
  
  Serial.print("WiFi Connection in Progress");

  WiFi.begin(wifi_ssid, wifi_password);

  int counter = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(2000);
    Serial.print("...");
    counter++;
    
    if (counter > 10)
    {
      Serial.println(" Ups WiFi Connection Failed -> Restarting..." );
      delay(2000);
      ESP.restart();
    }
  }

  //Printing local ip
  Serial.println("\n");
  Serial.println("WiFi Connection ---------------------> SUCCESS");
  Serial.println("Local IP: " );
  Serial.println(WiFi.localIP());

  
  client.setCallback(callback);
}

void loop() {
  check_mqtt_connection();
}

void process_sensors(){

  //get temp

    temp = dht.readTemperature();
    mqtt_data_doc["variables"][1]["last"]["value"] = temp;
    mqtt_data_doc["variables"][1]["last"]["save"] = 1;

    //save temp?
    //int dif = temp - prev_temp;
    //if (dif < 0) {dif *= -1;}
    // if (dif >= 1) {
    //     mqtt_data_doc["variables"][0]["last"]["save"] = 1;
    // }else{
    //     mqtt_data_doc["variables"][0]["last"]["save"] = 0;
    // }

    //prev_temp = temp;

  //get humidity

    hum = dht.readHumidity();
    mqtt_data_doc["variables"][2]["last"]["value"] = hum;
    mqtt_data_doc["variables"][2]["last"]["save"] = 1;

    //save hum?
    //dif = hum - prev_hum;
    //if (dif < 0) {dif *= -1;}
    // if (dif >= 1) {
    //     mqtt_data_doc["variables"][1]["last"]["save"] = 1;
    // }else{
    //     mqtt_data_doc["variables"][1]["last"]["save"] = 0;
    // }

    //prev_hum = hum;

   //get heat index

     hi = dht.computeHeatIndex(temp,hum);
     mqtt_data_doc["variables"][0]["last"]["value"] = hi;
     mqtt_data_doc["variables"][0]["last"]["save"] = 1;

}

void process_actuators()
{
  if (mqtt_data_doc["variables"][4]["last"]["value"] == "true")
  {
    Serial.println("\n");
    Serial.println("true");
    Serial.println("\n");
    digitalWrite(led, HIGH);
    mqtt_data_doc["variables"][4]["last"]["value"] = "";
  }
  else if (mqtt_data_doc["variables"][5]["last"]["value"] == "false")
  {
    Serial.println("\n");
    Serial.println("false");
    Serial.println("\n");
    digitalWrite(led, LOW);
    mqtt_data_doc["variables"][5]["last"]["value"] = "";
  }

}

void process_incoming_msg(String topic, String incoming){

  last_received_topic = topic;
  last_received_msg = incoming;

  int secondClosingBracket ;
  int firstClosingBracket;
  int lastOpeningBracket;

  firstClosingBracket = topic.indexOf('/');
  secondClosingBracket = topic.indexOf('/', firstClosingBracket + 1 );
  lastOpeningBracket = topic.lastIndexOf('/');
  
  String variable = topic.substring(secondClosingBracket+1,lastOpeningBracket);

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++ ){

    if (mqtt_data_doc["variables"][i]["variable"] == variable){
      
      DynamicJsonDocument doc(256);
      deserializeJson(doc, incoming);
      mqtt_data_doc["variables"][i]["last"] = doc;

    }

  }
  
    process_actuators();

}

void callback(char* topic, byte* payload, unsigned int length){
   
   String incoming = "";

   for (int i = 0; i < length; i++){
     incoming += (char)payload[i] ;
   }

   incoming.trim();

    process_incoming_msg(String(topic), incoming);

    Serial.println(incoming);
    Serial.println(String(topic));

}

void send_data_to_broker(){

  long now = millis();

  for(int i = 0; i < mqtt_data_doc["variables"].size(); i++){

    if (mqtt_data_doc["variables"][i]["variableType"] == "output"){
      continue;
    }

    int freq = mqtt_data_doc["variables"][i]["variableSendFreq"];

    if (now - varsLastSend[i] > freq * 1000){
      varsLastSend[i] = millis();

      String str_root_topic = mqtt_data_doc["topic"];
      String str_variable = mqtt_data_doc["variables"][i]["variable"];
      String topic = str_root_topic + str_variable + "/sdata";

      String toSend = "";

      serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);

      client.publish(topic.c_str(), toSend.c_str());
      
      //Serial.println("");
      //Serial.println("TOPIC:");
      //Serial.println(topic);
      //Serial.println("VALUE:");
      //Serial.println(toSend);

       //STATS
      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;
    }
    


  }

}

bool reconnect(){

  if (!get_mqtt_credentials()){
    Serial.println("\n");
    Serial.println("Error getting mqtt credentials :( \n\n RESTARTING IN 10 SECONDS");
    delay(10000);
    return false;
  }

  //Setting up Mqtt Server
  client.setServer(mqtt_server, 1883);
  Serial.println("\n");
  Serial.print("Trying MQTT Connection");
  Serial.println("\n");
  
  String str_client_id = "device_" + dId + "_" + random(1,9999);
  const char* username = mqtt_data_doc["username"];
  const char* password = mqtt_data_doc["password"];
  String str_topic = mqtt_data_doc["topic"];

  if(client.connect(str_client_id.c_str(), username, password)){
    Serial.print("Mqtt Client Connection ---------------------> SUCCESS");
    Serial.println("\n");
    delay(2000);

    client.subscribe((str_topic + "+/actdata").c_str());
    return true;
  }else{
    Serial.print("Mqtt Client Connection Failed :( ");
    Serial.println("\n");
    return false;
  }


}

void check_mqtt_connection(){
  
  if(!client.connected()){

    if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(" WiFi Connection Failed -> Restarting...");
    delay(15000);
    ESP.restart();
  }
    
    long now = millis();

    if (now - lastReconnectAttemp > 5000){
      lastReconnectAttemp = millis();
       if(reconnect()){
         lastReconnectAttemp = 0;
       }
    }

  }else{
    client.loop();
    process_sensors();
    send_data_to_broker();
    print_stats();
  }

}

bool get_mqtt_credentials() {

  Serial.print("\n╔══════════════════════════╗" );
  Serial.print("\n║ 3    MQTT CONNECTION   3 ║" );
  Serial.print("\n╚══════════════════════════╝" );
  Serial.print("\n\n");

  Serial.print("Getting MQTT Credentials from WebHook");
  delay(1000);

  String toSend = "dId=" + dId + "&password=" + webhook_pass;

  WiFiClient client2;
  HTTPClient http;
  http.begin(client2,webhook_endpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int response_code = http.POST(toSend);


  if (response_code < 0 ) {
    Serial.print("Error Sending Post Request :( ");
    http.end();
    return false;
  }

  if (response_code != 200) {
    Serial.print("Error in response :(   e-> " + response_code);
    http.end();
    return false;
  }

  if (response_code == 200) {
    String responseBody = http.getString();
    Serial.print("\n");
    Serial.print("Mqtt Credentials Obtained  -------------> Successfully");
    Serial.print("\n");
    deserializeJson(mqtt_data_doc, responseBody);
    //http.end();
    delay(1000);
    return true; 
  }
   
}

long lastStats = 0;

void print_stats()
{
  long now = millis();

  if (now - lastStats > 2000)
  {
    lastStats = millis();

    Serial.print("\n\n");
    Serial.print("\n╔══════════════════════════╗" );
    Serial.print("\n║       SYSTEM STATS       ║" );
    Serial.print("\n╚══════════════════════════╝" );
    Serial.print("\n\n");

    Serial.println("#  Name   Var         Type   Count  Last Value");

    for (int i = 0; i < mqtt_data_doc["variables"].size(); i++)
    {

      String variableFullName = mqtt_data_doc["variables"][i]["variableFullName"];
      String variable = mqtt_data_doc["variables"][i]["variable"];
      String variableType = mqtt_data_doc["variables"][i]["variableType"];
      String lastMsg = mqtt_data_doc["variables"][i]["last"];
      long counter = mqtt_data_doc["variables"][i]["counter"];

      Serial.println(String(i) + "  " + variableFullName.substring(0,5) + "  " + variable.substring(0,10) + "  " + variableType.substring(0,5) + "  " + String(counter).substring(0,10) + "      " + lastMsg);
    }
  }
}