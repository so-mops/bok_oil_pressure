/*
 Interface to RA oil pressure gauge
 V2 completed in April 2019 by Jeff Rill
 Syntax for reading pressure:
 oil_pressure = "BOK " + "RAOIL " + "123 " + "REQUEST " + "OIL_PRES "
 
 An upgrade using Responsive Analog Input class was added to reduce noise jitter
 on the analog input.  

 An offset may be added or subtracted to the pressure reading for field calibration.
 The offset value is an unsigned int with the default value = 0.
 An offset of +/- 1 changes the display pressure by approx. +/- .28 psi
 The offset is stored in the Arduino's EEPROM.
 Syntax for adding an offset:
 "BOK " + "RAOIL " + "123 " + "REQUEST " + OIL_PRES + "SET_OFFSET " + <offset>
 Syntax for reading the offset:
 current_offset_is = "BOK " + "RAOIL " + "123 " + "REQUEST " + OIL_PRES "
 
 This program talks to a  WIKA Model A-10 4-20 mA pressure sensor 0 - 200 lbs
 Gauge output => Arduino analog input 1
 215 ohm resistor from input 1 to ground
 */

#include <Ethernet.h>
#include <stdlib.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <ResponsiveAnalogRead.h>

// NG protocal defines
#define OBSID "BOK"
#define SYSID "RAOIL"
#define NO_ERROR 0
#define OBSID_ERROR 1
#define SYSID_ERROR 2
#define REFNUM_ERROR 3
#define QUERY_ERROR 4

//All eeprom addressing is set here. 
#define EEPROM_OFFSET  0
#define EEPROM_IPADDR  8
#define EEPROM_GATEWAY 12

//Buffer limits
#define RAWIN_SIZE 150
#define RESP_SIZE 50


struct serial_data
{
  char queryType[10];
  char cmd[20];
  char args[4][50];
  unsigned short arg_count;
};

const char * ercode[] = {
   "NO ERROR",
   "OBSID ERROR",
   "SYSID ERROR",
   "REFNUM ERROR",
   "QUERY ERROR"
};

struct ng_data
{
   char sysid[20];
   char obsid[10];
   unsigned short refNum;
   char queryType[8];
   unsigned short arg_count;  
   char args[7][40];
};
struct ng_data data;
struct ng_data currCom;
char ng_response[100];
struct serial_data sdata;
char serial_in[100];
unsigned short serial_in_counter=0;


// Device specific defines
#define analogPin1 0  // oil pressure
#define analogPin2 1  // air pressure
#define txPin 7
#define rxPin 8
#define WAIT_FOR_READ 2500

// global variables for the pressure
unsigned int  val = 0;
unsigned int  raw_val = 0;
float pressure = 0;
int offset = 0;
int address = 0;
int o = 0;  //variable for testing EEPROM
int byte_offset = 0;

// global variables for the LED display
int uid = 255;
int uid2 = 254;
int dp1 = 1;
int dp2 = 2;
int dp3 = 4;
int dp4 = 8;

int zero = 0;
int high = 9;
int low = 9;  

ResponsiveAnalogRead analog(analogPin1, false);

SoftwareSerial displayLED(rxPin,txPin);
//byte mac[] = { 0x90, 0xA2, 0xDA, 0x0F, 0x7B, 0xCA };
//byte mac[] = { 0x00, 0x0c, 0xc8, 0x04, 0x28, 0x10 };


// 90" network


//IPAddress ip(10, 30, 3, 46); // telstatus
EthernetServer server(5750);  // create a server at port



//Function prototypes
int parseNG( char inRaw[] , struct ng_data *parsed );
void printNG( struct ng_data *inputd );
String handle_input( struct ng_data *ngInput );
float get_oil_pressure();
//unsigned int get_oil_pressure();
void display_oil_pressure(float);
void send_LO(int);
void send_HI(int);
void store_offset(int);
int read_offset();

