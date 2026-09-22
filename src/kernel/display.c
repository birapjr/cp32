#include "display.h"
#include <stdint.h>
#define GPIO_BASE 0x60004000UL
#define GPIO_OUT_W1TS (GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC (GPIO_BASE + 0x0C)
#define GPIO_OUT1_W1TS (GPIO_BASE + 0x14)
#define GPIO_OUT1_W1TC (GPIO_BASE + 0x18)
#define GPIO_ENABLE_W1TS (GPIO_BASE + 0x24)
#define GPIO_ENABLE1_W1TS (GPIO_BASE + 0x30)
#define GPIO_FUNC_OUT(p) (GPIO_BASE + 0x554 + ((p) * 4))
#define SPI3_BASE 0x60025000UL
#define SPI_CMD (SPI3_BASE + 0x00)
#define SPI_CTRL (SPI3_BASE + 0x08)
#define SPI_CLOCK (SPI3_BASE + 0x0C)
#define SPI_CLK_GATE (SPI3_BASE + 0xE8)
#define SPI_USER (SPI3_BASE + 0x10)
#define SPI_MS_DLEN (SPI3_BASE + 0x1C)
#define SPI_W0 (SPI3_BASE + 0x98)
#define SPI_USR (1u << 24)
#define SPI_UPDATE (1u << 23)
#define SPI_USR_MOSI (1u << 27)
#define SPI_SIO (1u << 17)
#define SPI_MST_CLK_ACTIVE (1u << 1)
#define SPI_MST_CLK_SEL (1u << 2)
#define SPI_SPI3_D_OUT 68u
#define SPI_SPI3_CLK_OUT 66u
#define SYSTEM_PERIP_CLK_EN0 0x600C0018UL
#define SYSTEM_PERIP_RST_EN0 0x600C0020UL
#define SYSTEM_SPI3_CLK_EN (1u << 16)
#define SYSTEM_LEDC_CLK_EN (1u << 11)
#define LEDC_BASE 0x60019000UL
#define LEDC_LSTIMER3_CONF (LEDC_BASE + 0xB8)
#define LEDC_LSCH7_CONF0 (LEDC_BASE + 0x8C)
#define LEDC_LSCH7_DUTY (LEDC_BASE + 0x94)
#define LEDC_LSCH7_CONF1 (LEDC_BASE + 0x98)
#define LEDC_CONF (LEDC_BASE + 0xD0)
#define LEDC_CLK_EN (1u << 31)
#define LEDC_PARA_UP (1u << 25)
#define LEDC_DUTY_START (1u << 31)
#define LEDC_SIG_OUT_EN (1u << 2)
#define IO_MUX_BASE 0x60009000UL
#define IO_MUX_PIN(p) (IO_MUX_BASE + 0x04 + ((p) * 4))
#define IO_MUX_MCU_SEL (7u << 12)
#define IO_MUX_GPIO (1u << 12)
#define IO_MUX_IE (1u << 9)
#define BL 38u
#define RST 33u
#define DC 34u
#define MOSI 35u
#define SCK 36u
#define CS 37u
#define BIT(p) (1u << (p))
static unsigned ready, x, y, spi_fault, spi_transactions, spi_timeouts;
static char textbuf[12][16];
/* Last character actually painted in each opaque 15x10 text cell. */
static char painted[12][16];
static unsigned display_batch, display_redraw_pending;
static void out(unsigned p,int v){
  if (p < 32u) *(volatile uint32_t *)(v?GPIO_OUT_W1TS:GPIO_OUT_W1TC)=BIT(p);
  else *(volatile uint32_t *)(v?GPIO_OUT1_W1TS:GPIO_OUT1_W1TC)=BIT(p-32u);
}
static void oe(unsigned p){
  if (p < 32u) *(volatile uint32_t *)GPIO_ENABLE_W1TS=BIT(p);
  else *(volatile uint32_t *)GPIO_ENABLE1_W1TS=BIT(p-32u);
}
static void wait_short(void){volatile unsigned n=1;while(n--);}
/* Keep panel reset delays conservative without blocking boot for seconds. */
static void wait_long(void){volatile unsigned n=30000;while(n--);}
static void pad(unsigned p){volatile uint32_t *r=(volatile uint32_t *)IO_MUX_PIN(p);*r=(*r&~IO_MUX_MCU_SEL)|IO_MUX_GPIO|IO_MUX_IE;}
static void backlight_init(void){
  *(volatile uint32_t *)SYSTEM_PERIP_CLK_EN0|=SYSTEM_LEDC_CLK_EN;
  *(volatile uint32_t *)LEDC_CONF|=LEDC_CLK_EN;
  *(volatile uint32_t *)LEDC_LSTIMER3_CONF=(305u<<4)|10u|LEDC_PARA_UP;
  *(volatile uint32_t *)LEDC_LSTIMER3_CONF= (305u<<4)|10u;
  *(volatile uint32_t *)LEDC_LSCH7_DUTY=1023u;
  *(volatile uint32_t *)LEDC_LSCH7_CONF0=3u|LEDC_SIG_OUT_EN;
  *(volatile uint32_t *)LEDC_LSCH7_CONF1=LEDC_DUTY_START;
  *(volatile uint32_t *)GPIO_FUNC_OUT(BL)=80u|(1u<<9);
}
static void spi(uint8_t v){
  spi_transactions++;
  /* Software SPI fallback: isolate panel wiring from the GP-SPI peripheral. */
  { unsigned bit;
    for (bit=0; bit<8; bit++) {
      out(MOSI,(v & (0x80u >> bit)) != 0);
      out(SCK,1);
      wait_short();
      out(SCK,0);
    }
  }
}
static void cmd(uint8_t v){out(DC,0);out(CS,0);spi(v);out(CS,1);}
static void dat(uint8_t v){out(DC,1);out(CS,0);spi(v);out(CS,1);}
static void seq(uint8_t c,const uint8_t *v,unsigned n){unsigned i;out(DC,0);out(CS,0);spi(c);out(DC,1);for(i=0;i<n;i++)spi(v[i]);out(CS,1);}
/* M5GFX rotation 1 swaps the Cardputer offsets: logical 240x135 maps to
 * controller column offset 40 and row offset 53. */
