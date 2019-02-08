/*
 ESP8266 Hello World urlencode by Steve Nelson
 URLEncoding is used all the time with internet urls. This is how urls handle funny characters
 in a URL. For example a space is: %20
 These functions simplify the process of encoding and decoding the urlencoded format.
  
 It has been tested on an esp12e (NodeMCU development board)
 This example code is in the public domain, use it however you want. 
  Prerequisite Examples:
  https://github.com/zenmanenergy/ESP8266-Arduino-Examples/tree/master/helloworld_serial
*/
#include <String.h>
#include <Arduino.h>
unsigned char h2int(char c);

String urldecode(String str);


String urlencode(String str);
