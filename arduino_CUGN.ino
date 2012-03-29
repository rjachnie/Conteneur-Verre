#include <SoftwareSerial.h>
#include <stdio.h>
#include <string.h>
#include <EEPROM.h>

#include "DS1302.h"
#include "SimpleTimer.h"

//Nb Bouteilles
unsigned int Nb_bouteilles;

#define DEBUG

#ifdef DEBUG
  #define DEBUG_PRINT_CHAR(x) Serial.print (x)
  #define DEBUG_PRINT(x) Serial.println (x)
  #define DEBUG_PRINTDEC(x) Serial.println(x, DEC)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTDEC(x)
#endif 


//Création de l'objet Timer
SimpleTimer timer;

//PINs pour DS21302
uint8_t CE_PIN   = 5;
uint8_t IO_PIN   = 6;
uint8_t SCLK_PIN = 7;

char buf[50];

//Flag pour état btn1
boolean btn1_state = false;

//Création de l'objet pour gestion DS1302
DS1302 rtc(CE_PIN, IO_PIN, SCLK_PIN);

// Serial:
char incoming_char=0;      //Will hold the incoming character from the Serial Port.
SoftwareSerial cell(8,9);  //Create a 'fake' serial port. Pin 8 is the Rx pin, pin 9 is the Tx pin.

// Conteneur:
char baseNumber[]="+33631424719";
char containerID=0; // l'id du conteneur courant, à modifier à chaque fois
char update1Sent=0;
char update2Sent=0;
char hourUpdate1=12;
char hourUpdate2=19;
char hourReset=0;
SimpleTimer timerUpdates;

//Fonction Lecture VCC
long readVcc() {
  long result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  return result;
}


void print_time()
{
  /* Récupération du temps */
  Time t = rtc.time();

  snprintf(buf, sizeof(buf), "%01d %04d-%02d-%02d %02d:%02d:%02d",
           t.day,
           t.yr, t.mon, t.date,
           t.hr, t.min, t.sec);
  DEBUG_PRINT(buf);

}


//interrupt quand btn1 appuyé
void isr_btn1()
{ 
  if(!btn1_state)
  {
    btn1_state=true;
    DEBUG_PRINT("Pushed");
    //on met en place le timeout
    timer.setTimeout(1000, clear_btn1);
  }
}


//interrupt quand btn2 appuyé
void isr_btn2()
{
  if(btn1_state)
  {
    btn1_state=false;
    DEBUG_PRINT("Pushed 2");
    DEBUG_PRINT("Bouteille");
    Nb_bouteilles++; 
    EEPROM.write(43, Nb_bouteilles >> 8);
    EEPROM.write(42, Nb_bouteilles & 0x00FF);
  }  
}


//On enleve le flag quand le timeout du 1er btn est atteint
void clear_btn1()
{
  if(btn1_state)
  {
    btn1_state=false;
    DEBUG_PRINT("Btn1 TimeOut");  
  }
}

// Pour envoyer le premier SMS au démarrage (id, date, nbBouteilles, batterie)
void sendPing(){
	Time t = rtc.time();
	long indBattery=readVcc();
	char str[49]; // 10 + 2 + 1 + 2 + 1 + 2 + 1 + 4 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 5 + 1 + 10 (long, on sait jamais) = 49
	sprintf(str, "NYBI;PING;%d;%d/%d/%d %d:%d:%d;%d;%d", containerID, t.date, t.mon, t.yr, t.hr, t.min, t.sec, Nb_bouteilles, indBattery);
	sendSMS(str);
}

// Pour envoyer les SMS d'update (id, date, nbBouteilles, batterie)
void sendUpdatedCounter(){
	Time t = rtc.time();
	long indBattery=readVcc();
	char str[51]; // 2 + 49 = 51
	sprintf(str, "NYBI;UPDATE;%d;%d/%d/%d %d:%d:%d;%d;%d", containerID, t.date, t.mon, t.yr, t.hr, t.min, t.sec, Nb_bouteilles, indBattery);
	sendSMS(str);
}

// Envoie un SMS
void sendSMS(char* str){
        cell.println("AT+CFUN=0");
        delay(60000);
        
	//cell.println("AT+CMGF=1"); // set SMS mode to text
	cell.print("AT+CMGS=");  // now send message...
	cell.write(34); // ASCII equivalent of "
	cell.print(baseNumber);
	cell.write(34);  // ASCII equivalent of "
	cell.write(13);  // ASCII equivalent of Carriage Return
	delay(500); // give the module some thinking time
        cell.print(str);
	cell.write(26);  // ASCII equivalent of Ctrl-Z}

        cell.println("AT+CFUN=1");
}

// Regarde si on doit envoyer une update ou remettre les compteurs d'update a zero
void checkUpdates(){
	Time t = rtc.time(); // recuperer le temps
	DEBUG_PRINT("Checking Time");
	print_time();

	if (true || (!update1Sent && t.hr >= hourUpdate1 && t.hr <= hourUpdate1+1)){
		sendUpdatedCounter();
		update1Sent=1;
		DEBUG_PRINT("Update1");
	}
	else if (!update2Sent && t.hr >= hourUpdate2 && t.hr <= hourUpdate2+1){
		sendUpdatedCounter();
		update2Sent=1;
		DEBUG_PRINT("Update2");
	}

	if (t.hr >= hourReset && t.hr <= hourReset+1){
		update1Sent=0;
		update2Sent=0;
		DEBUG_PRINT("Update Reset");
	}
}

//Setup
void setup() {
    pinMode(2, INPUT);
    pinMode(3, INPUT);
  
    //Activation Pull-up
    digitalWrite(2, HIGH);  
    digitalWrite(3, HIGH);
    
    // 0 = pin 2 ; 1 = pin 3
    attachInterrupt(0, isr_btn1, FALLING);
    attachInterrupt(1, isr_btn2, FALLING);   
    
    Serial.begin(9600);
    
    Nb_bouteilles = (EEPROM.read(43) << 8) | EEPROM.read(42);
  
    DEBUG_PRINTDEC(Nb_bouteilles);
  
    rtc.write_protect(false);
    rtc.halt(false);

    /*Init A/M/J h */
    Time t(2012, 2, 16, 19, 37, 37, 3);
	
    /* Chargement de l'heure */
    rtc.time(t);
  //EEPROM.write(43,0);
  //EEPROM.write(42, 0);
    cell.begin(19200);//9600
	DEBUG_PRINT("Starting SIM900 Communication...");//SM5100B
	delay(5000);
	cell.print("ATE1\r"); //local echo
	timerUpdates.setInterval(60000, checkUpdates);//TODO remettre 10 minutes

	// delay pour attendre la connexion
	//delay(60000);
	//sendPing();
}

//Boucle principale
void loop() {
	if(cell.available()>0){ //If a character comes in from the cellular module...
		incoming_char=cell.read();    //Get the character from the cellular serial port.
		DEBUG_PRINT_CHAR(incoming_char);  //Print the incoming character to the terminal.
	}

    timer.run();
    timerUpdates.run();
}

