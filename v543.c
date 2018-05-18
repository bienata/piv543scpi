/*

kompilacja:
  g++ -o v543 -lwiringPi v543.c
  
uruchomienie:
  ./v543
  
*/

#include <stdio.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <wiringPi.h>

#define SCPI_PORT	5555

#define LINE_READY  0
#define LINE_CLK    1
#define LINE_LOAD   2
#define LINE_DATA   3

#define LED_READY   4
#define LED_SCPI    5

char* trim(char*);  // proto, kod niżej
void handlerIdn(char*);
void handlerRst(char*); 
void handlerMode(char*);
void handlerVRange(char*);
void handlerRRange(char*);
void handlerRawData(char*);
void handlerDisplay(char*); 
void handlerExit(char*); 


// prototyp handlerka komendy scpi, po prostu wypełnia wynik w out i tyle
typedef void (*TScpiCommandHandler)(char*);

// parka komenda-handler
typedef struct {
    const char *cmd;
    TScpiCommandHandler handler;
} TCommand;

// na informacje o zakresie
typedef struct {
    const char  *label;
    int scale;
} TRangeInfo;


unsigned char   uchLedReady = 0;
unsigned char   uchLedScpi = 0;
unsigned long   ulRawMeterData = 0L;
unsigned char   uchRangeId = 0;
unsigned char   uchModeId = 0;
unsigned char   uchPolarity = 0;


// to póki co obsługujemy
TCommand scpiCommands[] = {
    {   "*idn?",              &handlerIdn },    
    {   ":meter:mode?",       &handlerMode },
    {   ":meter:v:range?",    &handlerVRange },
    {   ":meter:r:range?",    &handlerRRange },
    {   ":meter:raw?",        &handlerRawData },
    {   ":meter:display?",    &handlerDisplay }, 
    {   ":debug:exit",        &handlerExit }, 
    {   NULL ,                NULL }
};

TRangeInfo volRanges[] = {
    {   "100V",       100   },  //0
    {   "1V",         10000 },  //1
    {   "1000V",      10    },  //2
    {   "10V",        1000  },  //3
    {   "100mV",      100   },  //4
    {   "error",      1     },  //5
    {   "error",      1     },  //6
    {   "error",      1     }   //7
};

TRangeInfo resRanges[] = {
    {   "100kΩ",      100   },  //0
    {   "1kΩ",        10000 },  //1
    {   "error",      1     },  //2
    {   "10kΩ",       1000  },  //3
    {   "error",      1     },  //4
    {   "1MΩ",        10000 },  //5
    {   "error",      1     },  //6
    {   "10MΩ",       1000  }   //7
};


const char *pszModeDesc[] = {
      "error",    // 0
      "R",        // 1
      "AC",       // 2
      "error",    // 3
      "DC"        // 4        
};

    
void makeLower ( char *s ) {
    for(int i = 0; s[ i ]; i++ ) {
      s[ i ] = tolower( s[ i ] );
    }   
}

// identyfikacja urządzenia 
void handlerIdn(char *out) {    
    strcpy ( out, "Meratronik V543 No.01473, SCPI connector, tasza (c) 2018\n");
}

// R,AC,DC
void handlerMode(char *out) {    
    sprintf( out, "%d|%s\n", uchModeId, pszModeDesc[ uchModeId ] );
}

// V,mV
void handlerVRange(char *out) {    
    sprintf( out, "%d|%s|%d\n", uchRangeId, volRanges[ uchRangeId ].label, volRanges[ uchRangeId ].scale );        
}

// kohm, Mohm
void handlerRRange(char *out) {    
    sprintf( out, "%d|%s|%d\n", uchRangeId, resRanges[ uchRangeId ].label, resRanges[ uchRangeId ].scale );            
}

// odsyła znak i pięć cyferek wyświetlacza
void handlerDisplay(char *out) {
    char sign = ' ';
    if ( uchModeId == 4 /*DC*/){
        sign = uchPolarity == 1 ? '+' : '-';
    }
    sprintf( out, "%c%05X\n", sign, ulRawMeterData &0x1FFFF );    
} 

// 32 bity hex
void handlerRawData(char *out) {
    sprintf( out, "%08X\n", ulRawMeterData );    
}

// debugowe wyjście
void handlerExit(char *out) {
    strcpy( out, "exit here\n" );    
}


// rozpoznanie i wykoannie polecenia SCPI
int processScpiCommand ( char *cmd, char *out ) {
    int o = 0;
    // domyslnie blad
    strcpy ( out, "error\n" );    
    for( int i = 0; scpiCommands[i].cmd != NULL; ++i ) {
        if ( strcmp ( cmd, scpiCommands[i].cmd ) == 0 ) {
            // trafiony!
            (scpiCommands[i].handler)(out);
            o = (scpiCommands[i].handler == handlerExit);
            break;
        }
    }
    digitalWrite ( LED_READY, uchLedScpi ) ;        
    uchLedScpi ^= 1;    
    return o;
}


