#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "vga_private.h"
#include "vga_primitives.h"
#include "font.h"

uint memcpy_dma_chan;

// 5x7 font
void writeStringBold(char* str);
static void inc_cursor_y();
static void inc_cursor_x();
static void dec_cursor_y();
static void dec_cursor_x();

void initialize_graphos();
static void setTextCursorVisible(bool v);

static vga_t * vga = NULL ;

/* BEGIN_CURSOR_STATE */
#define CUR_CELL_W 8      /* a celula desenhada por drawChar2 e sempre 8x16 */
#define CUR_CELL_H 16
#ifndef BS_ERASES
#define BS_ERASES 1       /* 1 = backspace (0x08) anda para tras E apaga; 0 = so anda */
#endif
static bool     cur_drawn = false;   /* true = a barra do cursor esta na tela */
static int16_t  cur_px = 0;          /* celula (em pixels) onde a barra esta desenhada */
static int16_t  cur_py = 0;
static uint8_t  cur_saved[CUR_CELL_W * CUR_CELL_H];  /* pixels (4 bits) que estavam embaixo */
static uint8_t  esc_state = 0;       /* 0 normal, 1 recebeu ESC, 2 dentro de ESC [ */
static uint16_t esc_param = 0;
static bool     esc_skip = false;
/* END_CURSOR_STATE */

void PANIC(){
  panic("Erro critico no sistema!");
  while(1){
    
  }
}
vga_t * get_vga(){
    return vga;
}
static void clrscr(){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  memset(&priv->vga_data_array[0], 0, priv->txcount) ;
  cur_drawn = false;   /* framebuffer zerado: nao ha nada para restaurar */
  esc_state = 0;
  // reset cursor position
  priv->cursor->x = 0 ;
  priv->cursor->y = 0 ;
}

static void dec_cursor_x(){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  priv->cursor->x -= priv->font.width;

  if(priv->cursor->x < 0){
    priv->cursor->x = 0;       
  }
}

static void dec_cursor_y(){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  priv->cursor->y -= priv->font.height  ;
  if(priv->cursor->y < 0){
    priv->cursor->y = 0;       
  }
}

static void inc_cursor_x(){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  priv->cursor->x += priv->font.width;
  if(priv->cursor->x > 79){
    priv->cursor->x = 0; 
    inc_cursor_y();      
  }
}

static void inc_cursor_y(){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  priv->cursor->y += priv->font.height  ;
  if(priv->cursor->y > 30){
    priv->cursor->y = 0;       
  }
}


static void set_vga_data_array(uint8_t video_data_array[])
{
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  priv->vga_data_array = &video_data_array[0];  
} 

static void set_vga_home(void){
  setTextCursorVisible(false);
  vga->setTextCursorPos(0,0);
  setTextCursorVisible(true);
}

static void set_vga_mode(uint8_t mode){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  priv->video_mode = mode;
  switch(mode){
    case MODE_TEXT_40_S:
      priv->width = 320;
      priv->height= 240;
      break;
    case MODE_TEXT_40_F:
      priv->width = 320;
      priv->height= 240;
      break;
    case MODE_TEXT_80_S:
      priv->width = 640;
      priv->height= 480;
      break;
    case MODE_TEXT_80_F:
      priv->width = 640;
      priv->height= 480;
      break;
    case MODE_320x240:
      priv->width = 320;
      priv->height= 240;
      break;
    case MODE_640x480:
      priv->width = 640;
      priv->height= 480;
      break;
    default:
      priv->width = 640;
      priv->height= 480;
      priv->video_mode = MODE_TEXT_80_S;
      break;

  }
}

