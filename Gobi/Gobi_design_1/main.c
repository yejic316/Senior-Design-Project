#include <msp430.h>
#include <msp430x26x.h>
#include <stdlib.h>   // atoi()
#include <stdio.h>    // sprintf()
#include <string.h>
#include <math.h>

#define UART_PRINTF

// Function prototypes
void InitSystem(void);
void InitTimers(void);
void InitDMA(void);
void InitADC(void);
void InitDAC(void);
void InitUART(void); // added UART Initialization
void Data_Process(void);
void UART_Data_Out(void);
void read_pin(void);

// Constants
#define ADCRATE 64
#define DEBUG 0
#define NUMOFRESULTS 1280
#define PORTFLAG BIT3
#define ISRFLG_DMA_BIT BIT2

// Global Variables
int sysMode = 0;
int ISRFLAG;

volatile unsigned int buffer0[NUMOFRESULTS];
volatile unsigned int buffer1[NUMOFRESULTS];
volatile unsigned int buffer2[NUMOFRESULTS];

/***************************************************************************************
 * Function: main()                                                                    *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Main program where the intializing                                             *
 *      functions are called from to setup system.                                     *
 *      In a while loop that is always true, will execute                              *
 *      the main functionality of system. Will change modes based on input             *
 *      from P4.1 and P4.2                                                            *
 ***************************************************************************************/
int main(void) {
    int i = 0;
    // call setup functions
    InitSystem();
    InitTimers();
    InitADC();
    InitDAC();
    InitDMA();
    InitUART();

    while (1) {
        read_pin();
        switch (sysMode) {
        case 0:                     // Standby Mode
            P4OUT = 0x01;           // LED3 ON
            ADC12CTL0 &= ~ENC;
            DAC12_1DAT &= 0x0000;
            DAC12_0DAT &= 0x0000;
            break;
        case 1:                    // Collect Data Mode
            ADC12CTL0 |= ENC;
            P4OUT = 0x02;           // LED4 ON

            //re-configuring DMA1 in case of re-sampling signals.
            DMA1SA = (void (*)()) &buffer0;
            DMA1DA = (void (*)()) &DAC12_1DAT;
            break;
        case 2:
            ADC12CTL0 |= ENC;      // Procesing Data
            P4OUT = 0x04;           // LED5 ON

            //because summation using updated buffer0 and buffer1 (it supposes to use previous buffer0 and buffer1)
            DMA2CTL&=~DMAEN;
            DMA0CTL&=~DMAEN;
            Data_Process();
            break;
        case 3:
            ADC12CTL0 |= ENC;      // Send to UART
            P4OUT = 0x08;          // LED6 ON
            UART_Data_Out();
            break;
        default:                   // Standby mode
            P4OUT = 0x00;
            ADC12CTL0 &= ~ENC;
            DAC12_1DAT &= 0x0000;
            DAC12_0DAT &= 0x0000;
            break;
        }
    }
}

//************************** Functions defined by prototypes ***************************
/***************************************************************************************
 * Function: InitSystem()                                                                 *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will Initialize the system clock,
 *      ports, and timers to be used in
 *      the system.                                                    *
 ***************************************************************************************/
void InitSystem(void) {
    unsigned int i;

    WDTCTL = WDTPW | WDTHOLD;       // stop watchdog timer
    BCSCTL1 = CALBC1_8MHZ;         // Set DCO to 8MHz
    DCOCTL = CALDCO_8MHZ;
    BCSCTL2 = 0;                  // SMCLK = MCLK = DCOCLK / 1

    for (i = 0xfffe; i > 0; i--)
        ;             // Delay for XTAL stabilization

    // PxDIR:
    // BIT = 0 -> input
    // BIT = 1 -> Output
    P4DIR |= 0x0F;      // Sets PINS 4.4 and 4.5 as inputs & 4.0-4.3 as output
    P6SEL = BIT1 + BIT2;           // Enable A/D on P6.1 and P6.2

    P4REN |= 0x30;

    // Clear Timer A and B registers
    TACCTL0 = 0x00;                // TACCR1 toggle, interrupt enabled
    TACCTL1 = 0x00;
    TACCTL2 = 0x00;
    TBCCTL0 = 0x00;
    TBCCTL1 = 0x00;
    TBCCTL2 = 0x00;

    P4OUT = 0x00;
}


