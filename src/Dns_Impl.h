/****************************************************************************************************************************
  Dns_Impl.h - Driver for W5x00

  Ethernet_Generic is a library for the W5x00 Ethernet shields trying to merge the good features of
  previous Ethernet libraries
  
  Based on and modified from 
  
  1) Ethernet Library         https://github.com/arduino-libraries/Ethernet
  2) EthernetLarge Library    https://github.com/OPEnSLab-OSU/EthernetLarge
  3) Ethernet2 Library        https://github.com/adafruit/Ethernet2
  4) Ethernet3 Library        https://github.com/sstaub/Ethernet3
    
  Built by Khoi Hoang https://github.com/khoih-prog/EthernetWebServer
  
  Copyright 2018 Paul Stoffregen
  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this
  software and associated documentation files (the "Software"), to deal in the Software
  without restriction, including without limitation the rights to use, copy, modify,
  merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to the following
  conditions:
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
  PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  
  Version: 2.3.0
    
  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  2.0.0   K Hoang      31/03/2022 Initial porting and coding to support SPI2, debug, h-only library
  2.0.1   K Hoang      08/04/2022 Add support to SPI1 for RP2040 using arduino-pico core
  2.1.0   K Hoang      22/04/2022 Add support to WIZNet W5100S
  2.2.0   K Hoang      02/05/2022 Add support to custom SPI for any board, such as STM32
  2.3.0   K Hoang      03/05/2022 Add support to custom SPI for RP2040, Portenta_H7, etc. using Arduino-mbed core
 *****************************************************************************************************************************/
// Arduino DNS client for WizNet5100-based Ethernet shield
// (c) Copyright 2009-2010 MCQN Ltd.
// Released under Apache License, version 2.0

#pragma once

#ifndef ETHERNET_GENERIC_DNS_IMPL_H
#define ETHERNET_GENERIC_DNS_IMPL_H

#include <Arduino.h>
#include "Ethernet_Generic.hpp"

#include "Dns.h"

#include "utility/w5100.h"

/////////////////////////////////////////////////////////

#define SOCKET_NONE              255

/////////////////////////////////////////////////////////

// Various flags and header field values for a DNS message
#define UDP_HEADER_SIZE          8
#define DNS_HEADER_SIZE          12
#define TTL_SIZE                 4
#define QUERY_FLAG               (0)
#define RESPONSE_FLAG            (1<<15)
#define QUERY_RESPONSE_MASK      (1<<15)
#define OPCODE_STANDARD_QUERY    (0)
#define OPCODE_INVERSE_QUERY     (1<<11)
#define OPCODE_STATUS_REQUEST    (2<<11)
#define OPCODE_MASK              (15<<11)
#define AUTHORITATIVE_FLAG       (1<<10)
#define TRUNCATION_FLAG          (1<<9)
#define RECURSION_DESIRED_FLAG   (1<<8)
#define RECURSION_AVAILABLE_FLAG (1<<7)
#define RESP_NO_ERROR            (0)
#define RESP_FORMAT_ERROR        (1)
#define RESP_SERVER_FAILURE      (2)
#define RESP_NAME_ERROR          (3)
#define RESP_NOT_IMPLEMENTED     (4)
#define RESP_REFUSED             (5)
#define RESP_MASK                (15)
#define TYPE_A                   (0x0001)
#define CLASS_IN                 (0x0001)
#define LABEL_COMPRESSION_MASK   (0xC0)

/////////////////////////////////////////////////////////

// Port number that DNS servers listen on
#define DNS_PORT                  53

/////////////////////////////////////////////////////////

// Possible return codes from ProcessResponse
#define SUCCESS                   1
#define TIMED_OUT                 -1
#define INVALID_SERVER            -2
#define TRUNCATED                 -3
#define INVALID_RESPONSE          -4

/////////////////////////////////////////////////////////

void DNSClient::begin(const IPAddress& aDNSServer)
{
  iDNSServer = aDNSServer;
  iRequestId = 0;
}

/////////////////////////////////////////////////////////

int DNSClient::_inet_aton(const char* address, IPAddress& result)
{
  uint16_t acc = 0; // Accumulator
  uint8_t dots = 0;

  while (*address) 
  {
    char c = *address++;
    
    if (c >= '0' && c <= '9') 
    {
      acc = acc * 10 + (c - '0');
      
      if (acc > 255) 
      {
        // Value out of [0..255] range
        return 0;
      }
    } 
    else if (c == '.') 
    {
      if (dots == 3) 
      {
        // Too much dots (there must be 3 dots)
        return 0;
      }
      
      result[dots++] = acc;
      acc = 0;
    } 
    else 
    {
      // Invalid char
      return 0;
    }
  }

  if (dots != 3) 
  {
    // Too few dots (there must be 3 dots)
    return 0;
  }
  
  result[3] = acc;
  
  return 1;
}

