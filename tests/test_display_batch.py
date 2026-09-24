#!/usr/bin/env python3
"""Execute production text rendering against a pixel-addressed LCD model."""
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode=True
from test_idle_handoff import extract_function
ROOT=Path(__file__).resolve().parents[1]
names=['cardputer_display_clear','cardputer_display_begin_batch','cardputer_display_end_batch',
       'glyph_scaled','cardputer_display_layout_char','cardputer_display_putc',
       'cardputer_display_defer_newline','cardputer_display_write']
bodies=[extract_function(ROOT/'src/kernel/display.c',n) for n in names]
bodies.append(extract_function(ROOT/'src/kernel/tty.c','cp32_cardputer_key'))
PRELUDE=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define CP32_IRAM_EXT
#define DC 1
#define CS 2
static unsigned x,y,ready,spi_fault,display_batch,display_redraw_pending;
static char textbuf[12][16], painted[12][16];
static uint16_t pixels[240*135], expected_pixels[240*135];
static char expected_text[12][16];
static unsigned wx,wy,ww,wh,pos,clears,cells;
static unsigned cell_writes[12][16];
static void out(unsigned pin,unsigned value) { (void)pin; (void)value; }
static void win(unsigned a,unsigned b,unsigned c,unsigned d) {
  assert(c<240 && d<135 && a<=c && b<=d);
  wx=a; wy=b; ww=c-a+1; wh=d-b+1; pos=0;
  if(ww==240 && wh==135) clears++;
  if(ww==15 && wh==10) { cells++; cell_writes[b/11][a/15]++; }
}
static void px(uint16_t value) {
  assert(pos<ww*wh); pixels[(wy+pos/ww)*240+wx+pos%ww]=value; pos++;
}
'''
TESTS=r'''
static void reset(void) {
  x=y=display_batch=display_redraw_pending=spi_fault=clears=cells=0; ready=1;
  memset(textbuf,' ',sizeof(textbuf)); memset(painted,' ',sizeof(painted)); memset(pixels,0,sizeof(pixels));
  memset(cell_writes,0,sizeof(cell_writes));
}
static void seed(void) {
  for(int i=0;i<11;i++) cardputer_display_write("seed text\n");
  assert(y==121); clears=0;
}
static void seed_distinct_rows(void) {
  for(int i=0;i<11;i++) {
    char row[17]; memset(row,'A'+i,16); row[16]=0;
    cardputer_display_write(row); cardputer_display_putc('\n');
  }
  assert(y==121); clears=0;
}
int main(void) {
  const char *lines="\nramdisk\nboot\nREADME\n[CMD ls capacity=65536 formatted=0]\n$ ";
  reset(); seed();
  for(const char *p=lines;*p;p++) cardputer_display_putc(*p);
  assert(clears==0); unsigned oldcells=cells, final_x=x,final_y=y;
  memcpy(expected_pixels,pixels,sizeof(pixels)); memcpy(expected_text,textbuf,sizeof(textbuf));
  reset(); seed(); cardputer_display_begin_batch();
  cardputer_display_write("\nramdisk\n");
  cardputer_display_write("boot\nREADME\n");
  cardputer_display_write("[CMD ls capacity=65536 formatted=0]\n$ ");
  assert(clears==0 && display_batch==1 && display_redraw_pending);
  cardputer_display_end_batch();
  assert(clears==0 && !display_batch && !display_redraw_pending);
  assert(x==final_x && y==final_y && x==30);
  assert(!memcmp(expected_pixels,pixels,sizeof(pixels)));
  assert(!memcmp(expected_text,textbuf,sizeof(textbuf)));
  unsigned batchedcells=cells;
  assert(batchedcells<oldcells);
  cardputer_display_end_batch(); assert(!display_batch && clears==0 && cells==batchedcells);
  /* Independent clean-screen rasterization must match the incremental LCD. */
  memcpy(expected_pixels,pixels,sizeof(pixels)); memset(pixels,0,sizeof(pixels));
  unsigned save_x=x,save_y=y;
  for(unsigned r=0;r<12;r++) for(unsigned c=0;c<16;c++) {
    x=c*15; y=r*11; glyph_scaled(textbuf[r][c]);
  }
  assert(!memcmp(expected_pixels,pixels,sizeof(pixels))); x=save_x; y=save_y;
  /* Enter at the bottom and the entire response must share ONE paint pass.
   * Compare with an immediate echo followed by a separate command repaint. */
  const char *response="ramdisk\nboot\nREADME\n[CMD ls capacity=65536 formatted=0]\n$ ";
  reset(); seed_distinct_rows(); cardputer_display_write("$ ls");
  cells=0; cardputer_display_putc('\n'); cardputer_display_write(response);
  unsigned two_pass_cells=cells, response_x=x, response_y=y;
  memcpy(expected_pixels,pixels,sizeof(pixels)); memcpy(expected_text,textbuf,sizeof(textbuf));
  reset(); seed_distinct_rows(); cardputer_display_write("$ ls");
  cells=0; memset(cell_writes,0,sizeof(cell_writes));
  cardputer_display_defer_newline();
  assert(!cells && !display_batch && display_redraw_pending);
  cardputer_display_begin_batch();
  cardputer_display_write(response);
  assert(!cells && display_batch==1); /* Not even an intermediate glyph. */
  cardputer_display_end_batch();
  assert(!clears && cells<two_pass_cells && x==response_x && y==response_y);
  assert(!memcmp(expected_pixels,pixels,sizeof(pixels)));
  assert(!memcmp(expected_text,textbuf,sizeof(textbuf)));
  for(unsigned r=0;r<12;r++) for(unsigned c=0;c<16;c++) assert(cell_writes[r][c]<=1);
  unsigned one_pass_cells=cells;
  /* Ordinary typing, even without an explicit batch, commits pending echo. */
  reset(); seed(); cardputer_display_putc('\n'); cardputer_display_putc('Q');
  memcpy(expected_pixels,pixels,sizeof(pixels));
  reset(); seed(); cells=0; cardputer_display_defer_newline();
  assert(!cells); cardputer_display_putc('Q');
  assert(cells && !display_redraw_pending && !memcmp(expected_pixels,pixels,sizeof(pixels)));
  /* A batch longer than the screen keeps only the final visible layout. */
  reset();
  for(int i=0;i<30;i++) cardputer_display_write("abcdefghijklmnopQ\n");
  memcpy(expected_pixels,pixels,sizeof(pixels)); memcpy(expected_text,textbuf,sizeof(textbuf));
  reset(); cardputer_display_begin_batch();
  for(int i=0;i<30;i++) cardputer_display_write("abcdefghijklmnopQ\n");
  assert(!cells && !clears); cardputer_display_end_batch();
  assert(!memcmp(expected_pixels,pixels,sizeof(pixels)));
  assert(!memcmp(expected_text,textbuf,sizeof(textbuf)));
  for(unsigned r=0;r<12;r++) for(unsigned c=0;c<16;c++) assert(cell_writes[r][c]<=1);
  /* Transient glyphs overwritten in a batch never reach the panel. */
  reset(); cardputer_display_write("A\bB\b "); assert(!cells && x==15);
  /* Full bottom line wraps through the text buffer instead of overwriting it. */
  reset(); y=121; cardputer_display_write("abcdefghijklmnopQ");
  assert(!memcmp(textbuf[10],"abcdefghijklmnop",16));
  assert(textbuf[11][0]=='Q' && x==15 && y==121 && clears==0);
  /* Exact-width line followed by newline advances only once. */
  reset(); cardputer_display_write("abcdefghijklmnop\nQ");
  assert(textbuf[1][0]=='Q' && x==15 && y==11 && clears==0);
  /* Overwriting a character with a space erases its pixels. */
  reset(); cardputer_display_write("A\b ");
  for(unsigned i=0;i<240*135;i++) assert(!pixels[i]);
  /* Canonical erase crosses a wrapped LCD row using BS-space-BS. */
  reset(); cardputer_display_write("abcdefghijklmnopQ\b \b\b \b");
  assert(x==225 && y==0 && textbuf[0][15]==' ' && textbuf[1][0]==' ');
  assert(!memcmp(textbuf[0],"abcdefghijklmno",15));
  cardputer_display_write("clear me"); cardputer_display_clear();
  assert(clears==1 && x==0 && y==0);
  for(unsigned r=0;r<12;r++) for(unsigned c=0;c<16;c++)
    assert(textbuf[r][c]==' ' && painted[r][c]==' ');
  for(unsigned i=0;i<240*135;i++) assert(!pixels[i]);
  /* Hyphen is one horizontal stroke, with no fallback-pattern pixels. */
  /* Period is one 2x2 baseline dot, including in real directory output. */
  reset(); cardputer_display_write("AaCc");
  {
    const char *rows[4][5]={
      {"01110","10001","11111","10001","10001"},
      {"00000","01110","00001","01111","01111"},
      {"01110","10001","10000","10001","01110"},
      {"00000","01110","10000","10000","01110"}
    };
    assert(x==60 && y==0 && cells==4);
    for(unsigned k=0;k<4;k++) for(unsigned py=0;py<10;py++)
      for(unsigned px=0;px<15;px++) {
        unsigned lit=px>=4 && px<14 && rows[k][py/2][(px-4)/2]=='1';
        assert(pixels[py*240+k*15+px]==(lit ? 0xFFFF : 0));
      }
  }
  reset(); cardputer_display_write(".\n..");
  assert(x==30 && y==11 && cells==3);
  for(unsigned py=0;py<135;py++) for(unsigned px=0;px<240;px++) {
    unsigned dot=(py>=8 && py<10 && px>=8 && px<10) ||
                 (py>=19 && py<21 && ((px>=8 && px<10) || (px>=23 && px<25)));
    assert(pixels[py*240+px]==(dot ? 0xFFFF : 0));
  }
  cardputer_display_write("\b \b");
  assert(x==15 && y==11 && textbuf[1][1]==' ');
  for(unsigned py=11;py<22;py++) for(unsigned px=15;px<30;px++)
    assert(!pixels[py*240+px]);
  reset(); cardputer_display_write("-");
  assert(x==15 && y==0 && textbuf[0][0]=='-');
  for(unsigned py=0;py<135;py++) for(unsigned px=0;px<240;px++)
    assert(pixels[py*240+px]==((py>=4 && py<6 && px>=2 && px<12) ? 0xFFFF : 0));
  reset(); cardputer_display_write("_");
  for(unsigned py=0;py<135;py++) for(unsigned px=0;px<240;px++)
    assert(pixels[py*240+px]==((py>=8 && py<10 && px>=2 && px<12) ? 0xFFFF : 0));
  reset(); cardputer_display_write("/");
  for(unsigned py=0;py<10;py++) for(unsigned px=0;px<15;px++) {
    unsigned start=10-2*(py/2);
    assert(pixels[py*240+px]==((px>=start && px<start+2) ? 0xFFFF : 0));
  }
  char key=0;
  assert(cp32_cardputer_key(55|0x80,&key)==1 && key=='-');
  assert(cp32_cardputer_key(0x87,&key)==0); /* Aa/Shift pressed. */
  assert(cp32_cardputer_key(55|0x80,&key)==1 && key=='_');
  assert(cp32_cardputer_key(7,&key)==0); /* Shift released. */
  assert(cp32_cardputer_key(55|0x80,&key)==1 && key=='-');
  assert(cp32_cardputer_key(55,&key)==0);
  /* Official matrix: Fn=3, Aa=7, Ctrl=4, a=13. Releasing Aa while
   * a remains held must not emit a second character or latch uppercase. */
  for(unsigned cycle=0;cycle<20;cycle++) {
    assert(cp32_cardputer_key(0x8d,&key)==1 && key=='a');
    assert(cp32_cardputer_key(13,&key)==0);
    assert(cp32_cardputer_key(0x87,&key)==0);
    assert(cp32_cardputer_key(0x8d,&key)==1 && key=='A');
    assert(cp32_cardputer_key(7,&key)==0);
    assert(cp32_cardputer_key(13,&key)==0);
    assert(cp32_cardputer_key(0x8d,&key)==1 && key=='a');
    assert(cp32_cardputer_key(0x83,&key)==0); /* Fn is not Shift. */
    assert(cp32_cardputer_key(0x8d,&key)==1 && key=='a');
    assert(cp32_cardputer_key(3,&key)==0);
  }
  assert(cp32_cardputer_key(0x84,&key)==0);
  assert(cp32_cardputer_key(0x87,&key)==0);
  assert(cp32_cardputer_key(0x8d,&key)==1 && key==1); /* Ctrl-Shift-A */
  assert(cp32_cardputer_key(4,&key)==0);
  assert(cp32_cardputer_key(7,&key)==0);
  assert(cp32_cardputer_key(0x8d,&key)==1 && key=='a');
  for(unsigned e=0;e<256;e++) {
    unsigned code=(e&127)-1;
    if(code/10>=7 || code%10>=8) assert(cp32_cardputer_key(e,&key)==0);
  }
  reset(); spi_fault=1; cardputer_display_write("hidden\n");
  assert(!display_batch && clears==0 && x==0 && y==0);
  printf("LCD model: zero scroll clears, %u versus %u cell writes, pixel/cursor/wrap/erasure checks passed\n",batchedcells,oldcells);
  printf("Enter plus command: %u cell writes in one pass versus %u across two passes\n",one_pass_cells,two_pass_cells);
  return 0;
}
'''
prototypes='\n'.join(b[:b.index('{')].rstrip()+';' for b in bodies)
with tempfile.TemporaryDirectory(prefix='cp32-display-') as folder:
 p=Path(folder); (p/'test.c').write_text(PRELUDE+prototypes+'\n'+'\n'.join(bodies)+TESTS)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
