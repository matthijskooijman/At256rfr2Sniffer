/**************************************************************************\
* Copyright (c) 2012-2014, Pinoccio Inc. All rights reserved.              *
* Copyright (c) 2025, Matthijs Kooijman <matthijs@stdin.nl>
* ------------------------------------------------------------------------ *
*  This program is free software; you can redistribute it and/or modify it *
*  under the terms of the BSD License as described in license.txt.         *
\**************************************************************************/

#include <Arduino.h>
#include "lwm/atmegarfr2.h"
#include "lwm/helpers.h"

static constexpr size_t MAX_FRAME_LENGTH = 128;


/* Page 123 of the 256RFR2 datasheet */
static unsigned bitrates[] = {
  [0] = 250,  // kb/s |  -100 dBm
  [1] = 500,  // kb/s |   -96 dBm
  [2] = 1000, // kb/s |   -94 dBm
  [3] = 2000, // kb/s |   -86 dBm
};

/* Page 120/121 of the 256RFR2 datasheet */
static uint8_t MIN_CHANNEL=11;
static uint8_t MAX_CHANNEL=26;


template<typename T, unsigned int sz>
inline unsigned int lengthof(const T (&)[sz]) { return sz; }

// A queue of packets captured, awaiting 
struct Queue {
  struct Packet {
    uint8_t size; // Total packet size
    uint8_t sent; // How many bytes were sent?
    uint8_t data[MAX_FRAME_LENGTH]; // Data
  } *packets;
  uint8_t head; // Index of next packet to enqueue
  uint8_t tail; // Index of next packet to dequeue
  uint8_t len; // Number of packets the queue has space for
};

// Calculate the next value for a queue pointer
static uint8_t next(Queue *q, uint8_t n) {
  if (n + 1 == q->len)
    return 0;
  else
    return n + 1;
}

// Read a packet from the hardware buffer into the queue
static void read_packet(Queue *q) {
  uint8_t n = next(q, q->head);

  // Queue full, just drop the packet
  if (n == q->tail)
    return;

  Queue::Packet *p = &q->packets[q->head];

  p->size = TST_RX_LENGTH_REG;
  if (p->size > sizeof(q->packets->data))
    return;

  memcpy(p->data, (const void*)&TRX_FRAME_BUFFER(0), p->size);
  p->sent = 0;
  q->head = n;
}

// Send some data from the queue over serial
static void process_packets(bool binary, Queue *q) {
  if (q->head == q->tail)
    return;

  Queue::Packet *p = &q->packets[q->tail];

  if (binary && p->sent == 0)
    Serial.write(p->size);

  // Only send a single data packet each time, to prevent blocking on
  // the serial port for too long and missing a packet.
  if (p->size) {
    uint8_t b = p->data[p->sent];
    if (binary) {
      Serial.write(b);
    } else {
      if (b < 0x10)
        Serial.write('0');
      Serial.print(b, HEX);
    }
    p->sent++;
  }

  if (p->sent == p->size) {
    q->tail = next(q, q->tail);

    if (!binary)
      Serial.println();
  }
}

static void start_sniffing(uint8_t bitrate_idx, uint8_t channel, bool binary) {
  // Allocate a queue of packet payloads as big as possible - we won't
  // need any more memory after this (malloc reserves a bit of memory
  // for the stack which is more than enough for the few function calls
  // and ISRs we've left to do).
  Queue q = {
    .packets = NULL,
    .head = 0,
    .tail = 0,
    .len = 0,
  };

  while(q.len < 256 /* head and tail are uin8_t */) {
    void* newp = realloc(q.packets, (q.len + 1) * sizeof(*q.packets));
    // No more memory, done
    if (!newp)
      break;
    q.packets = (Queue::Packet*)newp;
    q.len++;
  }

  Serial.print(F("Buffer size is "));
  Serial.print(q.len);
  Serial.println(F(" packets."));

  if (!binary) {
    Serial.println(F("Starting capture with text output"));
  } else {
    Serial.println(F("Starting capture with binary output"));
    // Magic string for syncing the stream. Insired by
    // http://cetic.github.io/foren6/guide.html and
    // https://github.com/cetic/contiki/tree/sniffer/examples/sniffer
    // though we do not use the same enable sequence (yet?)
    Serial.write("SNIF");
  }

  phyInit(bitrate_idx, channel);

  // This sets up 'promiscuous' mode, in the sense that it just receives
  // _all_ packets, without checking for validity, addressing or even CRC.
  // This is different from setting the sniffer mode suggested in the
  // datasheet, which works in RX_AACK state with AACK_PROM_MODE and
  // AACK_DIS_AC enabled. That approach still does  CRC checks and
  // possibly also some address filter checks, which we don't want here.
  phyTrxSetState(TRX_CMD_RX_ON);

  TRX_CTRL_2_REG_s.rxSafeMode = 1;
  while(true) {
    if (IRQ_STATUS_REG_s.rxEnd)
    {
      read_packet(&q);
      while (TRX_STATUS_RX_ON != TRX_STATUS_REG_s.trxStatus);

      IRQ_STATUS_REG_s.rxEnd = 1;
      TRX_CTRL_2_REG_s.rxSafeMode = 0;
      TRX_CTRL_2_REG_s.rxSafeMode = 1;
    }
    process_packets(binary, &q);
  }
}