/***************************************************************************************
 * Function: InitTimers()                                                                 *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will Initialize Timers provided
 *      by MSP430 that will be used
 *      in this system. Timer B is used
 *      to toggle every half of ADCRATE(64)                                         *
 ***************************************************************************************/
void InitTimers(void) {
    // TIMER A
    TACTL = 0;
    TACTL = TASSEL_2 + TACLR + ID_3 + MC_1;        // SMCLK/8, clear TAR
    TACCTL0 &= ~CCIE;                               // CCR0 interrupt disabled
    TACCR0 = 50000;                  // max clock count for compare/capture mode

    TBCTL = TBSSEL_2 + MC_1 + TBCLR;
    TBCCTL1 = OUTMOD_2;       // Toggle at TBCCR1 and Reset at TBCCR0
    TBCCR0 = ADCRATE;        // Timer B max count is ADCRATE
    TBCCR1 = (ADCRATE >> 1); // Timer B will toggle every half of ADCRATE
}

/***************************************************************************************
 * Function: InitADC()                                                                 *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will Initialize the ADC module
 *      to be used in this system.
 *      Two ADC channels will be used
 *      for converting two analog signals.
 *      Will sample at 235KHz at 12-bit
 *      resolution.                                                 *
 ***************************************************************************************/
void InitADC(void) {
    // Sample and hole time = 4 ADC12CLk cycles
    // Reference voltage on = 2.5V
    // Multiple sample and conversion Mode: one trigger to start, rest auto
    // ADC is on
    ADC12CTL0 = REFON + ADC12ON + REF2_5V + MSC;

    // Pulse mode selected
    // Repeat sequence channel mode
    // ADC12 Clock divider: 1
    // ADC12 clock source: SMCLK
    // Sample and hold source: TimerB1 out
    ADC12CTL1 = SHP + CONSEQ_3 + ADC12SSEL_3 + SHS_3;

    // Channel A2(PIN 4) analog input
    // Selected reference voltage: VR+ = VREF+, VR- = AVSS
    // THis channel is last conversion in the sequence
    ADC12MCTL0 = INCH_2 + SREF_1;

    // Channel A1(PIN3)
    ADC12MCTL1 = INCH_1 + SREF_1 + EOS;
}

/***************************************************************************************
 * Function: InitDAC()                                                                 *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will Initialize the DAC to
 *      be used in the system. Two
 *      channels of the DAC will be
 *      used to output analog signal.
 *      Will be set to convert whenever
 *      its data register is written to.                                                    *
 ***************************************************************************************/
void InitDAC(void) {
    // outputs at reference voltage
    // Low speed/ current input and low speed/current output
    // converts when DAC12_0DAT and DAC12_1DAT are written to
    DAC12_1CTL = DAC12IR + DAC12AMP_2 + DAC12OPS;
    DAC12_0CTL = DAC12IR + DAC12AMP_2;
}

/***************************************************************************************
 * Function: InitDMA()                                                                 *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will Initialize the DMA register to                                            *
 *      transfer data from ADC to Buffer0[]                                            *
 *      and from Buffer0[] to DAC. Also, will                                          *
 *      carry the final signal to the DAC0 to                                          *
 *      output to final test point.                                                    *
 ***************************************************************************************/
void InitDMA(void) {
    DMACTL0 = DMA0TSEL_6 + DMA1TSEL_6 + DMA2TSEL_6;

    // DMA0
    DMA0SA = (void (*)()) &ADC12MEM0;
    DMA0DA = (void (*)()) &buffer0;
    DMA0SZ = NUMOFRESULTS;

    DMA0CTL = DMADSTINCR_3 + DMADT_4 + DMAEN;

    // DMA1
    DMA1SA = (void (*)()) &buffer0;
    DMA1DA = (void (*)()) &DAC12_1DAT;
    DMA1SZ = NUMOFRESULTS;

    DMA1CTL = DMASRCINCR_3 + DMADT_4 + DMAEN;

    //DMA2
    DMA2SA = (void (*)()) &ADC12MEM1;
    DMA2DA = (void (*)()) &buffer1;
    DMA2SZ = NUMOFRESULTS;

    DMA2CTL = DMADSTINCR_3 + DMADT_4 + DMAEN;
}