void setup(){
  
// initialize the ethernet device
  IPAddress ip;
  byte gateway[4];
  //Maybe subnet should be configurable
  byte subnet[] = { 255, 255, 0, 0};
  byte mac[] = { 0x00, 0x0c, 0xc8, 0x02, 0xbe, 0x33 };
  
  
  ip[0] = EEPROM.read(EEPROM_IPADDR);
  ip[1] = EEPROM.read(EEPROM_IPADDR+1);
  ip[2] = EEPROM.read(EEPROM_IPADDR+2);
  ip[3] = EEPROM.read(EEPROM_IPADDR+3);

  gateway[0] = EEPROM.read(EEPROM_GATEWAY);
  gateway[1] = EEPROM.read(EEPROM_GATEWAY+1);
  gateway[2] = EEPROM.read(EEPROM_GATEWAY+2);
  gateway[3] = EEPROM.read(EEPROM_GATEWAY+3);
  
   Ethernet.begin(mac, ip, gateway, subnet );
//Ethernet.begin(mac ); for DHCP
   Serial.begin(9600);
   ///Serial.println("1 serial enabled");
//start listening for clients
   server.begin();
   /* Serial.print("server is at ");
   Serial.println(Ethernet.localIP()); */
   displayLED.begin(9600);
   pinMode(txPin, OUTPUT);
  ///  pinMode(analogPin, INPUT);
   byte_offset= sizeof(int);
   int offset = read_offset(); 
   /* Serial.print("byte offset= ");  Serial.println(byte_offset); */
   //ResponsiveAnalogRead analog(analogPin1, true);
 }
 
void loop(){
   unsigned short charCounter= 0;
   char rawin[RAWIN_SIZE];
   //printIPAddress();
   static unsigned long timer=-1*WAIT_FOR_READ;
   static char resp[RESP_SIZE];
// if an incoming client connects, there will be bytes available to read:
   EthernetClient client = server.available();
   while (client.connected()) {
      if (client.available())
      {
         // read bytes from the incoming client and write them back
         // to any clients connected to the server:
         char c = client.read();
         if (c == '\n' )
         {
            int syntaxError = parseNG(rawin, &currCom);
            
            if ( syntaxError == NO_ERROR )
            {
              handle_input( &currCom ).toCharArray(resp, RESP_SIZE);
              sprintf(ng_response, "%s %s %i %s", OBSID, SYSID, currCom.refNum, RESP_SIZE );
            }
            else
              sprintf(ng_response, "%s", ercode[syntaxError] );
               
            client.println(ng_response);
            ng_response[0]='\0';
            rawin[0] = '\0';
            client.stop();
         }
         else
         {
             // Dont overload the buffer
             if(charCounter > RAWIN_SIZE)
              continue;
              
             rawin[ charCounter ] = c;
             rawin[charCounter+1] = '\0';
             charCounter++;
         }
        
      }
      //Serial.println(rawin); 
   }
   
   while(Serial.available()>0)
   {
    
    char sc=Serial.read();
    if (sc == '\n')
    {
      serial_in[serial_in_counter] = '\0';
      sprintf(rawin, "%s %s 123 %s\0", OBSID, SYSID, serial_in );
      Serial.println(rawin);
      int syntaxLIMITSTATE_ERROR = parseNG(rawin, &currCom);
      
      //printNG(&currCom);
      
        
        if ( syntaxLIMITSTATE_ERROR == NO_ERROR )
        {
          handle_input(&currCom).toCharArray(resp, RESP_SIZE);
          sprintf(ng_response, "%s %s %i %s", OBSID, SYSID, currCom.refNum, resp );
        }
          
        else
          sprintf(ng_response, "%s %s %s", OBSID, SYSID, ercode[syntaxLIMITSTATE_ERROR] );
          
        Serial.println(ng_response );
          
      
      rawin[0] = '\0';
      serial_in[0] = '\0';
      serial_in_counter = 0;
    }
    else
    {
      serial_in[serial_in_counter] = sc;
      serial_in_counter++;
    }
    
   }
  
   if( (millis() - timer) > WAIT_FOR_READ )
   {
    pressure = get_oil_pressure();
    display_oil_pressure(pressure);
    timer = millis();
   }
   //delay(2500);
}