void drawPixel(short x, short y, color_t color) {
    vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
    if((x > (priv->width-1)) | (x < 0) | (y > (priv->height-1)) | (y < 0) ) return;

    int pixel = ((priv->width * y) + x) ;
    if (pixel & 1) {
        priv->vga_data_array[pixel>>1] = (priv->vga_data_array[pixel>>1] & priv->topmask) | (color << 4) ;
    }
    else {
        priv->vga_data_array[pixel>>1] = (priv->vga_data_array[pixel>>1] & priv->bottommask) | (color) ;
    }
}
void drawHLine_old(int x, int y, int w, color_t color) {
    vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  // range checks
  if((x >= priv->width) || (y >= priv->height)) return;
  if((x + w - 1) >= priv->width)  w = priv->width  - x - 1;
  //
  //short xx = x;
  short both_color = color | (color<<4) ;
  // loner pixel at x -- align left with next byte boundary
  if((x & 1)) {
    drawPixel(x,y,color);
    x++ ;
    w-- ;
  }
  // draw loner pixel at end and adjust width
  if((w & 1)){
    drawPixel(x+w-1, y, color);
    w-- ;
  }
  // draw rest of line
  int len = (w>>1)  ;
  if (len>0 && y<480 ) memset(&priv->vga_data_array[320*y+(x>>1)], both_color, len) ;
 }

void fillRect_old(short x, short y, short w, short h, color_t color) {
   vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
   if((y + h - 1) >= priv->height) h = priv->height - y - 1;

  for(int i=x; i <= w;i++)
      for( int j=y;j <= h;j++)
          drawPixel( i,  j, color ) ;
}



static void drawChar_interna(vga_text_private_t* priv, short x, short y, uint8_t c, color_t color, color_t bg, uint8_t size) {
  //vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  char i, j;
  if((x >= priv->width)            || // Clip right
     (y >= priv->height)           || // Clip bottom
     ((x + 6 * size - 1) < 0) || // Clip left
     ((y + 8 * size - 1) < 0))   // Clip top
    return;

    // Calcula a área do caractere
    short charWidth = 8 * size;
    short charHeight = 8 * size;    
    // PRIMEIRO: Limpa a área completa do caractere
    fillRect(x, y, charWidth, charHeight, BLACK);

  uint8_t lastColumn = 5 ;
  uint8_t altura = 8 ;
  uint8_t largura = 6 ;
  for (i=0; i<largura; i++ ) {
    uint8_t line;
    if (i == lastColumn)
      line = 0x0;
    else
      line = pgm_read_byte(font_5x7+(c*lastColumn)+i);
    for ( j = 0; j < altura; j++) {
      if (line & 0x1) {
        if (size == 1) // default size
          drawPixel(x+i, y+j, color);
        else {  // big size
          fillRect(x+(i*size), y+(j*size), size, size, color);
        }
      } else if (bg != color) {
        if (size == 1) // default size
          drawPixel(x+i, y+j, bg);
        else {  // big size
          fillRect(x+i*size, y+j*size, size, size, bg);
        }
      }
      line >>= 1;
    }
  }
}

static void drawChar2( vga_text_private_t* priv,int start_x, int start_y, uint8_t char_code, int color,  int bgcolor, uint8_t size) {
    //vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

    // Calcula a área do caractere
    size=1;
    short charWidth = 8 * size;
    short charHeight = 16 * size;    
    // PRIMEIRO: Limpa a área completa do caractere
    fillRect(start_x, start_y, charWidth, charHeight, bgcolor);

    const uint8_t *char_start = priv->font.data + (char_code * priv->font.height);
    // Percorrer todas as linhas (bytes) do caractere
    for (int row = 0; row < priv->font.height; row++) {
        uint8_t current_byte = char_start[row];

        // Percorrer todos os bits do byte (da esquerda para a direita)
        for (int bit = 7; bit >= 0; bit--) {
            // Calcular coordenadas do pixel
            int pixel_x = start_x + (7 - bit);  // 7-bit para ir da esquerda para direita
            int pixel_y = start_y + row;
            // Verificar se o bit está setado (1)
            if ((current_byte >> bit) & 1) {
                // Desenhar o pixel
                if( size == 1  )
                  drawPixel(pixel_x, pixel_y, color);
                else  
                  fillRect(pixel_x+(row*size), pixel_y+(bit*size), size, size, bgcolor);
            }else{
                // Desenhar o pixel
                if( size == 1  )
                  drawPixel(pixel_x, pixel_y, bgcolor);
                else  
                  fillRect(pixel_x+row*size, pixel_y+bit*size, size, size, bgcolor);
            }
        }
    }
}


