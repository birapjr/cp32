// kernel/wdf.h
#ifndef WDT_H
#define WDT_H

void swd_disable(void);
void rtc_wdt_disable(void);
void wdt_disable_all(void);
void wdt_feed_all(void);
void wdt_feed_super_wdt();

#endif