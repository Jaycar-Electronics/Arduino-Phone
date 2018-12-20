// Using a GSM shield to emulate the functions of a corded phone
// Implemented as a state machine
//  +->IDLE <---HS UP----> HSUP
//  |   ^  ^                 |
//  | Ring   \Local H/up-    Dial
//  |   v                \   v
//  |  RING ----HS UP----> CALLIP
//  |                       |
// HS DN                    |
//  |                       |
//  +- CALLEND <-Rem H/up---+


//needs to use hardware serial as LCD uses the hardwired software serial pins
//D9 is used to boot module, then released to LCD, may not work if D9 use is mixed up
//LCD is just for status info/CND, will work fine without it

#include <LiquidCrystal.h>
LiquidCrystal lcd(8, 9, 4, 5, 6, 7); //(RS,E,D4,D5,D6,D7)

#define gsm Serial
//Phone states
#define IDLE 1
#define HSUP 2
#define CALLIP 3
#define RING 4
#define CALLEND 5
int phonestate=IDLE;
#define PNOLEN 17
char dialno[PNOLEN]="";   //outgoing calls
int dialnoptr=0;
char CNDno[PNOLEN]="";    //incoming calls
#define RXBUFFER 80
char rxdata[RXBUFFER]="";    //incoming data from GSM
int rxptr=0;
long ringtmout;
//dialtmout and DIALTMOUT delay decide how long to wait after handset is lifted before call is commenced (dialling can still happen before handset is lifted
long dialtmout;
#define DIALTMOUTDELAY 2000
//SIM900 needs international format, use the following to convert
//if there is a leading zero, drop zero and use CCODE, if no zero, use ACODE
#define CCODE "+61"
#define ACODE "+612"

//external hardware- hook switch and ringer
#define HSWITCH 12
#define RINGER 11
//if 0, high=offhook, if 1, high=onhook
#define HSWITCHINVERT 0
//keypads in use- USELCDKEYPAD should be 0 if not connected as A0 may float
#define USELCDKEYPAD 0
#define USEMATRIX 1
//pins to connections on SP0770- (L-R) columns are 3,1,5 (top-bottom) rows are 2,7,6,4
#define MATRIX1 A1
#define MATRIX2 A2
#define MATRIX3 A3
#define MATRIX4 A4
#define MATRIX5 A5
#define MATRIX6 2
#define MATRIX7 3

//pin for buttons
#define KEYPIN A0
//button constants
#define btnRIGHT  6
#define btnUP     5
#define btnDOWN   4
#define btnLEFT   3
#define btnSELECT 2
#define btnNONE   (-1)

void setup() {
  long btime;
  pinMode(HSWITCH,INPUT_PULLUP);
  pinMode(RINGER,OUTPUT);
  digitalWrite(RINGER,LOW);
  gsm.begin(19200);
  btime=gsmstart(11000);    //start GSM shield
  lcd.begin(16, 2);      
  lcd.setCursor(0, 0);
  lcd.print("DuinoPhone      ");
  lcd.setCursor(0, 1);
  if(btime){
    lcd.print("GSM Module start");
  }else{
    lcd.print("GSM Module fail ");
    while(1){};             //sit here, there's not much to do if GSM module isn't working    
  }
  gsm.println("AT+CMGF=1");     //set to text mode
  delay(100);
  gsm.println("ATE0");     //echo off
  delay(100);
  gsm.println("AT+CLIP=1");     //calling number info on
  delay(100);
  gsmpurge(200);
}

void loop() {
  int hswitch;
  hswitch=digitalRead(HSWITCH);
  if(HSWITCHINVERT){hswitch=!hswitch;}
  for(int i=0;i<30;i++){checkgsm();}     //see if any data is coming from GSM module
  if((RING==phonestate)&&(millis()-ringtmout>5000)){phonestate=IDLE;dialno[0]=0;dialnoptr=0;CNDno[0]=0;}     //if no ring received for a while, it's stopped ringing #3
  if(IDLE==phonestate){CNDno[0]=0;dialtmout=millis();}     //clear incoming number in idle, clear dial timeout
  if((IDLE==phonestate)&&(hswitch)){phonestate=HSUP;}   //handset has been picked up #9
  if((HSUP==phonestate)&&(!hswitch)){phonestate=IDLE;dialno[0]=0;dialnoptr=0;CNDno[0]=0;}    //handset put down without call occurring, number to be dialled is cleared #1
  if((RING==phonestate)&&(hswitch)){gsm.println("ATA");phonestate=CALLIP;}    //handset picked up while ringing, call in progress #7
  if((CALLIP==phonestate)&&(!hswitch)){gsm.println("ATH");phonestate=IDLE;dialno[0]=0;dialnoptr=0;CNDno[0]=0;}    //handset put down while call in progress, hang up and return to idle #5
  if((IDLE==phonestate)||(HSUP==phonestate)){checkkeys();}      //user can dial here  > could add DTMF tones during call (does GSM module support this?)
  if((HSUP==phonestate)&&(millis()-dialtmout>DIALTMOUTDELAY)&&(dialno[0]!=0)){dial();phonestate=CALLIP;}    //dial number and assume call in progress #6
  if((CALLEND==phonestate)&&(!hswitch)){phonestate=IDLE;dialno[0]=0;dialnoptr=0;CNDno[0]=0;}     //handset has been replaced after call terminated at remote end #2, clear dialled number
  lcdphonestate();  //show current state
}

