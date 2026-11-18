/* Veranda Term: the serial port, Telnet, the modem, XMODEM (MIT licence) */
#ifndef NET_H
#define NET_H

/* ser.s: the interrupt half */
void SerIrq(void);
extern volatile unsigned char ser_on, ser_lo, ser_rxbank, ser_mcr, ser_ier, ser_flow;
extern volatile unsigned int ser_rxhead, ser_rxtail, ser_lost, ser_oe, ser_frames, ser_polls;
extern volatile unsigned char ser_txbuf[256];
extern volatile unsigned char ser_txhead, ser_txtail;
extern volatile unsigned char ser_sbank, ser_rate, ser_logbank;
extern volatile unsigned int ser_saddr, ser_send, ser_logpos;

unsigned char ser_at(unsigned char lo);
unsigned char ser_probe(void);
unsigned int ser_divisor(void);
unsigned char ser_start(void);          /* 0 ok, 1 no UART at cfg.port */
void ser_stop(void);
void ser_pause(void);
void ser_resume(void);
void ser_aside(void);                  /* asideVector (V91) */
void ser_wake(void);                   /* the main loop: armed again after it */
unsigned char ser_msr(void);
unsigned int ser_get(unsigned char *buf, unsigned int max);
void ser_byte(unsigned char c);
unsigned char ser_txroom(void);

extern unsigned char tn_on, tn_echo, tn_bin, tn_seen, tn_auto;
void net_send(unsigned char c);         /* a byte to the host (IAC doubled) */
void net_rx(const unsigned char *p, unsigned int n);
void net_dial(const char *s);
void net_hangup(void);
void net_tick(void);
extern unsigned char syncing;           /* (V94) the modem's rate looked for: */
void o_sync(void);                      /* sync.c (overlay 2): from the start */
unsigned char o_sync_tick(void);        /* each pass of the main loop meanwhile: 1 done */
void dial_go(void);
void sends(const char *s);
void uart_rate(unsigned char i);        /* the UART at bauds[i] */
extern unsigned char net_mlen;
extern unsigned char term_ovl2;         /* term.c: overlay 2's bank */
void net_dropped(void);                 /* term.c: the line was lost */
extern unsigned char online, dialing, hangup_state, got_ok;
extern unsigned int online_since;
extern unsigned long rx_total;

struct when { unsigned char yr, mon, dy, hr, mn, sec; };
extern struct when net_time;
extern unsigned char net_time_ok;
unsigned char parse_time(const char *s, struct when *w);
void time_zone(struct when *w, signed char quarters);
unsigned char weekday(const struct when *w);

/* term.c: the capture file */
void capture_byte(unsigned char c);

/* xmodem.c */
extern unsigned char xm_active;
unsigned char xm_start(const char *name);      /* 0 ok, else a file error */
void xm_byte(unsigned char c);
void xm_tick(void);
void xm_cancel(void);
extern unsigned long xm_bytes;
extern unsigned int xm_blocks, xm_retries;
extern unsigned char xm_result;         /* 0 running, 1 done, 2 cancelled, 3 failed */
extern unsigned char xm_crc, xm_big;    /* the mode used: CRC, a 1 KB block seen */

#endif
