#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

//    OBSCURITE LED D2 éteinte = 1
//    LUMIÈRE LED D2 allumée = 0

/* ESPSmartMeter
 *
 * Mesure les impulsions lumineuses d'un compteur électrique, et convertit cette
 * mesure en Watts (en considérant 1Wh par impulsion).
 */


//Numéro de broche de l'entrée numérique
#define NUM_PIN D5
//intervalle de temps minimal entre deux changements d'états (en ms)
#define MIN_TEMPS_REBOND 100
//Intervalle de temps entre deux calculs de puissance (ms)
#define TEMPS_MESURE 60*1000

//Mot clé "volatile" car modifiées par une interruption
//Nombre d'impulsions mesurées
volatile uint32_t n_impulsions = 0;
//Instant auquel le dernier changement d'état à eu lieu
volatile uint32_t inst_anti_rebond = 0;
//Instant auquel la mesure courante a commencé
uint32_t inst_debut_mesure = 0;

//Réseau
//WiFi
const char* ssid     = "SSID";
const char* password = "PASS";

//Site web pour visualiser les données : io.adafruit.com
//l'API key est obtenue à partir de io.adafruit.com, en créant un "feed"
const char* apikey = "APIKEY"; 
//obtenue a partir du navigateur web et du certificat de io.adafruit.com
const char fingerprint[] PROGMEM = "77:00:54:2D:DA:E7:D8:03:27:31:23:99:EB:27:DB:CB:A5:4C:57:18";
//adresse pour poster la donnée : LOGIN est le nom d'utilisateur sur io.adafruit
//et puissance le nom du feed que nous avons crée
String url = "https://io.adafruit.com/api/v2/LOGIN/feeds/puissance/data";

HTTPClient http;
WiFiClientSecure client;

void setup()
{
  //Mise en place de la liaison série
  Serial.begin(9600);

  //Configure an interrupt on rising edge of NUM_PIN
  pinMode(NUM_PIN, INPUT);

  //Connection au réseau WiFi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  client.setFingerprint(fingerprint);

  //Association de la fonction interrupt_impulsion à chaque front montant
  //détecté sur la broche d'entrée numérique
  attachInterrupt(digitalPinToInterrupt(NUM_PIN), interrupt_impulsion, RISING);

  //On commence la mesure maintenant
  inst_debut_mesure = millis();

}

void loop()
{
  //Attendre TEMPS_MESURE ms
  delay(TEMPS_MESURE);

  //Faire la mesure
  float puissance = calc_puissance();
  prepare_mesure();
  
  //Afficher la mesure
  Serial.println(puissance);

  //Transmission de la mesure
  envoie(puissance);
}

//Envoie la donnée en paramètre au serveur emoncms
void envoie(float data)
{
  Serial.print("connecting to ");
  Serial.println(url);

  //Le payload est au format json de type : {"value" : <valeur>}
  String payload = String("{\"value\": \"") + String(data) + String("\"}");
  http.begin(client, url);
  //On ajoute les 2 entêtes dans la requête HTTP : le type du paylod (json) et la clef pour le serveur
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-AIO-Key", apikey);
  //Enfin on émet le "post" HTTP et on devrait recevoir un 200 si la requête a été acceptée par le serveur
  int httpCode = http.POST(payload);
  Serial.print("HTTP POST Response: "); Serial.println(httpCode);
  http.end();
}

//Ajoute 1 à n_impulsions quand une impulsion est détectée
void interrupt_impulsion()
{
  //Récupération de l'instant courant
  uint32_t curr_timestamp = millis();
  //Calcul de la durée entre maintenant et le dernier changement d'état
  uint32_t delta_t = calc_delta_t(inst_anti_rebond, curr_timestamp);

  //Mise à jour de l'instant du dernier changement d'état
  inst_anti_rebond = curr_timestamp;

  //S'il n'y a pas eu de rebond depuis DEBOUNCE_TIME ms
  if(delta_t >= MIN_TEMPS_REBOND) {
    //...alors augmenter de 1 le nombre d'impulsions
    ++n_impulsions;
  }
}

//Calcul la puissance consommée
float calc_puissance()
{
  //Calcul de l'intervalle de temps exact entre le début de la mesure
  //et maintenant
  float delta_t = (float)calc_delta_t(inst_debut_mesure, millis());

  //P [W] = E[J] / t[s] = (E[Wh] * 3600) / (t[ms] * 0.001)
  //P [W] = 3600000 * E[Wh]/t[ms])
  return (3600000.0 * n_impulsions)/delta_t;
}

//Met en place une nouvelle mesure
void prepare_mesure()
{
  //Désactive les interruptions : évite qu'une interruption ne vienne acceder à
  //n_impulsions et inst_debut mesure pendant qu'on les modifie.
  noInterrupts();

  n_impulsions = 0;
  inst_debut_mesure = millis();

  interrupts();
}

//Calcule une différence temporelle en prenant en compte un possible retour à
//zéro du timer
uint32_t calc_delta_t(uint32_t start_time, uint32_t stop_time)
{
  //S'il n'y a pas eu de retour à zéro
  if(stop_time >= start_time) {
    return (stop_time - start_time);
  }
  //S'il y a eu de retour à zéro
  else {
    //OxFFFFFFFF est la valeur maximale d'un uint32_t
    return (0xFFFFFFFF - start_time + stop_time);
  }
}