void dial(){    //dial the typed number
  gsm.print("ATD ");
  if(dialno[0]=='0'){     //if there's a leading zero, drop the zero and dial as international number (should work for Aus, NZ, maybe not USA), may not work for 000 etc
    gsm.print(CCODE);
    gsm.print(&dialno[1]);
  }else{
    gsm.print(ACODE);     //other wise, add country and area code and dial as international number
    gsm.print(dialno);
    }
  gsm.println(";");
}

void checkkeys(){
  if(USELCDKEYPAD){checklcdkeypad();} //comment this out if not using LCD, otherwise floating input may give stray characters
  if(USEMATRIX){checkmatrix();}
  if(dialnoptr>PNOLEN-2){dialnoptr=PNOLEN-2;dialno[dialnoptr]=0;}   //truncate to 16 chars
}

void checklcdkeypad(){
  static int lastkey=btnNONE;
  int key;
  key = read_LCD_buttons();
  if(lastkey==btnNONE){   //only if key just pressed
    if(key==btnSELECT){}
    if(key==btnUP){dialno[dialnoptr]++;if(dialno[dialnoptr]>'9'){dialno[dialnoptr]='9';}if(dialno[dialnoptr]<'0'){dialno[dialnoptr]='0';}}
    if(key==btnDOWN){dialno[dialnoptr]--;if(dialno[dialnoptr]>'9'){dialno[dialnoptr]='9';}if(dialno[dialnoptr]<'0'){dialno[dialnoptr]='0';}}
    if(key==btnLEFT){}
    if(key==btnRIGHT){dialnoptr++;}
    lastkey=key;
  }
}

void checkmatrix(){
  static int lastkey=0;   //null aka no key- use ASCII encoding here
  int key=0;        //assume nothing pressed
  pinMode(MATRIX1,INPUT_PULLUP);
  pinMode(MATRIX2,INPUT_PULLUP);
  pinMode(MATRIX3,INPUT_PULLUP);
  pinMode(MATRIX4,INPUT_PULLUP);
  pinMode(MATRIX5,INPUT_PULLUP);
  pinMode(MATRIX6,INPUT_PULLUP);
  pinMode(MATRIX7,INPUT_PULLUP);

  pinMode(MATRIX1,OUTPUT);
  digitalWrite(MATRIX1,LOW);
  delay(1);
  if(!digitalRead(MATRIX2)){key='2';}
  if(!digitalRead(MATRIX7)){key='5';}
  if(!digitalRead(MATRIX6)){key='8';}
  if(!digitalRead(MATRIX4)){key='0';}
  pinMode(MATRIX1,INPUT_PULLUP);

  pinMode(MATRIX3,OUTPUT);
  digitalWrite(MATRIX3,LOW);
  delay(1);
  if(!digitalRead(MATRIX2)){key='1';}
  if(!digitalRead(MATRIX7)){key='4';}
  if(!digitalRead(MATRIX6)){key='7';}
  if(!digitalRead(MATRIX4)){key='*';}
  pinMode(MATRIX3,INPUT_PULLUP);

  pinMode(MATRIX5,OUTPUT);
  digitalWrite(MATRIX5,LOW);
  delay(1);
  if(!digitalRead(MATRIX2)){key='3';}
  if(!digitalRead(MATRIX7)){key='6';}
  if(!digitalRead(MATRIX6)){key='9';}
  if(!digitalRead(MATRIX4)){key='#';}
  pinMode(MATRIX5,INPUT_PULLUP);

  if((!lastkey)&&key){    
    dialno[dialnoptr]=key;    //add key pressed
    dialnoptr++;
    dialno[dialnoptr]=0;      //null
    dialtmout=millis();         //reset time before dialling if key pressed
  }
  lastkey=key;
}

void checkgsm(){
  int d; 
  if(gsm.available()){
    d=gsm.read();            //check data
    if(d>31){                 //if ascii character
      rxdata[rxptr]=d;        //add to buffer
      rxptr++;  
      rxdata[rxptr]=0;
      if(rxptr>RXBUFFER-2){
        rxptr=RXBUFFER-2;   ///keep within array bounds
      }
    }
    if(d==13){          //if end of line
      if(strmatch(rxdata,"RING")){handlering();}
      if(strmatch(rxdata,"NO CARRIER")){handlehangup();}
      if(strmatch(rxdata,"+CLIP: ",7)){getphoneno();}
      rxptr=0;
      rxdata[0]=0;
    }
  }
}