static void tx4(uint8_t command_byte,unsigned a,unsigned b){
  out(DC,0); out(CS,0); spi(command_byte); out(DC,1);
  spi(a>>8); spi(a); spi(b>>8); spi(b); out(CS,1);
}
static void win(unsigned a,unsigned b,unsigned c,unsigned d){
  /* M5GFX keeps logical X/Y in CASET/RASET; rotation is handled by MADCTL. */
  unsigned x0=a+40, x1=c+40;
  unsigned y0=b+53, y1=d+53;
  tx4(0x2A,x0,x1); tx4(0x2B,y0,y1); cmd(0x2C);
}
static void px(uint16_t v){spi(v>>8);spi(v);}
static void glyph(char c){static const uint8_t f[26][5]={{14,17,17,31,17},{30,17,30,17,30},{14,17,16,17,14},{30,17,17,17,30},{31,16,30,16,31},{31,16,30,16,16},{14,16,23,17,14},{17,17,31,17,17},{14,4,4,4,14},{7,2,2,18,12},{17,18,28,18,17},{16,16,16,16,31},{17,27,21,17,17},{17,25,21,19,17},{14,17,17,17,14},{30,17,30,16,16},{14,17,17,21,14},{30,17,30,18,17},{15,16,14,1,30},{31,4,4,1,30},{17,17,17,17,14},{17,17,17,10,4},{17,17,21,27,17},{17,10,4,10,17},{17,10,4,4,4},{31,2,4,8,31}};uint8_t r[5];unsigned i,j,sx,sy;if(c>='a'&&c<='z')for(i=0;i<5;i++)r[i]=f[c-'a'][i];else if(c>='A'&&c<='Z')for(i=0;i<5;i++)r[i]=f[c-'A'][i];else for(i=0;i<5;i++)r[i]=c==' '?0:(uint8_t)(0x11^(c*13u+i*7u));win(x,y,x+11,y+13);out(DC,1);out(CS,0);for(j=0;j<7;j++)for(sy=0;sy<2;sy++)for(i=0;i<6;i++)for(sx=0;sx<2;sx++)px(i<5&&(r[i]&(1u<<j))?0xFFFF:0);out(CS,1);x+=12;if(x>227){x=0;y+=14;}if(y>114){x=0;y=0;}}
void cardputer_display_init(void){
  unsigned p;
  *(volatile uint32_t *)SYSTEM_PERIP_CLK_EN0|=SYSTEM_SPI3_CLK_EN;
  *(volatile uint32_t *)SYSTEM_PERIP_RST_EN0|=SYSTEM_SPI3_CLK_EN;
  wait_short();
  *(volatile uint32_t *)SYSTEM_PERIP_RST_EN0&=~SYSTEM_SPI3_CLK_EN;
  *(volatile uint32_t *)SPI_CTRL=0;
  /* Match M5GFX Bus_SPI: this is a write-only, MOSI-only transaction. */
  *(volatile uint32_t *)SPI_USER=SPI_USR_MOSI;
  *(volatile uint32_t *)SPI_CMD=0;
  *(volatile uint32_t *)SPI_CLK_GATE=SPI_MST_CLK_ACTIVE|SPI_MST_CLK_SEL;
  /* M5GFX FreqToClockDiv(80 MHz APB, 40 MHz SPI) => 0x1001. */
  *(volatile uint32_t *)SPI_CLOCK=(1u<<12)|1u;
  *(volatile uint32_t *)SPI_CMD=SPI_UPDATE;
  { volatile unsigned sync=100; while (*(volatile uint32_t *)SPI_CMD & SPI_UPDATE) if (!--sync) { spi_fault=1; spi_timeouts++; break; } }
  for(p=33;p<=38;p++){pad(p);oe(p);out(p,0);}
  /* Select the GPIO output signal for the temporary software-SPI path. */
  *(volatile uint32_t *)GPIO_FUNC_OUT(MOSI)=256u|(1u<<10);
  *(volatile uint32_t *)GPIO_FUNC_OUT(SCK)=256u|(1u<<10);
  out(CS,1);out(SCK,0);out(RST,0);backlight_init();
  wait_long();
  out(RST,1);
  wait_long();
  cmd(1);
  wait_long();
  { static const uint8_t b7[]={0x35}; seq(0xB7,b7,1); }
  { static const uint8_t bb[]={0x28}; seq(0xBB,bb,1); }
  { static const uint8_t c0[]={0x0C}; seq(0xC0,c0,1); }
  { static const uint8_t c2[]={0x01,0xFF}; seq(0xC2,c2,2); }
  { static const uint8_t c3[]={0x10}; seq(0xC3,c3,1); }
  { static const uint8_t c4[]={0x20}; seq(0xC4,c4,1); }
  { static const uint8_t d0[]={0xA4,0xA1}; seq(0xD0,d0,2); }
  { static const uint8_t b0[]={0x00,0xC0}; seq(0xB0,b0,2); }
  { static const uint8_t e0[]={0xD0,0x00,0x02,0x07,0x0A,0x28,0x32,0x44,0x42,0x06,0x0E,0x12,0x14,0x17}; seq(0xE0,e0,14); }
  { static const uint8_t e1[]={0xD0,0x00,0x02,0x07,0x0A,0x28,0x31,0x54,0x47,0x0E,0x1C,0x17,0x1B,0x1E}; seq(0xE1,e1,14); }
  cmd(0x11);
  wait_long();
  cmd(0x13);cmd(0x29);
  cmd(0x3A);dat(0x55);cmd(0x36);dat(0x60);cmd(0x21);
  x=y=0;ready=1;
  { unsigned row,col; for(row=0;row<12;row++) for(col=0;col<16;col++) textbuf[row][col]=' '; }
}
void cardputer_display_clear(void){
  unsigned n,row,col;
  if(!ready||spi_fault)return;
  win(0,0,239,134);
  out(DC,1); out(CS,0);
  for(n=0;n<240u*135u;n++) px(0x0000);
  out(CS,1);
  x=y=0;
  for(row=0;row<12;row++) for(col=0;col<16;col++) textbuf[row][col]=painted[row][col]=' ';
  display_redraw_pending=0;
}
static void glyph_scaled(char c);
/* Lay out the complete batch in textbuf before any pixel transfer. Pending
 * echo-newline scrolling belongs to the next batch, so begin must retain it.
 * At outer commit, paint each changed cell once at its final screen position. */