static void drawChar(vga_text_private_t* priv,uint8_t c, color_t color, color_t bg, uint8_t size) {
  //vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  if(size == 2)
    drawChar_interna( priv,priv->cursor->x, priv->cursor->y, c, color, bg, size);
  else  
    drawChar2( priv,priv->cursor->x, priv->cursor->y, c, color, bg, size);
}
extern void scroll_up_graphics(void);

/* BEGIN_CURSOR_SLICE */
/* ====================================================================
 * CURSOR NAO DESTRUTIVO
 * Antes: a barra do cursor era desenhada POR CIMA da celula e "apagada"
 * escrevendo um espaco. Toda vez que o cursor passava sobre um caractere,
 * o caractere era destruido.
 * Agora: ao desenhar a barra guardamos os pixels que estavam embaixo e,
 * ao apagar, devolvemos esses pixels. O cursor nunca perde texto.
 * Regra: todo desenho (texto, graficos, clrscr) precisa antes tirar a
 * barra (cursor_erase / cursor_release), senao o cursor restauraria
 * pixels velhos por cima do desenho novo.
 * Todo desenho roda no core 1 (o timer so avisa o core 1).
 * ==================================================================== */
static uint8_t get_px(vga_text_private_t* priv, int x, int y) {
  if (x < 0 || y < 0 || x > (priv->width - 1) || y > (priv->height - 1)) return 0;
  int pixel = (priv->width * y) + x;
  uint8_t b = priv->vga_data_array[pixel >> 1];
  return (pixel & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 0x0F);
}

/* Tira a barra e devolve o que estava embaixo. */
static void cursor_erase(void) {
  if (!cur_drawn) return;
  for (int r = 0; r < CUR_CELL_H; r++) {
    for (int col = 0; col < CUR_CELL_W; col++) {
      drawPixel(cur_px + col, cur_py + r, cur_saved[r * CUR_CELL_W + col]);
    }
  }
  cur_drawn = false;
}

/* Guarda o que esta embaixo e desenha a barra na posicao atual do cursor. */
static void cursor_draw(vga_text_private_t* priv) {
  int16_t x = priv->cursor->x;
  int16_t y = priv->cursor->y;

  if (cur_drawn) {
    if (cur_px == x && cur_py == y) return;   /* ja esta aqui */
    cursor_erase();                           /* estava em outra celula */
  }
  cur_px = x;
  cur_py = y;
  for (int r = 0; r < CUR_CELL_H; r++) {
    for (int col = 0; col < CUR_CELL_W; col++) {
      cur_saved[r * CUR_CELL_W + col] = get_px(priv, x + col, y + r);
    }
  }
  drawChar(priv, priv->cursor->shape, priv->textcolor, priv->textbgcolor, 1);
  cur_drawn = true;
}

/* Usada pelo orion_bus.c antes de qualquer comando vindo do 68000. */
void cursor_release(void) {
  cursor_erase();
}

/* Move o cursor (ESC [ n A/B/C/D). NAO apaga nada. */
static void esc_execute(vga_text_private_t* priv, uint8_t final) {
  int n  = esc_param ? esc_param : 1;
  int fw = priv->font.width;
  int fh = priv->font.height;
  int x  = priv->cursor->x;
  int y  = priv->cursor->y;
  int maxx = (priv->width  / fw - 1) * fw;
  int maxy = (priv->height / fh - 1) * fh;

  switch (final) {
    case 'A': y -= n * fh; break;   /* cima     */
    case 'B': y += n * fh; break;   /* baixo    */
    case 'C': x += n * fw; break;   /* direita  */
    case 'D': x -= n * fw; break;   /* esquerda */
    default:  return;               /* sequencia desconhecida: ignora */
  }
  if (x < 0) x = 0;
  if (x > maxx) x = maxx;
  if (y < 0) y = 0;
  if (y > maxy) y = maxy;
  priv->cursor->x = x;
  priv->cursor->y = y;
}