void getphoneno(){    //takes number from +CLIP response and puts it in CNDno
  int t,k,p;
  t=0;
  p=0;
  CNDno[0]=0; //empty string
  k=strlen(rxdata);
  for(int i=0;i<k;i++){
  if(rxdata[i]>31){      //ignore control characters (cr/lf etc)
    if(rxdata[i]=='"'){
      t=t+1;p=0;                                     //increase token (incoming number is between first and second quote marks)
    }else{
      if(t==1){CNDno[p]=rxdata[i];p=p+1;CNDno[p]=0;}        //add to output
      if(p>PNOLEN-2){p=PNOLEN-2;}     // truncate if too long
      }
    }
  }
}

void handlering(){
  if(IDLE==phonestate){phonestate=RING;}      //ringing has started #4
  ringtmout=millis();     //start counter in case ringing stops
  tone(RINGER,500,800);     //sound ringer
}

void handlehangup(){
  if(CALLIP==phonestate){phonestate=CALLEND;}  //call terminated from remote end  #8
  if(RING==phonestate){phonestate=IDLE;}    //call terminated during ringing #3
}

void lcdphonestate(){
  static long t=0;
  if(millis()-t<300){return;}   //do nothing if it's less than 300ms since last update
  lcd.setCursor(0,0);
  lcd.print("                ");
  lcd.setCursor(0,0);
  if(CNDno[0]==0){lcd.print(dialno);}   //display either incoming or outgoing number
    else{lcd.print(CNDno);}

  lcd.setCursor(0, 1);
  switch(phonestate){
    case IDLE:   lcd.print("Ready...........");break;     //display current state
    case HSUP:   lcd.print("Please dial.....");break;
    case CALLIP: lcd.print("Call in progress");break;
    case RING:   lcd.print("Incoming call...");break;
    case CALLEND:lcd.print("Replace handset.");break;
    default:lcd.print("????????????????");break;       //undefined state
  }
  t=millis();
}

long gsmstart(long tmout){      //returns time to init if successful, 0 if not
  long t;
  t=millis();   //to track if timeout has occurred
  while(millis()-t<tmout){
    gsm.println("AT");     //standard test for comms
    delay(100);
    while(gsm.available()){
      int d;
      d=gsm.read();
      if(d=='O'){
        d=gsm.read();
        if(d=='K'){   ///response received
          return millis()-t;
        }
      }
    }
    pinMode(9,OUTPUT);    //for powering of gsm module
    digitalWrite(9,HIGH);     //cycle power key
    delay(1000);
    digitalWrite(9,LOW);
    delay(2200);          //wait for module to stabilise
    pinMode(9,INPUT);    //release pin 9 for LCD module
  }  
  return 0;   //has timed out
}

void gsmpurge(long n){      //clear receive buffer, and wait a bit to see if more characters come
  long t;
  int d;
  t=millis();
  while(millis()-t<n){    //clear buffer
    if(gsm.available()){
      d=gsm.read();
      t=millis();
    }
  }
}

int strmatch(char a[],char b[],int n){      //true if strings match for first n characters
  int x,y;
  x=strlen(a);
  y=strlen(b);
  if(x>n){x=n;}
  if(y>n){y=n;}
  if(x!=y){return 0;}
  for(int i=0;i<x;i++){
    if(a[i]!=b[i]){return 0;}
  }
  return 1;
}

int strmatch(char a[],char b[]){      //true if strings match
  int x,y;
  x=strlen(a);
  y=strlen(b);
  if(x!=y){return 0;}
  for(int i=0;i<x;i++){
    if(a[i]!=b[i]){return 0;}
  }
  return 1;
}

int read_LCD_buttons(){
  int adc_key_in = 0;
  adc_key_in = analogRead(KEYPIN);      // read the value from the sensor 
  delay(5); //switch debounce delay. Increase this delay if incorrect switch selections are returned.
  int k = (analogRead(KEYPIN) - adc_key_in); //gives the button a slight range to allow for a little contact resistance noise
  if (5 < abs(k)) return btnNONE;  // double checks the keypress. If the two readings are not equal +/-k value after debounce delay, it tries again.
  // my buttons when read are centered at these valies: 0, 144, 329, 504, 741
  // we add approx 50 to those values and check to see if we are close
  if (adc_key_in > 1000) return btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
  if (adc_key_in < 50)   return btnRIGHT;  
  if (adc_key_in < 195)  return btnUP; 
  if (adc_key_in < 380)  return btnDOWN; 
  if (adc_key_in < 555)  return btnLEFT; 
  if (adc_key_in < 790)  return btnSELECT;   
  return btnNONE;  // when all others fail, return this...
}