void cardputer_display_begin_batch(void){
  display_batch++;
}
void cardputer_display_end_batch(void){
  unsigned row,col,saved_x,saved_y;
  if (!display_batch || --display_batch || !display_redraw_pending) return;
  display_redraw_pending=0;
  if (!ready || spi_fault) return;
  saved_x=x; saved_y=y;
  for(row=0;row<12;row++) for(col=0;col<16;col++) {
    if(textbuf[row][col]==painted[row][col]) continue;
    x=col*15; y=row*11; glyph_scaled(textbuf[row][col]);
    painted[row][col]=textbuf[row][col];
  }
  x=saved_x; y=saved_y;
}
static void glyph_scaled(char c){
  static const uint8_t f[26][5]={{14,17,31,17,17},{30,17,30,17,30},{14,17,16,17,14},{30,17,17,17,30},{31,16,30,16,31},{31,16,30,16,16},{14,16,23,17,14},{17,17,31,17,17},{14,4,4,4,14},{7,2,2,18,12},{17,18,28,18,17},{16,16,16,16,31},{17,27,21,17,17},{17,25,21,19,17},{14,17,17,17,14},{30,17,30,16,16},{14,17,17,21,14},{30,17,30,18,17},{15,16,14,1,30},{31,4,4,1,30},{17,17,17,17,14},{17,17,17,10,4},{17,17,21,27,17},{17,10,4,10,17},{17,10,4,4,4},{31,2,4,8,31}};
  static const uint8_t d[10][5]={{14,17,17,17,14},{4,12,4,4,14},{14,1,6,8,31},{30,1,14,1,30},{2,6,10,31,2},{31,16,30,1,30},{14,16,30,17,14},{31,1,2,4,4},{14,17,14,17,14},{14,17,15,1,14}};
  static const uint8_t l[26][5]={{0,14,1,15,15},{16,16,30,17,30},{0,14,16,16,14},{1,1,15,17,15},{0,14,31,16,14},{6,9,28,8,8},{0,15,17,15,1},{16,16,30,17,17},{4,0,12,4,14},{2,0,6,2,18},{16,18,28,18,17},{12,4,4,4,14},{0,26,21,17,17},{0,30,17,17,17},{0,14,17,17,14},{0,30,17,30,16},{0,15,17,15,1},{0,22,25,16,16},{0,15,28,3,30},{8,8,28,8,7},{0,17,17,19,13},{0,17,17,10,4},{0,17,21,21,10},{0,17,10,4,10},{0,17,17,15,1},{0,31,2,4,31}};
  uint8_t r[5]; unsigned i,row,col,sx,sy;
  if(c==' ') for(i=0;i<5;i++) r[i]=0;
  else if(c>='a'&&c<='z') for(i=0;i<5;i++) r[i]=l[c-'a'][i];
  else if(c>='A'&&c<='Z') for(i=0;i<5;i++) r[i]=f[c-'A'][i];
  else if(c>='0'&&c<='9') for(i=0;i<5;i++) r[i]=d[c-'0'][i];
  else if(c=='[') { r[0]=6; r[1]=4; r[2]=4; r[3]=4; r[4]=6; }
  else if(c==']') { r[0]=12; r[1]=4; r[2]=4; r[3]=4; r[4]=12; }
  else if(c=='-') { r[0]=0; r[1]=0; r[2]=62; r[3]=0; r[4]=0; }
  else if(c=='_') { r[0]=0; r[1]=0; r[2]=0; r[3]=0; r[4]=62; }
  else if(c=='/') { r[0]=2; r[1]=4; r[2]=8; r[3]=16; r[4]=32; }
  else if(c==':') { r[0]=0; r[1]=8; r[2]=0; r[3]=8; r[4]=0; }
  else if(c=='.') { r[0]=0; r[1]=0; r[2]=0; r[3]=0; r[4]=4; }
  else if(c=='=') { r[0]=0; r[1]=31; r[2]=0; r[3]=31; r[4]=0; }
  else if(c=='$') { r[0]=4; r[1]=30; r[2]=5; r[3]=30; r[4]=4; }
  else for(i=0;i<5;i++) r[i]=(c==' ')?0:(uint8_t)(0x11^(c*13u+i*7u));
  win(x,y,x+14,y+9); out(DC,1); out(CS,0);
  for(row=0;row<5;row++) for(sy=0;sy<2;sy++)
    for(col=0;col<8;col++) for(sx=0;sx<(col<7 ? 2u : 1u);sx++)
      px((col<7 && (r[row]&(1u<<(6-col)))) ? 0xFFFF : 0x0000);
  out(CS,1);
}
/* This layout pass does not send pixels, even when several rows scroll. */
static void cardputer_display_layout_char(char c){
  unsigned row,col;
  if(!ready||spi_fault)return;
  if(c=='\r')return;
  if(c=='\n'){
    x=0; y+=11;
    if(y<=123)return;
    for(row=0;row<11;row++) for(col=0;col<16;col++)
      textbuf[row][col]=textbuf[row+1][col];
    for(col=0;col<16;col++) textbuf[11][col]=' ';
    x=0; y=121;
    display_redraw_pending=1;
    return;
  }
  if(c=='\b'){
    if(x>=15)x-=15;
    else if(y>=11){y-=11;x=225;}
    return;
  }
  /* Delay wrap until the next printable character; newline after a full
   * line advances exactly once. Scrolling must update textbuf too. */
  if(x>=240) cardputer_display_layout_char('\n');
  row=y/11; col=x/15;
  if(row<12 && col<16) textbuf[row][col]=c;
  if(row<12 && col<16) display_redraw_pending=1;
  x+=15;
}
void cardputer_display_putc(char c){
  unsigned standalone=(display_batch==0);
  if(standalone) cardputer_display_begin_batch();
  cardputer_display_layout_char(c);
  if(standalone) cardputer_display_end_batch();
}
/* Enter echo advances logical state immediately but does not repaint a
 * whole scroll just before the command response. The next write/echo commits
 * that scroll together with its own layout; no batch is held across IPC. */
void cardputer_display_defer_newline(void){
  cardputer_display_layout_char('\n');
}
void cardputer_display_write(const char *s){
  cardputer_display_begin_batch();
  while(*s)cardputer_display_putc(*s++);
  cardputer_display_end_batch();
}
unsigned cardputer_display_faulted(void){return spi_fault;}