String handle_input( struct ng_data *ngInput ){
   String resp = "UNKOWN COMMAND";
   byte octets[4];
   char ipaddr[18];
   if ( strcmp( ngInput->queryType, "COMMAND" ) == 0 )
   {
      /* Serial.println("");
      Serial.println("COMMAND received");
      Serial.print("args[0]= ");
      Serial.println(ngInput->args[0]);
      Serial.print("args[1]= ");
      Serial.println(ngInput->args[1]); */
      
      if( strcmp( ngInput->args[0] , "FOO1" ) == 0 )
      {
         //digitalWrite( LED, HIGH);
         resp = "ok";
      }
     
	    else if( strcmp( ngInput->args[0] , "FOO2" ) == 0 )
      {
         //digitalWrite( LED, LOW);
         resp = "ok";
      }
	 
	    else if( strcmp( ngInput->args[0] , "SET_OFFSET" ) == 0 )
      {
         //Get the offset value and store it in the variable offset" 
         offset = atoi(ngInput->args[1]);
		     //Serial.println(ngInput->args[1]);
         store_offset(offset);
		     /* Serial.println("");
         Serial.print("new offset= "); Serial.println(offset); */
	    }

      else if( strcmp( ngInput->args[0], "IPADDR" ) == 0 )
      {     
        if(sscanf(ngInput->args[1], "%i.%i.%i.%i", &octets[0], &octets[1], &octets[2], &octets[3]) != 4)
        {
          resp ="BAD IP ADDRESS";
        }
        else
        {
          EEPROM.write(EEPROM_IPADDR, octets[0]);
          EEPROM.write(EEPROM_IPADDR+1, octets[1]);
          EEPROM.write(EEPROM_IPADDR+2, octets[2]);
          EEPROM.write(EEPROM_IPADDR+3, octets[3]);
        }
        resp="OK";
      }

      

      if( strcmp( ngInput->args[0], "GATEWAY" ) == 0 )
      {     
        if(sscanf(ngInput->args[1], "%i.%i.%i.%i", &octets[0], &octets[1], &octets[2], &octets[3]) != 4)
        {
          resp ="BAD GATWAY ADDRESS";
        }
        else
        {
          EEPROM.write(EEPROM_GATEWAY, octets[0]);
          EEPROM.write(EEPROM_GATEWAY+1, octets[1]);
          EEPROM.write(EEPROM_GATEWAY+2, octets[2]);
          EEPROM.write(EEPROM_GATEWAY+3, octets[3]);
        }
        resp="OK";
      }
      
   }
	 else if (	strcmp( ngInput->queryType, "REQUEST" ) == 0 )
   {
      /* Serial.println("");
      Serial.println("REQUEST received");
      Serial.print("args[0]= ");
      Serial.println(ngInput->args[0]);*/
      //Serial.print("args[1]= ");
      //Serial.println(ngInput->args[1]); */
       
      if( strcmp(  ngInput->args[0], "OIL_PRES" ) == 0 )
      {       
          resp = pressure;
          //resp="ok";
          //Serial.println("");
          /* Serial.print("pressure sent= "); Serial.println(pressure); */
       }
     
       else if( strcmp( ngInput->args[0], "GET_OFFSET" ) == 0)
       {
		       resp = read_offset();
	         //resp= "ok";
           //Serial.println("");
           //Serial.print("offset requested= "); Serial.println(offset);
        }

        else if( strcmp(  ngInput->args[0], "IPADDR" ) == 0 )
        {
          sprintf(ipaddr, "%i.%i.%i.%i",EEPROM.read(EEPROM_IPADDR), EEPROM.read(EEPROM_IPADDR +1),EEPROM.read(EEPROM_IPADDR +2),EEPROM.read(EEPROM_IPADDR +3) ); 
          resp=ipaddr;
        }
     
        else if( strcmp(  ngInput->args[0], "GATEWAY" ) == 0 )
        {
          sprintf(ipaddr, "%i.%i.%i.%i",EEPROM.read(EEPROM_GATEWAY), EEPROM.read(EEPROM_GATEWAY +1),EEPROM.read(EEPROM_GATEWAY +2),EEPROM.read(EEPROM_GATEWAY +3) ); 
          resp=ipaddr;
        }
    }
    else
	  {
		   resp = "BAD";
    }
	  return resp;
} // end of handle_input



