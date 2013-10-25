
////////////////////////////////////////////
//// Chargement des librairies USB et PTP pour le controle de l'appareil photo

#include <inttypes.h>
#include <avr/pgmspace.h>

#include <avrpins.h>
#include <max3421e.h>
#include <usbhost.h>
#include <usb_ch9.h>
#include <Usb.h>
#include <usbhub.h>
#include <address.h>

#include <message.h>

#include <ptp.h>
#include <canoneos.h>

#define SHUTTER_SPEED_BULB       0x0c

class CamStateHandlers : 
public EOSStateHandlers
{
  enum CamStates { 
    stInitial, stDisconnected, stConnected                                                   };
  CamStates stateConnected;

public:
  CamStateHandlers() : 
  stateConnected(stInitial) {
  };

  virtual void OnDeviceDisconnectedState(PTP *ptp);
  virtual void OnDeviceInitializedState(PTP *ptp);
} 
CamStates;

USB                 Usb;
USBHub              Hub1(&Usb);
CanonEOS            Eos(&Usb, &CamStates);
int camConnected = 0;

void CamStateHandlers::OnDeviceDisconnectedState(PTP *ptp)
{
  if (stateConnected == stConnected || stateConnected == stInitial)
  {
    stateConnected = stDisconnected;
    E_Notify(PSTR("Camera disconnected\r\n"),0x80);
    camConnected = 0;
  }
}

void CamStateHandlers::OnDeviceInitializedState(PTP *ptp)
{
  if (stateConnected == stDisconnected || stateConnected == stInitial)
  {
    stateConnected = stConnected;
    E_Notify(PSTR("Camera connected\r\n"),0x80);
    camConnected = 1;

    uint16_t rc = ((CanonEOS*)ptp)->SetProperty(EOS_DPC_ShutterSpeed,SHUTTER_SPEED_BULB);

    if (rc != PTP_RC_OK)
      ErrorMessage<uint16_t>("Error", rc);
  }
}


////////////////////////////////////////////
//// Chargement de la librairie accelStepper pour controler les moteurs pas à pas

#include <AccelStepper.h>
AccelStepper stepper(1,3,2);


////////////////////////////////////////////
//// Chargement de la librairie UDP pour le transfert de données via Ethernet

#include <SPI.h>         // needed for Arduino versions later than 0018
#include <Ethernet.h>
#include <EthernetUdp.h>         // UDP library from: bjoern@cs.stanford.edu 12/30/2008

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {  

  0x90, 0xA2, 0xDA, 0x0E, 0xBF, 0xD8
//  0x90, 0xA2, 0xDA, 0x0E, 0xC0, 0x60
  //0x90, 0xA2, 0xDA, 0x0E, 0xC0, 0x9F
  //0x90, 0xA2, 0xDA, 0x0E, 0xBF, 0xAD 
};
IPAddress ip(

 192, 168, 2, 21
//  192, 168, 2, 22
 // 192, 168, 2, 23
 // 192, 168, 2, 24
);

boolean isMaster = true; // correspond à l'arduino connecté à l'USB Camera, 
// il renvoie aussi les données du Stepper à Processing

unsigned int localPort = 6100;      // local port to listen on

// buffers for receiving and sending data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,
char  ReplyBuffer[] = "received";       // a string to send back

// An EthernetUDP instance to let us send and receive packets over UDP
EthernetUDP Udp;


////////////////////////////////////////////
//// Chargement de la librairie NeoPixel pour le controle des leds

#include <Adafruit_NeoPixel.h>

// Parameter 1 = number of pixels in strip
// Parameter 2 = pin number (most are valid)
// Parameter 3 = pixel type flags, add together as needed:
//   NEO_RGB     Pixels are wired for RGB bitstream
//   NEO_GRB     Pixels are wired for GRB bitstream
//   NEO_KHZ400  400 KHz bitstream (e.g. FLORA pixels)
//   NEO_KHZ800  800 KHz bitstream (e.g. High Density LED strip)
int nbLeds = 60;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(nbLeds, 5, NEO_GRB + NEO_KHZ800);

/////////////////////////////////////
//// Définition des variables

int runStepper = 0;
boolean stripStatus = true;

int diffCounter = 0;
float stepperSpeed = 27; //26.666 * 1;
int stepResolution = 4;
int stepperRevolution = 1600;
int stepperGoal = -stepperRevolution;
int stepperPos = 0;


/////////////////////////////////////
//// Initialisation
void setup()
{ 


  //// Test de la connexion du du shield USB
  if (Usb.Init() == -1)
  {
    //// USB shield non connecté
    strip.setPixelColor(0, 0,0,127); ////bleu
  } 
  else if (Usb.Init() == 0)
  {
    //// USB shield connecté
    strip.setPixelColor(0, 0,127,0); ////vert
    //isMaster = true;
  }

  delay( 200 );

  //// Initialisation du ruban de leds
  strip.begin();
  strip.show();

  // Démarrage de l'Ethernet et UDP:
  Ethernet.begin(mac,ip);
  Udp.begin(localPort);

  //// Initialisation du moteur pas à pas
  stepper.setMaxSpeed(1000);
  stepper.setSpeed(stepperSpeed);	
}