// :) żywcem zerżnięte z dawnego Arduino
unsigned long readV543rawData( void ) {
  unsigned long rawFrame = 0L;
  int n;  
  digitalWrite( LINE_LOAD, LOW ); // do -\/- pulse
  digitalWrite( LINE_LOAD, HIGH ); 
  digitalWrite( LINE_CLK, LOW );
  for ( n = 31; n >= 0; n-- )  {
    if ( digitalRead( LINE_DATA ) ) {
          rawFrame |= ( (unsigned long)1 << n );
    }     
    digitalWrite( LINE_CLK, HIGH);        
    digitalWrite( LINE_CLK, LOW);
  } // for       
  return rawFrame & 0x03FFFFFFL;
}


// obsługa przerwania od GPIO z pinu LINE_READY Meratronika
void onMeterReadyInterrupt( void ) {        
    // fizyczny odczyt
    ulRawMeterData = readV543rawData();    
    uchRangeId = (ulRawMeterData >> 17) & 0x07;
    uchModeId = (ulRawMeterData >> 22) & 0x07;
    uchPolarity = (ulRawMeterData >> 20) & 0x03;
    // mignięcie ledem
    digitalWrite ( LED_READY, uchLedReady ) ;        
    uchLedReady ^= 1;
}

// main foo.
int main( int argc, char *argv[] ) {

     struct sockaddr_in serv_addr, cli_addr;  
     int serverSocket, clientSocket;
     int clilen;
     int n;
     int data;
     char commandBuffer[ 64 ];
     char responseBuffer[ 64 ];     
     
    wiringPiSetup () ;     
    pinMode ( LINE_READY, INPUT );
    pinMode ( LINE_CLK,   OUTPUT );
    pinMode ( LINE_LOAD,  OUTPUT );     digitalWrite( LINE_LOAD, HIGH );   
    pinMode ( LINE_DATA,  INPUT );      digitalWrite( LINE_CLK, HIGH );   
    
    pinMode ( LED_READY, OUTPUT );  
    pinMode ( LED_SCPI, OUTPUT );

    
    if ( wiringPiISR( LINE_READY, INT_EDGE_RISING, &onMeterReadyInterrupt ) < 0 ) { 
        printf ( "Unable to setup ISR: %s\n", strerror (errno) ) ;
        return 1 ;
    }    
         
     serverSocket = socket( AF_INET, SOCK_STREAM, 0 );
     if ( serverSocket < 0 ) {
         perror ( "00 error when opening server socket" );
	     exit (1);
     }
     
     bzero( (char *)&serv_addr, sizeof(serv_addr) );
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons( SCPI_PORT );
     
     if ( bind( serverSocket, (struct sockaddr *) &serv_addr, sizeof(serv_addr) ) < 0 ) {
         perror ( "01 error when binding server socket" );
	     exit (1);
     }
     
     listen( serverSocket, 5);     
     clilen = sizeof(cli_addr);
  
     // czekaj na polecenia     
     while ( 1 ) {
       
        printf( "10 waiting for connection\n" );
	
        if ( ( clientSocket = accept( serverSocket, (struct sockaddr *) &cli_addr, (socklen_t*) &clilen) ) < 0 ) {
	        perror ( "02 error on accept incoming connection" );
	        exit (1);
	    }
	
        printf( "11 waiting for input data\n" );
	
	    bzero( commandBuffer, sizeof( commandBuffer ) );
	    bzero( responseBuffer, sizeof( responseBuffer ) );	  	
	
    	if ( ( n = read( clientSocket, commandBuffer, sizeof( commandBuffer ) - 1 ) ) < 0 ){
	        perror ( "03 error when receiving request" );
	        exit (1);	      
	    }

        makeLower( commandBuffer );
        
	    int exitRequest = processScpiCommand ( trim( commandBuffer ), responseBuffer );
    
	    if ( (n = write( clientSocket, responseBuffer, strlen( responseBuffer ) ) ) < 0 ) {
	        perror ( "04 error when sending response" );
	        exit (1);	      	      	   
	    } 
	    else {
            printf( "12 SCPI [%s]->[%s]\n", commandBuffer, trim( responseBuffer)  );    
        }

	    // release conn to client
        close( clientSocket );
        
	    if ( exitRequest ) {        
            printf( "19 done\n" );                
            break;
        }
        
     } // of while     
     
     return 0; 
}

// ----------- obce z sieci ----------------

// zapożyczone:
// https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
char *trim(char *str)
{
    size_t len = 0;
    char *frontp = str;
    char *endp = NULL;

    if( str == NULL ) { return NULL; }
    if( str[0] == '\0' ) { return str; }

    len = strlen(str);
    endp = str + len;

    /* Move the front and back pointers to address the first non-whitespace
     * characters from each end.
     */
    while( isspace((unsigned char) *frontp) ) { ++frontp; }
    if( endp != frontp )
    {
        while( isspace((unsigned char) *(--endp)) && endp != frontp ) {}
    }

    if( str + len - 1 != endp )
            *(endp + 1) = '\0';
    else if( frontp != str &&  endp == frontp )
            *str = '\0';

    /* Shift the string so that it starts at str so that if it's dynamically
     * allocated, we can still free it on the returned pointer.  Note the reuse
     * of endp to mean the front of the string buffer now.
     */
    endp = str;
    if( frontp != str )
    {
            while( *frontp ) { *endp++ = *frontp++; }
            *endp = '\0';
    }
    return str;
}
// koniec zapożyczen