void tft_write(uint8_t c){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  /* 0. Antes de mexer em qualquer coisa: tira a barra e devolve o que estava embaixo */
  cursor_erase();

  /* 1. Sequencias de escape ANSI: ESC [ n A/B/C/D (setas) */
  if (esc_state == 1) {
    if (c == '[') {
      esc_state = 2;
      esc_param = 0;
      esc_skip  = false;
      return;
    }
    esc_state = 0;          /* ESC solto: ignora o ESC e trata este byte normalmente */
  } else if (esc_state == 2) {
    if (c >= '0' && c <= '9') {
      if (!esc_skip && esc_param < 1000) esc_param = (uint16_t)(esc_param * 10 + (c - '0'));
      return;
    }
    if (c == ';') {         /* so o primeiro parametro importa para as setas */
      esc_skip = true;
      return;
    }
    if (c >= 0x40 && c <= 0x7E) {   /* byte final da sequencia */
      esc_state = 0;
      esc_execute(priv, c);
      return;
    }
    esc_state = 0;          /* byte inesperado: aborta a sequencia e trata normalmente */
  }
  if (c == 0x1B) {
    esc_state = 1;
    return;
  }

  /* 2. Caracteres normais e de controle */
  switch(c){
    case 0x0A:  // \n (faz tambem o \r)
              priv->cursor->y += priv->font.height;
              /* fall through */
    case 0x0D:  // \r
              priv->cursor->x = 0;
              break;
    case 0x08:  // backspace: anda para tras e (BS_ERASES) apaga a celula
              priv->cursor->x -= priv->font.width;
              if (priv->cursor->x < 0) {
                priv->cursor->x = 0;
              }
#if BS_ERASES
              drawChar(priv, ' ', priv->textcolor, priv->textbgcolor, priv->font.size);
#endif
              break;
    case 0x09: { // \t
              int new_x = priv->cursor->x + priv->tabspace;
              if (new_x < priv->width){
                  priv->cursor->x = new_x;
              }
              break;
    }
    default:
              drawChar(priv, c, priv->textcolor, priv->textbgcolor, priv->font.size);
              priv->cursor->x += priv->font.width;

              if(priv->cursor->x >= priv->width ) {
                priv->cursor->y += priv->font.height;
                priv->cursor->x = 0;
              }
  }
  if( priv->cursor->y >= priv->height ){
    scroll_up_graphics();
    priv->cursor->x = 0;
    priv->cursor->y -= priv->font.height;
  }
}

/* Chamada pelo "timer" de piscar (no core 1). c=1 mostra, c=0 esconde. */
void put_cursor(uint8_t c){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  if( priv->cursor->visible == false ){
    cursor_erase();                 /* escondido: so garante que nao sobrou barra */
    return;
  }
  if( priv->cursor->blink == true ){
    if( c )
      cursor_draw(priv);
    else
      cursor_erase();
  }else{
    cursor_draw(priv);              /* sem piscar: sempre visivel */
  }
}
/* END_CURSOR_SLICE */

//uint8_t bufferA[2400];
//uint8_t bufferB[2400];
//uint8_t *front_buffer = bufferA;
//uint8_t *back_buffer = bufferB;
//
//void vga_scroll() {
//    // 1. Copia da linha 1 até 29 do front para a linha 0 até 28 do back
//    // (80 colunas * 29 linhas = 2320 bytes)
//    memcpy(back_buffer, front_buffer + 80, 2320);
//
//    // 2. Limpa a nova linha 29 no back buffer
//    memset(back_buffer + 2320, ' ', 80);
//
//    // 3. TROCA OS PONTEIROS (O back vira front e vice-versa)
//    uint8_t *temp = front_buffer;
//    front_buffer = back_buffer;
//    back_buffer = temp;
//
//    // 4. Reposiciona o cursor na última linha
//    vga->setTextCursorPos(0,29);
//}