/***************************************************************************************
 * Function: InitUART()                                                                 *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will Initialize the UART registers to                                            *
 *      transfer data to computer via serial port.
 *                                                  *
 ***************************************************************************************/
void InitUART(void) {
    P3SEL = 0xC0;                             // P3.6,7 = USCI_A1 TXD/RXD
    UCA1CTL1 |= UCSSEL_2;                     // SMCLK
    UCA1BR0 = 69;                              // 8MHz 115200
    UCA1BR1 = 0;                              // 8MHz 115200
    UCA1MCTL = UCBRS2;                        // Modulation UCBRSx = 4 for 8MHz
    UCA1CTL1 &= ~UCSWRST;                   // **Initialize USCI state machine**
    UC1IE |= UCA1RXIE;                          // Enable USCI_A0 RX interrupt
}

/***************************************************************************************
 * Function: Data_Process()                                                                 *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will sum two digital signals and store into buffer[2]
 *
 ***************************************************************************************/
void Data_Process(void) {
    unsigned int i;
    DMA1SA = (void (*)()) &buffer2;
    DMA1DA = (void (*)()) &DAC12_0DAT;

    for (i = 0; i < NUMOFRESULTS; i++) {
        //-2300 to match DC offset of final output and input signal (buffer0)
        buffer2[i] = buffer0[i] - buffer1[i] +2100;
    }
    ADC12CTL0 |= ENC;
}

/***************************************************************************************
 * Function: UART_Data_Out()                                                           *
 * Input Parameters: NONE                                                              *
 * Output: NONE                                                                        *
 * Description:                                                                        *
 *      Will transmit a digital signal to USB via UART module to
 *      be graphed using Python
 ***************************************************************************************/
void UART_Data_Out(void) {
    unsigned int i = 0;
    for (i = 0; i < NUMOFRESULTS; i++)
    {
        //printf("%u,%u\n", buffer0[i], buffer2[i]);
        printf("%u\r\n",  buffer2[i]);
    }
}

void read_pin(void) {
    int k = 0;
    unsigned SW1;
    unsigned SW2;
    unsigned temp1;
    unsigned temp2;

    P4IN = 0;

    SW1 = P4IN & 0x10;
    SW2 = P4IN & 0x20;
    for (k = 1600000; k > 0; k--)
        ;
    temp1 = P4IN & 0x10;
    temp2 = P4IN & 0x20;


    if (SW1 == temp1 && SW2 == temp2)
    {
        if(SW1 == 0x10 & SW2 == 0)
            sysMode = 1;
        else if (SW2 == 0x20 && SW1 == 0)
            sysMode = 2;
        else if (SW1 == 0x10 && SW2 == 0x20)
            sysMode = 3;
        else
            sysMode = 0;
    }


    return;
}
//****************************************************************************************
#ifdef UART_PRINTF
//@brief Will send a character over UART
int fputc(int _c, register FILE *_fp) {
    // hold until register UC1IFG matches
    // UCA1TXIFG register
;   while (!(UC1IFG & UCA1TXIFG))
        ;

    // Add single integer as unsigned char
    //to Transmission Buffer
    UCA1TXBUF = (unsigned char) _c;

    // return int value as unsigned char
    return (unsigned char) _c;
}

// @brief will send a string over UART
int fputs(const char *_ptr, register FILE *_fp) {
    unsigned int i, len;

    // Length of string
    len = strlen(_ptr);

    // loop until i equals length of string
    for (i = 0; i < len; i++) {
        // hold until register UC1IFG matches
        // UCA1TXIFG register
        while (!(UC1IFG & UCA1TXIFG))
            ;

        // add each a single char from string to
        // Transmission buffer
        UCA1TXBUF = (unsigned char) _ptr[i];
    }

    // return length of string
    return (len);
}

#endif
