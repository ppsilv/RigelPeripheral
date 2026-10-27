#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <util/delay.h>

/*
  Port ATmega8 -> ATmega328P
  minipro -p ATMEGA328P@DIP28 -w interruptCntrl.ino.with_bootloader.bin

  VERSAO COM /VPA (IACK):
    O IPL fica ativo ate a CPU reconhecer a interrupcao (ciclo de IACK = /VPA em 0).
    Nesse momento o ATmega solta o IPL. Assim a ISR do 68000 nao reentra,
    por mais rapida ou lenta que ela seja.

    /VPA (saida do 74HC139, pino 9) -> PB2 (pino 16 do DIP28)
*/

//When cpu say FC0,FC1 and FC2  = 1,1,1 but no interrupt active.
#define CPU_INT_ACK_WRONG     0x80
//When arrive a irq resquest but we have one active irq in curse
#define IRQ_REQUEST_LOST      0x81
//ACK sem interrupção
#define ACK_WITHOUT_INT       0x82
//Irq <level> must be released
#define IRQ_MUST_BE_RELEASED  0x83

// Definições dos pinos IPL no PORTC
#define IPL0  PC0
#define IPL1  PC1
#define IPL2  PC2
#define IPL_MASK ((1 << IPL0) | (1 << IPL1) | (1 << IPL2))

// Definições dos pinos de IRQ no PORTD
#define IRQ1_PIN  PD2
#define IRQ2_PIN  PD3
#define IRQ3_PIN  PD4
#define IRQ4_PIN  PD5
#define IRQ5_PIN  PD6
#define IRQ7_PIN  PD7
#define ALL_IRQ_MASK ((1<<IRQ1_PIN)|(1<<IRQ2_PIN)|(1<<IRQ3_PIN)|(1<<IRQ4_PIN)|(1<<IRQ5_PIN)|(1<<IRQ7_PIN))

// /VPA vindo do 74HC139 (ativo em 0). PB2 = PCINT2 (pino 16 do DIP28)
#define VPA_PIN   PB2

/*
  IRQs que sao "de borda": a fonte nao tem como ser limpa pela ISR do 68000
  (o systick e' um sinal quadrado de 5 ms em 0 / 5 ms em 1, gerado pelo OC1A).
  Depois do IACK essas fontes ficam "servidas" e so' voltam a disparar depois
  que o pino voltar ao nivel inativo (1).
  As outras (teclado, etc.) continuam por NIVEL: o IPL volta se a fonte ainda estiver ativa.
  Para tornar outra IRQ de borda, some o bit dela aqui.
*/
#define EDGE_IRQS  (1 << IRQ2_PIN)

#define TIMEOUT  1000
long timer1=millis()+100;
long timer2=millis()+TIMEOUT;

#define NOT_ACTIVE 7
unsigned char irq_s[8]={NOT_ACTIVE,NOT_ACTIVE,NOT_ACTIVE,NOT_ACTIVE,NOT_ACTIVE,NOT_ACTIVE,NOT_ACTIVE,NOT_ACTIVE};
// Variáveis globais
volatile uint8_t active_irq = NOT_ACTIVE;
volatile uint8_t served = 0;     // bits (posicao do PORTD) das IRQs de borda ja reconhecidas pela CPU

/*
  Valor que esta nos pinos IPL (logica invertida) -> bit da IRQ no PORTD.
  codigo 0 = nivel 7, 2 = nivel 5, 3 = nivel 4, 4 = nivel 3, 5 = nivel 2, 6 = nivel 1, 7 = nenhuma
*/
static const uint8_t irq_mask_by_code[8] = {
  (1 << IRQ7_PIN), 0, (1 << IRQ5_PIN), (1 << IRQ4_PIN),
  (1 << IRQ3_PIN), (1 << IRQ2_PIN), (1 << IRQ1_PIN), 0
};

#define LED_PIN PB5  // LED no PB5 (D13 no Arduino Uno)
#define LED_PORT PORTB

// Configuração dos ports
void setup_ports() {
    // Configurar PC0-PC2 como saídas (IPL0-IPL2)
    DDRC |= (1 << IPL0) | (1 << IPL1) | (1 << IPL2);
    // IPL inicialmente inativo (todos em 1)
    PORTC |= (1 << IPL0) | (1 << IPL1) | (1 << IPL2);

    // Configurar PB1 como saída (OC1A - Debug) e PB5 (LED)
    DDRB |= (1 << PB1) | (1 << PB5);

    // PB2 = /VPA: entrada com pull-up (sem fio ligado fica em 1, sem interrupcao falsa)
    DDRB  &= ~(1 << VPA_PIN);
    PORTB |=  (1 << VPA_PIN);

    // Configura PD2-PD7 como entradas com pull-up
    DDRD &= ~ALL_IRQ_MASK;
    PORTD |= ALL_IRQ_MASK;
}

