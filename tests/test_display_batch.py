#!/usr/bin/env python3
"""Execute production text rendering against a pixel-addressed LCD model."""
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode=True
from test_idle_handoff import extract_function
ROOT=Path(__file__).resolve().parents[1]
names=['cardputer_display_begin_batch','cardputer_display_end_batch',
       'glyph_scaled','cardputer_display_putc','cardputer_display_write']
bodies=[extract_function(ROOT/'src/kernel/display.c',n) for n in names]
PRELUDE=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define DC 1
#define CS 2
static unsigned x,y,ready,spi_fault,display_batch,display_redraw_pending;
static char textbuf[12][16];
static uint16_t pixels[240*135], expected_pixels[240*135];
static char expected_text[12][16];
static unsigned wx,wy,ww,wh,pos,clears;
static void out(unsigned pin,unsigned value) { (void)pin; (void)value; }
static void win(unsigned a,unsigned b,unsigned c,unsigned d) {
  assert(c<240 && d<135 && a<=c && b<=d);
  wx=a; wy=b; ww=c-a+1; wh=d-b+1; pos=0;
  if(ww==240 && wh==135) clears++;
}
static void px(uint16_t value) {
  assert(pos<ww*wh); pixels[(wy+pos/ww)*240+wx+pos%ww]=value; pos++;
}
'''
TESTS=r'''
static void reset(void) {
  x=y=display_batch=display_redraw_pending=spi_fault=clears=0; ready=1;
  memset(textbuf,' ',sizeof(textbuf)); memset(pixels,0,sizeof(pixels));
}
static void seed(void) {
  for(int i=0;i<11;i++) cardputer_display_write("seed text\n");
  assert(y==121); clears=0;
}
int main(void) {
  const char *lines="\nramdisk\nboot\nREADME\n[CMD ls capacity=65536 formatted=0]\n$ ";
  reset(); seed();
  for(const char *p=lines;*p;p++) cardputer_display_putc(*p);
  assert(clears>1); unsigned oldclears=clears, final_x=x,final_y=y;
  memcpy(expected_pixels,pixels,sizeof(pixels)); memcpy(expected_text,textbuf,sizeof(textbuf));
  reset(); seed(); cardputer_display_begin_batch();
  cardputer_display_write("\nramdisk\n");
  cardputer_display_write("boot\nREADME\n");
  cardputer_display_write("[CMD ls capacity=65536 formatted=0]\n$ ");
  assert(clears==0 && display_batch==1 && display_redraw_pending);
  cardputer_display_end_batch();
  assert(clears==1 && !display_batch && !display_redraw_pending);
  assert(x==final_x && y==final_y && x==30);
  assert(!memcmp(expected_pixels,pixels,sizeof(pixels)));
  assert(!memcmp(expected_text,textbuf,sizeof(textbuf)));
  cardputer_display_end_batch(); assert(!display_batch && clears==1);
  /* Full bottom line wraps through the text buffer instead of overwriting it. */
  reset(); y=121; cardputer_display_write("abcdefghijklmnopQ");
  assert(!memcmp(textbuf[10],"abcdefghijklmnop",16));
  assert(textbuf[11][0]=='Q' && x==15 && y==121 && clears==1);
  /* Exact-width line followed by newline advances only once. */
  reset(); cardputer_display_write("abcdefghijklmnop\nQ");
  assert(textbuf[1][0]=='Q' && x==15 && y==11 && clears==0);
  /* Overwriting a character with a space erases its pixels. */
  reset(); cardputer_display_write("A\b ");
  for(unsigned i=0;i<240*135;i++) assert(!pixels[i]);
  reset(); spi_fault=1; cardputer_display_write("hidden\n");
  assert(!display_batch && clears==0 && x==0 && y==0);
  printf("LCD model: identical batched pixels/cursor, %u scroll redraws reduced to 1, wrap/space/fault checks passed\n",oldclears);
  return 0;
}
'''
prototypes='\n'.join(b[:b.index('{')].rstrip()+';' for b in bodies)
with tempfile.TemporaryDirectory(prefix='cp32-display-') as folder:
 p=Path(folder); (p/'test.c').write_text(PRELUDE+prototypes+'\n'+'\n'.join(bodies)+TESTS)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