/////////////////////////////////////////////////////////

int DNSClient::getHostByName(const char* aHostname, IPAddress& aResult, uint16_t timeout)
{
  int ret = 0;

  // See if it's a numeric IP address
  if (_inet_aton(aHostname, aResult)) 
  {
    // It is, our work here is done
    return 1;
  }

  // Check we've got a valid DNS server to use
  if (iDNSServer == (IPAddress) INADDR_NONE) 
  {
    return INVALID_SERVER;
  }

  // Find a socket to use
  if (iUdp.begin(1024 + (millis() & 0xF)) == 1) 
  {
    // Try up to three times
    int retries = 0;
    
    // while ((retries < 3) && (ret <= 0)) {
    // Send DNS request
    ret = iUdp.beginPacket(iDNSServer, DNS_PORT);
    
    if (ret != 0) 
    {
      // Now output the request data
      ret = BuildRequest(aHostname);
      
      if (ret != 0) 
      {
        // And finally send the request
        ret = iUdp.endPacket();
        
        if (ret != 0) 
        {
          // Now wait for a response
          int wait_retries = 0;
          ret = TIMED_OUT;
          
          while ((wait_retries < 3) && (ret == TIMED_OUT)) 
          {
            ret = ProcessResponse(timeout, aResult);
            wait_retries++;
          }
        }
      }
    }
    retries++;
    //}

    // We're done with the socket now
    iUdp.stop();
  }

  return ret;
}

/////////////////////////////////////////////////////////

uint16_t DNSClient::BuildRequest(const char* aName)
{
  // Build header
  //                                    1  1  1  1  1  1
  //      0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
  //    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //    |                      ID                       |
  //    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //    |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |
  //    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //    |                    QDCOUNT                    |
  //    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //    |                    ANCOUNT                    |
  //    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //    |                    NSCOUNT                    |
  //    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  //    |                    ARCOUNT                    |
  //    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
  // As we only support one request at a time at present, we can simplify
  // some of this header
  iRequestId = millis(); // generate a random ID
  uint16_t twoByteBuffer;

  // FIXME We should also check that there's enough space available to write to, rather
  // FIXME than assume there's enough space (as the code does at present)
  iUdp.write((uint8_t*)&iRequestId, sizeof(iRequestId));

  twoByteBuffer = htons(QUERY_FLAG | OPCODE_STANDARD_QUERY | RECURSION_DESIRED_FLAG);
  iUdp.write((uint8_t*)&twoByteBuffer, sizeof(twoByteBuffer));

  twoByteBuffer = htons(1);  // One question record
  iUdp.write((uint8_t*)&twoByteBuffer, sizeof(twoByteBuffer));

  twoByteBuffer = 0;  // Zero answer records
  iUdp.write((uint8_t*)&twoByteBuffer, sizeof(twoByteBuffer));

  iUdp.write((uint8_t*)&twoByteBuffer, sizeof(twoByteBuffer));
  // and zero additional records
  iUdp.write((uint8_t*)&twoByteBuffer, sizeof(twoByteBuffer));

  // Build question
  const char* start = aName;
  const char* end = start;
  uint8_t len;
  
  // Run through the name being requested
  while (*end) 
  {
    // Find out how long this section of the name is
    end = start;
    
    while (*end && (*end != '.') ) 
    {
      end++;
    }

    if (end - start > 0) 
    {
      // Write out the size of this section
      len = end - start;
      iUdp.write(&len, sizeof(len));
      // And then write out the section
      iUdp.write((uint8_t*)start, end - start);
    }
    
    start = end + 1;
  }

  // We've got to the end of the question name, so
  // terminate it with a zero-length section
  len = 0;
  iUdp.write(&len, sizeof(len));
  // Finally the type and class of question
  twoByteBuffer = htons(TYPE_A);
  iUdp.write((uint8_t*)&twoByteBuffer, sizeof(twoByteBuffer));

  twoByteBuffer = htons(CLASS_IN);  // Internet class of question
  iUdp.write((uint8_t*)&twoByteBuffer, sizeof(twoByteBuffer));
  
  // Success!  Everything buffered okay
  return 1;
}

/////////////////////////////////////////////////////////