/*
  Gerando o sinal de 100HZ no pino PB1 (OC1A)
  Usado para gerar interrupção 2 no 68000 (fio de PB1 para PD3)
  16MHz / 64 = 250kHz; OCR1A=1249 -> 1250 contagens = 5ms
  Toggle a cada 5ms -> período de 10ms -> 100Hz
*/
void setup_timer1() {
    TCCR1A = (1 << COM1A0);                              // Toggle OC1A on compare match
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);   // CTC mode, prescaler 64
    OCR1A = 1249;
    // OCIE1A desligado: o toggle do OC1A e' feito pelo hardware, nao precisa de ISR
    // (e a ISR so' atrasaria o PCINT do /VPA)
}
//Dummy counter, not used
volatile uint32_t ms_counter = 0;
ISR(TIMER1_COMPA_vect) {
  ms_counter++;
}

//Timer 2 not used
void setup_timer2() {
    TCCR2A = (1 << WGM21);             // CTC mode
    OCR2A = 99;                        // 50us @ 16MHz/8 (2MHz -> 100 contagens)
    TCCR2B = (1 << CS21);              // Prescaler 8
    TIMSK2 |= (1 << OCIE2A);
}
ISR(TIMER2_COMPA_vect) {

}

/*
  /VPA em PB2 -> pin change interrupt (PCINT2, vetor PCINT0_vect).
  O PCINT dispara nas duas bordas e o ATmega guarda o pedido no flag PCIF0 mesmo
  que a ISR demore um pouco, entao o pulso de /VPA (~1 us) nao se perde.
*/
void setup_vpa_interrupt() {
    PCMSK0 = (1 << PCINT2);       // so' PB2 (PB1/OC1A e PB5/LED nao disparam)
    PCIFR  = (1 << PCIF0);        // limpa pedido antigo
    PCICR |= (1 << PCIE0);        // habilita o grupo PCINT0..7
}

ISR(PCINT0_vect) {
    // Chegou um ciclo de IACK: a CPU aceitou a interrupcao que esta nos pinos IPL.
    uint8_t code = PORTC & 0x07;
    if (code != NOT_ACTIVE) {
        served |= (irq_mask_by_code[code] & EDGE_IRQS);   // so' marca as IRQs de borda
        PORTC = (PORTC & 0xF8) | NOT_ACTIVE;              // solta o IPL
    }
    // Espera o /VPA terminar (limite ~60 us) e descarta a borda de subida,
    // para a ISR nao ser chamada de novo pelo fim do mesmo pulso.
    uint8_t t = 200;
    while (!(PINB & (1 << VPA_PIN)) && --t) { }
    PCIFR = (1 << PCIF0);
}

//int0 not used
void setup_fc_interrupt() {
    EICRA |= (1 << ISC01);
    EICRA &= ~(1 << ISC00);
    EIMSK |= (1 << INT0);
}
ISR(INT0_vect) {

}

uint8_t check_irq_pins_status() {
    return (~PIND) & ALL_IRQ_MASK;
}
/*
  To start:
          int7 send 0
          int6 send 1
          int5 send 2
          int4 send 3
          int3 send 4
          int2 send 5
          int1 send 6
       no int  send 7
*/
uint8_t get_irq_code(uint8_t status) {
    if (status & (1<<IRQ7_PIN)) return 0;
    if (status & (1<<IRQ5_PIN)) return 2;
    if (status & (1<<IRQ4_PIN)) return 3;
    if (status & (1<<IRQ3_PIN)) return 4;
    if (status & (1<<IRQ2_PIN)) return 5;
    if (status & (1<<IRQ1_PIN)) return 6;
    return NOT_ACTIVE;
}

uint8_t get_irq_started() {
    return get_irq_code(check_irq_pins_status());
}

void setup(){
  Serial.begin(115200);
  Serial.println("Teste do gerenciador de interrupção 2.0 (IACK via /VPA em PB2)");
  setup_ports();

  //Setup interrups
  setup_timer1();
  setup_vpa_interrupt();

  sei();
  pinMode(LED_BUILTIN, OUTPUT);
  timer1=millis()+TIMEOUT;
  delay(0.100); // 100 mili segundos
}

void loop() {
  while (1) {
    /*
      Le os pinos, calcula e escreve o IPL tudo junto (atomico): se o /VPA chegasse
      no meio, a ISR soltaria o IPL e este laco o colocaria de volta com o valor velho.
    */
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        uint8_t raw = check_irq_pins_status();
        served &= raw;                       // fonte voltou a inativa: pode disparar de novo
        PORTC = (PORTC & 0xF8) | get_irq_code(raw & (uint8_t)~served);
    }

    if (millis() > timer1) {
      timer1 = millis() + 500;
      LED_PORT ^= (1 << LED_PIN);
    }
  }
}