//static void setTextCursor_old(uint16_t x, uint16_t y) {
//  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
//  if((x >= priv->width) || (y >= priv->height)) 
//    return;
//  if(x*priv->font.width >= priv->width) {
//    x = x*priv->font.width / priv->font.width;
//    if( y < priv->height )
//      y = y *priv->font.height;
//    else
//      y = 0 ;
//  }else{
//    x = x*priv->font.width;
//    if( y < priv->height )
//      y = y *priv->font.height;
//    else
//      y = 0 ;
//  }
//
//  priv->cursor->x = x;
//  priv->cursor->y = y;
//}
static void setTextCursor(uint16_t x, uint16_t y) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  
  // 1. Verifica se a posição em caracteres está além da matriz de texto
  if((x >= priv->width) || (y >= priv->height)) 
    return;

  // 2. Calcula a posição real em PIXELS usando variáveis locais
  // para evitar o efeito bola de neve multiplicativa
  uint16_t pixel_x = x * priv->font.width;
  uint16_t pixel_y = y * priv->font.height;

  // 3. Garante que a posição em pixels não vai estourar a resolução da tela
  if (pixel_x >= priv->width)  pixel_x = 0;
  if (pixel_y >= priv->height) pixel_y = 0;

  // 4. Salva com segurança no cursor do sistema
  priv->cursor->x = pixel_x;
  priv->cursor->y = pixel_y;
}

static uint8_t getTextSize(void) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  
  return priv->font.size;
}
void set_blink_interval(uint16_t interval)
{
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  priv->cursor->blink_interval = interval;
}
static uint16_t get_blink_interval(void){
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  return priv->cursor->blink_interval;
}
/* 1 = o 68000 mandeu esconder o cursor (CMD_SET_CUR_OFF). Enquanto estiver 1, nenhuma outra rotina
   (WRITE_SCREEN, scroll, clrscr...) consegue religar o cursor com setTextCursorVisible(true). */
volatile bool cursor_forced_off = false;

static void setTextCursorVisible(bool v) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  priv->cursor->visible = v && !cursor_forced_off;
}
static void setTextCursorBlink(bool b) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  priv->cursor->blink = b;
}

static void setTextColor(char c, char b) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  priv->textcolor = c;
  priv->textbgcolor = b;
}

static uint8_t getTextColor(void) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  return priv->textcolor;
}
static inline void u8_to_hex(uint8_t val, char *buf) {
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = hex[(val >> 4) & 0x0F];
    buf[1] = hex[val & 0x0F];
    buf[2] = '\0';
}

static void printString(char* str){
    vga->setTextCursorVisible(false);    
    while (*str){
        tft_write(*str++);
    }
    vga->setTextCursorVisible(true);    
}

static void printString1(char* str,int32_t num){
  uint8_t buf[8+1];
  while (*str){
      tft_write(*str++);
  }
  //sprintf(buf,"%x",num);
  u8_to_hex(num,buf);
  uint8_t size = sizeof(buf);
  buf[--size]='\0';
  uint8_t i=0;
  while(buf[i]){
    tft_write(buf[i++]);
  }
  tft_write('\n');
}
static void printString2(char* str,int32_t num){
  uint8_t buf[8+1];
  while (*str){
      tft_write(*str++);
  }
//  sprintf(buf,"%x",num);
  u8_to_hex(num,buf);
  uint8_t size = sizeof(buf);
  buf[--size]='\0';
  uint8_t i=0;
  while(buf[i]){
    tft_write(buf[i++]);
  }
}


static void setTextColorBig(color_t color, char background) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  priv->textcolor = color;
  priv->textbgcolor = background;
}

static void setTextFont(font_t *font) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;

  priv->font.name = font->name;
  priv->font.width = font->width ;
  priv->font.height= font->height ;
  priv->font.size = font->size ;   
  priv->font.data = font->data ;


}

short readPixel(short x, short y) {
  vga_text_private_t* priv = (vga_text_private_t*)vga->_private;
  int pixel = ((640 * y) + x) ;
  short color ;
  if (pixel & 1) {
      color = priv->vga_data_array[pixel>>1] >> 4 ;
  }
  else {
      color = priv->vga_data_array[pixel>>1] & 0xf  ;
  }
  return color ;
}
//extern void __not_in_flash_func(vga_line_handler)();
////ESTA FUNÇÃO ESTÁ OK PARA 320 TESTADO 26/03
//void init_vga_320x200(){
//  // 1. Habilita a interrupção específica do PIO0
//  pio_set_irq0_source_enabled(pio0, pis_interrupt1, true);
//  // 2. Diz qual função deve rodar
//  irq_set_exclusive_handler(PIO0_IRQ_0, vga_line_handler);
//  // 3. Liga a interrupção no nível do processador
//  irq_set_enabled(PIO0_IRQ_0, true);
//  irq_set_priority(PIO0_IRQ_0, 0);  
//}