int parseNG( char inRaw[] , struct ng_data *parsed ) {
   short word_count = 0;
   char *tok;
   int errorState = 0;
   int refNum;
   char queryType[10];
   char obsid[10];
   char sysid[10];
   tok = strtok(inRaw, " \t");
   
          while( tok != NULL )
          {  
                 switch(word_count)
                 {
                         case 0://observation ID
                                 strcpy( obsid, tok );
                                 if( strcmp( obsid, OBSID ) == 0)
                                         strcpy( parsed->obsid, tok );
                                 else
                                                 errorState = 1;
                                 break;
         
                         case 1://system ID
                                 strcpy( sysid, tok );
                                 
                                 if( strcmp( sysid, SYSID ) == 0 )
                                         strcpy( parsed->sysid, tok );
                                 
                                 else
                                         errorState = 2;
                                 break;
                         
                         case 2://reference number
                                 refNum = atoi( tok );
                                         if( refNum > 0 )
                                                 parsed->refNum = refNum;
                                         else 
                                         errorState = 3;
                                         break;
         
                         case 3://query type
                                 strcpy( queryType, tok );
                                 
                                 if ( ( strcmp(queryType, "COMMAND") == 0) || ( strcmp(queryType, "REQUEST") == 0) )
                                         strcpy( parsed->queryType, queryType );
                                 else
                                         errorState = 4;
                                 break;

                         default://Arguments
                                 strcpy( parsed->args[word_count - 4], tok  );
         
                 }
                  tok = strtok( NULL, " \t" );
                   word_count++;
          }
   parsed->arg_count = word_count - 1;
   return errorState;
 }
 

 



// read the RA oil pressure
// the A/D is 10 bit so 0-5 volts is read as 0-1024 binary
// 0-5V -> 0-1024
// 4-20mA thru a 215 ohm resistor -> .86 - 4.3
// 0-200 psi -> 173-875 binary 
// binary range = 702

float get_oil_pressure() {
   int cal_val= 0;
   static int last_val;
    
   val = analogRead(analogPin1); 
   analog.update(); 
   raw_val = analog.getRawValue();
   val = analog.getValue();
   
   // +/- 1 point offset adds or subtracts one point from the binary value
   // so 1 point in offset is equal to .284 lbs/sq in
   ///Serial.println("");
   /* Serial.println("***** get_oil_pressure() ***** ");
   Serial.print("binary val=  "); Serial.println(val, BIN);
   Serial.print("raw analog val=  "); Serial.println(raw_val);
   Serial.print("analog val=  "); Serial.println(val); */
   
   offset= read_offset(); //get offset from EEPROM
   cal_val= val + offset;
   /* Serial.print("offset from EEPROM=  "); Serial.println(offset);
   Serial.print("cal_val + offset=  "); Serial.println(cal_val); */
   float pressure = (200.0/702.0)*(cal_val)-49.29; //?? check this
   //float pressure = (200.0/702.0)*(cal_val)-50.0;
   /* Serial.print("pressure=  "); Serial.println(pressure);
   Serial.println(""); */
   return pressure;
} // end of get_oil_pressure()