/////////////////////////////////////
//// Execution
void loop()
{

  //// Test de la connexion de l'appareil photo
  if (isMaster && camConnected == 0)
  {
    Usb.Task();
    strip.setPixelColor(0, 127,0,0); ////rouge
    strip.show();
  } 

  //// Si l'appareil photo est connecté ou 
  //// ou si ce n'est pas le Master
  else if ( (isMaster && camConnected == 1) || (isMaster == false))
  {

    //// Attente démarrage process
    if (runStepper == 0){
         for(uint16_t i=0; i<nbLeds; i++) {
            strip.setPixelColor(i, i<stepperSpeed? 0xffffff : 0);
          }
          
      strip.setPixelColor(0, 255,255,255); ////blanc
      strip.show();
    } 

    //// Process d'affichage des couleurs
    else if (runStepper == 1){

      if (stepperPos == 0 && isMaster) Eos.StartBulb();

      int stepperDiff = stepper.currentPosition() - stepperPos;
      if (abs(stepperDiff) >=stepResolution){
        diffCounter ++;


        if(isMaster){

          Udp.beginPacket("192.168.2.7", 6000);
          char stepperPosStr[7] = {
            0                                                                                                                                                                                                                                                      }; //Min value in an int is -32768, so need to make sure there is room for 6characters+null
          sprintf(stepperPosStr,"%d",stepper.currentPosition()+stepResolution); //convert int to char*
          Udp.write(stepperPosStr);
          Udp.endPacket();

        }

        stepperPos = stepper.currentPosition();


        if (stepper.currentPosition() == stepperGoal){
          
        
          if(isMaster) Eos.StopBulb();

          delay(3000);
          
       
          
          
          
          stepper.setCurrentPosition(0);
          stepperPos = 0;
          runStepper = 0;
        }
      }

      stepper.moveTo(stepperGoal);
      stepper.setSpeed(stepperSpeed);
      stepper.runSpeedToPosition();
    }
  
  /*
    //// Remise en position initiale si rotation continue impossible
    else if (runStepper == 2){
      if (stripStatus) {
        for(uint16_t i=0; i<nbLeds; i++) {
          strip.setPixelColor(i, 0,0,0);
        }
        strip.show();
        stripStatus = false;
      }

      stepper.moveTo(0);
      stepper.setSpeed(stepperSpeed);
      stepper.runSpeedToPosition();

      if (stepper.currentPosition() == 0){
        stepperPos = 0;
        runStepper = 0;
      }
    }
*/
    //// Ecoute des données transmises par Processing
    checkEthernet();
  }


}


/////////////////////////////////////
//// Ecoute des communications UDP

void checkEthernet() {

  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if(packetSize)
  {
    memset(packetBuffer, 0, UDP_TX_PACKET_MAX_SIZE);
    // read the packet into packetBufffer
    Udp.read(packetBuffer,UDP_TX_PACKET_MAX_SIZE);

    int counter = -1;
    char * pch;
    pch = strtok (packetBuffer,",");

    boolean RGBtemp = false;

    //// Si infos couleurs RGB alors affichage Led
    if (strcmp(pch, "RGB")  == 0){
      RGBtemp = true;
      while (pch != NULL && RGBtemp)
      {
        long int colorHex = strtol (pch,NULL,16);
        strip.setPixelColor(counter, colorHex);

        counter++;
        pch = strtok (NULL, ",");
      }
      strip.show();

    } 

    //// Déplacement du moteur pas à pas vers la droite
    else if (strcmp(pch, "right")  == 0){
      runStepper == 0;
      stepper.moveTo(stepperGoal);
      stepper.setSpeed(stepperSpeed);
      stepper.runSpeedToPosition();
    } 
    //// Déplacement du moteur pas à pas vers la gauche
    else if (strcmp(pch, "left")  == 0){
      runStepper == 0;
      stepper.moveTo(-stepperGoal);
      stepper.setSpeed(stepperSpeed);
      stepper.runSpeedToPosition();
    } 
    //// Démarrage du process d'affichage des couleurs
    else if (strcmp(pch, "runOn")  == 0){
      runStepper = 1;
    }
    //// Arret du process d'affichage des couleurs
    else if (strcmp(pch, "runOff")  == 0){
      runStepper = 0;
    }
    //// Retour à la position zéro du moteur pas à pas
    else if (strcmp(pch, "zero")  == 0){
      if (stepper.currentPosition() != 0) runStepper = 2;
    }
    //// Réinitialisation de la position zéro du moteur pas à pas
    else if (strcmp(pch, "set")  == 0){
      stepperPos = 0;
      stepper.setCurrentPosition(0);
    }
    //// Ouverture de l'appareil photo
    else if (strcmp(pch, "start-bulb")  == 0){
      Eos.StartBulb();
    }
    //// Fermeture de l'appareil photo
    else if (strcmp(pch, "stop-bulb")  == 0){
      Eos.StopBulb();
    }

  }

}
