uint16_t DNSClient::ProcessResponse(uint16_t aTimeout, IPAddress& aAddress)
{
  uint32_t startTime = millis();

  // Wait for a response packet
  while (iUdp.parsePacket() <= 0) 
  {
    if ((millis() - startTime) > aTimeout) 
    {
      return TIMED_OUT;
    }
    
    delay(50);
  }

  // We've had a reply!
  // Read the UDP header
  //uint8_t header[DNS_HEADER_SIZE]; // Enough space to reuse for the DNS header
  
  union 
  {
    uint8_t  byte[DNS_HEADER_SIZE]; // Enough space to reuse for the DNS header
    uint16_t word[DNS_HEADER_SIZE / 2];
  } header;

  // Check that it's a response from the right server and the right port
  if ( (iDNSServer != iUdp.remoteIP()) || (iUdp.remotePort() != DNS_PORT) ) 
  {
    // It's not from who we expected
    return INVALID_SERVER;
  }

  // Read through the rest of the response
  if (iUdp.available() < DNS_HEADER_SIZE) 
  {
    return TRUNCATED;
  }
  
  iUdp.read(header.byte, DNS_HEADER_SIZE);

  uint16_t header_flags = htons(header.word[1]);
  
  // Check that it's a response to this request
  if ((iRequestId != (header.word[0])) ||
      ((header_flags & QUERY_RESPONSE_MASK) != (uint16_t)RESPONSE_FLAG) ) 
      {
    // Mark the entire packet as read
    iUdp.flush(); // FIXME
    
    return INVALID_RESPONSE;
  }
  
  // Check for any errors in the response (or in our request)
  // although we don't do anything to get round these
  if ( (header_flags & TRUNCATION_FLAG) || (header_flags & RESP_MASK) ) 
  {
    // Mark the entire packet as read
    iUdp.flush(); // FIXME
    
    return -5; //INVALID_RESPONSE;
  }

  // And make sure we've got (at least) one answer
  uint16_t answerCount = htons(header.word[3]);
  
  if (answerCount == 0) 
  {
    // Mark the entire packet as read
    iUdp.flush(); // FIXME
    
    return -6; //INVALID_RESPONSE;
  }

  // Skip over any questions
  for (uint16_t i = 0; i < htons(header.word[2]); i++) 
  {
    // Skip over the name
    uint8_t len;
    
    do 
    {
      iUdp.read(&len, sizeof(len));
      
      if (len > 0) 
      {
        // Don't need to actually read the data out for the string, just
        // advance ptr to beyond it
        iUdp.read((uint8_t *)NULL, (size_t)len);
      }
    } while (len != 0);

    // Now jump over the type and class
    iUdp.read((uint8_t *)NULL, 4);
  }

  // Now we're up to the bit we're interested in, the answer
  // There might be more than one answer (although we'll just use the first
  // type A answer) and some authority and additional resource records but
  // we're going to ignore all of them.
  for (uint16_t i = 0; i < answerCount; i++) 
  {
    // Skip the name
    uint8_t len;
    
    do 
    {
      iUdp.read(&len, sizeof(len));
      
      if ((len & LABEL_COMPRESSION_MASK) == 0) 
      {
        // It's just a normal label
        if (len > 0) 
        {
          // And it's got a length
          // Don't need to actually read the data out for the string,
          // just advance ptr to beyond it
          iUdp.read((uint8_t *)NULL, len);
        }
      } 
      else 
      {
        // This is a pointer to a somewhere else in the message for the
        // rest of the name.  We don't care about the name, and RFC1035
        // says that a name is either a sequence of labels ended with a
        // 0 length octet or a pointer or a sequence of labels ending in
        // a pointer.  Either way, when we get here we're at the end of
        // the name
        // Skip over the pointer
        iUdp.read((uint8_t *)NULL, 1); // we don't care about the byte
        // And set len so that we drop out of the name loop
        len = 0;
      }
    } while (len != 0);

    // Check the type and class
    uint16_t answerType;
    uint16_t answerClass;
    
    iUdp.read((uint8_t*)&answerType, sizeof(answerType));
    iUdp.read((uint8_t*)&answerClass, sizeof(answerClass));

    // Ignore the Time-To-Live as we don't do any caching
    //iUdp.read((uint8_t *)NULL, TTL_SIZE); // don't care about the returned bytes
    for (int i =0; i < TTL_SIZE; i++)
    {
      iUdp.read(); // we don't care about the returned byte
    }

    // And read out the length of this answer
    // Don't need header_flags anymore, so we can reuse it here
    iUdp.read((uint8_t*)&header_flags, sizeof(header_flags));

    if ( (htons(answerType) == TYPE_A) && (htons(answerClass) == CLASS_IN) ) 
    {
      if (htons(header_flags) != 4) 
      {
        // It's a weird size
        // Mark the entire packet as read
        iUdp.flush(); // FIXME
        
        return -9;//INVALID_RESPONSE;
      }
      
      // FIXME: seeems to lock up here on ESP8266, but why??
      iUdp.read(aAddress.raw_address(), 4);
      
      return SUCCESS;
    } 
    else 
    {
      // This isn't an answer type we're after, move onto the next one
      iUdp.read((uint8_t *)NULL, htons(header_flags));
    }
  }

  // Mark the entire packet as read
  iUdp.flush(); // FIXME

  // If we get here then we haven't found an answer
  return -10; //INVALID_RESPONSE;
}

/////////////////////////////////////////////////////////

#endif    // ETHERNET_GENERIC_DNS_IMPL_H