void phyInit(uint8_t bitrate_idx, uint8_t channel) {
  TRXPR_REG_s.trxrst = 1;
  phyTrxSetState(TRX_CMD_TRX_OFF);

  // Set channel, normally band=0 and channel specifies the IEEE802.4
  // channel number, but with non-zero bands more fine-grained
  // frequencies can be configured. 
  phySetChannel(/* band */ 0, /* channel */ channel);

  phySetRate(bitrate_idx);
}

void setup() {
  uint8_t channel = 20;
  uint16_t bitrate_idx = 0;
  bool binary = false;

  Serial.begin(115200);
  Serial.println();
  Serial.println(F("802.15.4 packet sniffer starting..."));
  Serial.println(F(" - To configure bitrate, send e.g.: B250"));

  Serial.print(F("   Supported bitrates: "));
  for (size_t i = 0; i < lengthof(bitrates); ++i) {
    if (i != 0) Serial.print(", ");
    Serial.print(bitrates[i]);
  }
  Serial.print(F(" (default "));
  Serial.print(bitrates[bitrate_idx]);
  Serial.println(F(" kb/s)"));

  Serial.println(F(" - To configure channel, send e.g.: C11"));
  Serial.print(F("   Supported channels: "));
  Serial.print(MIN_CHANNEL);
  Serial.print(F("-"));
  Serial.print(MAX_CHANNEL);
  Serial.print(F(" (default "));
  Serial.print(channel);
  Serial.println(F(")"));

  do {
    int c = Serial.read();

    if (c < 0) {
      continue;
    } else if (c == '\n' || c == '\r') {
      // Ignore any extra newlines (in case settings are terminated with
      // \r\n, the extra one might end up here
    } else if (c == '#') {
      binary = false;
      break;
    } else if (c == '!') {
      binary = true;
      break;
    } else if (c == 'B' || c == 'C') {
      // Read a newline-terminated integer.
      // This code is way too verbose, but the Arduino parseInt and
      // readBytesUntil are just too imprecise wrt corner cases.
      uint16_t value = 0;
      do {
        int d = Serial.peek();
        if (d < 0) {
          continue;
        } else if (d >= '0' && d <= '9') {
          Serial.read();
          value *= 10;
          value += (d - '0');
        } else {
          break;
        }
      } while(true);

      if (c == 'B') {
        size_t i = 0;
        for (; i < lengthof(bitrates); ++i) {
          if (value == bitrates[i]) {
            bitrate_idx = i;
            break;
          }
        }

        if (i == lengthof(bitrates)) {
          Serial.print(F("Unsupported bitrate: "));
          Serial.println(value);
        } else {
          Serial.print(F("Setting bitrate: "));
          Serial.println(value);
        }
      } else { // c == 'C'
        if (value >= MIN_CHANNEL && value <= MAX_CHANNEL) {
          Serial.print(F("Setting channel: "));
          Serial.println(value);

          channel = value;
        } else {
          Serial.print(F("Unsupported channel: "));
          Serial.println(value);
        }
      }
    } else {
      Serial.print(F("Unexpected byte received: 0x"));
      Serial.println(c, HEX);
    }
  } while (true);

  start_sniffing(bitrate_idx, channel, binary);
}

void loop() {
}