// To display a float value:
// First multiplay times 10,then round off to the nearest whole number.
// convet to an int and take the high and low byte.
// a decimal point will be lit on the display to effectively divide by 10
void display_oil_pressure(float pressure) {
  if (val < 173) { 
     // val < 173 is a negative pressure which is not possible with the sensor we're using
     // if the display says "LO" check for an open connection to the pressure sensor
     ///Serial.println(val);
     send_LO(uid);
     dpoff(uid);
  }
  else if (val > 950) {  
    // actually val > 875 exceeds the 200 lbs max range for the pressure sensor we're using
    // but it doesn't hurt to display a higher value just for diagnostic purposes
     send_HI(uid);
     dpoff(uid);
  }
  else {
     float x10pressure= (pressure*10);
     /* Serial.print("pressure= "); Serial.println(pressure);
     Serial.print("x10pressure= ");  Serial.println(x10pressure); */
     float round_pressure= round(x10pressure);
     ///Serial.print("round_pressure= ");  Serial.println(round_pressure);
     int display_pressure= int(round_pressure);
     ///Serial.print("display_pressure= ");  Serial.println(display_pressure);
     int pressure_high = highByte(display_pressure);
     int pressure_low = lowByte(display_pressure);
     ///Serial.print("pressure_high= ") ; Serial.println(pressure_high);
     ///Serial.print("pressure_low= ") ;Serial.println(pressure_low); 
     write_to_display(uid, pressure_high, pressure_low);
     delay(20);
     // return pressure;
  }
} // end of display_oil_pressure


void send_LO(int uid){
   delay(20);
   //Serial.println(uid);
   //Serial.println("HELP");
   displayLED.write(uid);
   displayLED.write('b');
   displayLED.write('L');
   displayLED.write('O');
} // end of send_OPEN

void send_HI(int uid){
   delay(20);
   //Serial.println(uid);
   //Serial.println("HELP");
   displayLED.write(uid);
   displayLED.write('b');
   displayLED.write('H');
   displayLED.write('I');
} // end of send_HI

// Turn ON the decimal points specied by 'which_dp'.
void dpon(int uid, int which_dp) {
   delay(20);
   displayLED.write(uid);
   displayLED.write('c');
   displayLED.write(which_dp);  
} // end of dpon

// Turn OFF all decimal points
void dpoff(int uid) {
   delay(20);
   displayLED.write(uid);
   displayLED.write('c');
   displayLED.write(zero);  
} // end of dpoff

// Write the high-byte and low-byte of the unsigned integer 'pressure' to the display.
// That's how the display works, you have to split the integer into high and low bytes and 
// send them separately.
// Each +/- (1) point of offset changes the display pressure by approx. +/- .28 psi
void write_to_display(int uid, byte high, byte low){
   delay(20);
   displayLED.write(uid);
   displayLED.write('f');
   displayLED.write(high); //high byte
   displayLED.write(low);  // low byte
   dpon(uid, dp2);
} // end of write_to_display

// Stores the calibration offset value in EEPROM
// "put" will store a float value in the EEPROM as four bytes
// use address += sizeof(float) to generate the next EEPROM address
// //an offset of three to five changes the pressure by one
void store_offset(int offset){
  address = 0;
  EEPROM.put(EEPROM_OFFSET, byte_offset);  // end of store_offset
  /* Serial.print("offset stored in EEPROM= ");  
  Serial.println(offset); */
}
// Read the calibration offset value from EEPROM
int read_offset(){
	address = 0;
	EEPROM.get(EEPROM_OFFSET, byte_offset);
  /* Serial.print("offset read from EEPROM= ");  
  Serial.println(offset); */
  return offset;
} //end of read_offset


void printNG(struct ng_data *dt)
{
  Serial.print( "OBSID=> " );
  Serial.println( dt->obsid );
  Serial.print( "SYSID=> " );
  Serial.println(dt->sysid);
  Serial.print( "qt=> " );
  Serial.println( dt->queryType );
  Serial.print("arg_count=> ");
  Serial.println(dt->arg_count);
  for( int ii=0; ii<dt->arg_count-3; ii++ )
  {
    Serial.print( "arg=> " );
    Serial.println( dt->args[ii] );
  }
  
}