//extern char *active_buffer;
#define TXCOUNT 153600 // Total pixels/2 (since we have 2 pixels per byte)
char vga_video_data_array0[TXCOUNT];
//char vga_video_data_array1[TXCOUNT];
char *active_buffer = (char *)&vga_video_data_array0[0];
unsigned char buffer=0;
font_t *font;


//ESTA FUNÇÃO ESTÁ OK PARA 320 TESTADO 26/03
vga_t* create_screen(screenMode_t mode){ //,uint8_t active_buffer1[],uint32_t txcount,font_t * font1){
  vga = (vga_t*)malloc(sizeof(vga_t));
  vga_text_private_t* priv = (vga_text_private_t*)malloc(sizeof(vga_text_private_t));
  if (!vga || !priv) {
      free(vga);
      free(priv);
      return NULL;
  }
  memcpy_dma_chan = dma_claim_unused_channel(true);
  font = set_font(FONTE_8X16_2);
    // Initialize the VGA screen
  initVGA(  &active_buffer, TXCOUNT , mode) ;

  //if( mode == MODE_TEXT_40_S ||
  //    mode == MODE_TEXT_40_F ||
  //    mode == MODE_320x240
  //  ){
  //  init_vga_320x200();
  //}
  vga->_private = priv;
  
  priv->cursor = create_default_cursor() ;  
  priv->textcolor = WHITE ;
  priv->textbgcolor = BLACK ;
  priv->font.name = font->name;
  priv->font.width = font->width ;
  priv->font.height= font->height ;
  priv->font.size = font->size ;   
  priv->font.data = font->data ;
  priv->tabspace = 4;
  priv->txcount = TXCOUNT ;
  priv->topmask = 0b00001111 ;
  priv->bottommask = 0b11110000 ;
  priv->vga_data_array = &active_buffer[0] ;

  set_vga_mode(mode);

  vga->setTextFont = setTextFont;
  vga->printString = printString;
  vga->setTextColor = setTextColor;
  vga->getTextColor = getTextColor;
  vga->getTextSize = getTextSize;
  vga->setTextCursorPos = setTextCursor;
  vga->clrscr = clrscr;
  vga->setTextCursorVisible = setTextCursorVisible;
  vga->setTextCursorBlink = setTextCursorBlink;
  vga->get_blink_interval = get_blink_interval;
  vga->set_blink_interval = set_blink_interval;
  vga->set_vga_home = set_vga_home;
  vga->set_vga_mode = set_vga_mode;
  vga->pchar = tft_write;
  vga->set_vga_data_array = set_vga_data_array;
  vga->printString1 = printString1;
  vga->printString2 = printString2;
  vga->inc_cursor_y = inc_cursor_y;
  vga->inc_cursor_x = inc_cursor_x;
  vga->dec_cursor_y = dec_cursor_y;
  vga->dec_cursor_x = dec_cursor_x;

  initialize_graphos();

  return vga;
}

#include "eeprom.h"
#include "hardware/resets.h"
extern sys_config_t vga_nvc_config;

void change_mode(screenMode_t mode) {
    
//  pio_clear_instruction_memory(pio0);
//  pio_clear_instruction_memory(pio1);  

  eeprom_load_config(&vga_nvc_config);
  char buf[64];
  sprintf(buf,"Antes da configuracao: [%d]\n",(int8_t)vga_nvc_config.video_mode);
  vga->printString(buf);
  vga_nvc_config.video_mode = mode;  
  eeprom_save_config(&vga_nvc_config);
  sprintf(buf,"Depois da configuracao:[%d]\n",(int8_t)vga_nvc_config.video_mode);
  vga->printString(buf);

  // 1. Coloca o PIO0 e o DMA em estado de reset (limpa FIFOs e máquinas de estado)
  reset_block(RESETS_RESET_PIO0_BITS | RESETS_RESET_DMA_BITS);

  // 2. Tira do reset e espera o hardware confirmar que está pronto
  unreset_block_wait(RESETS_RESET_PIO0_BITS | RESETS_RESET_DMA_BITS);

  reset();

}