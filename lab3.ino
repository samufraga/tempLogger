#include <LiquidCrystal.h>
#include <Wire.h>

#define dispCA1 13

volatile unsigned char t0Count = 0;
volatile unsigned char t1Count = 0;
volatile unsigned char dispSelect = 0;
volatile unsigned char dispSwitch = 1;
byte dispEnMask;
byte dispConvert[] = {B10000001, B11111001, B10001010, B11001000, B11110000, B11000100, B10000100, B11101001, B10000000, B11000000};

unsigned char colPins[3] = {11, 12, 10}; //pinos digitais ligados às colunas 1, 2 e 3 do teclado
unsigned char rowPins[4] = {9, 8, 0, 1}; //pinos digitais ligados às linhas 1, 2, 3 e 4 do teclado
unsigned char keyMatrix[4][3] = {{'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'#', '0', '*'}};

char command;
char quantityWait;
char quantityCount;
char statusUpdate = 0;
String quantityStr;

volatile char loggerEn = 0;
volatile char storeData = 0;
volatile unsigned int temperature = 0;

LiquidCrystal lcd(7, 6, 5, 4, 3, 2);

void setup(){
    //configura interrupções
    cli(); 
    set_Timer();
    sei();

    //pinos de saída display 7 seg.
    DDRC |= B00001110;
    pinMode(13, OUTPUT);

    //pino sensor de temperatura
    pinMode(A0, INPUT);

    //configura pinos de saída/entrada para teclado matricial
    for (int i = 0; i <= 3; i++){
        pinMode(rowPins[i], INPUT); //coloca as linhas em alta impedância
        digitalWrite(rowPins[i], LOW);
    }
    for (int i = 0; i <= 2; i++){
        pinMode(colPins[i], INPUT_PULLUP);
    }
    
    lcd.begin(16, 2);
    lcd.clear();
    lcd.home();
    Wire.begin();
}

void loop(){
    //lê teclado matricial
    char key = keypadScan();

    //identifica que alguma tecla foi pressionada
    if (key != 'z'){
        //se está aguardando digitar quantidade de dados, armazena os dígitos a cada loop
        //até receber 4 dígitos ou #, mostrando no LCD os números inseridos.
        if (quantityWait){
            switch (key){
                case '*':
                    quantityWait = 0;
                    quantityCount = 0;
                    commandDecode('*');
                    break;
                case '#':
                    transferData(quantityStr.toInt());
                    break;
                default:
                    quantityStr.concat(key);
                    lcd.setCursor(quantityCount+10, 1);
                    lcd.print(key);
                    quantityCount++;
                    if(quantityCount==4){
                        transferData(quantityStr.toInt());
                    }
                    break;
            }
        //se não, decodifica commando digitado
        }else{
            commandDecode(key);
        }
    }

    //grava temperatura na EEPROM, atualizando status no LCD se o comando foi dado anteriormente
    if(loggerEn && storeData){
        word pointer = eepromGetPointer();  //lê endereço a ser gravado do final da EEPROM
        eepromWrite(pointer>>8 | B01010000, pointer & 0xFF, temperature); //grava temperatura na EEPROM
        if (pointer < 2046){
            pointer += 2; //incrementa endereço de gravação
        }
        eepromSetPointer(pointer); //escreve endereço de gravação na EEPROM
        storeData = 0;
        //atualiza status no LCD
        if (command == 2 && statusUpdate){
            int ndata = eepromGetPointer()/2;
            printLCD("Gravados: "+String(ndata)+"     ", "Disponivel: "+String(1022-ndata)+"   ");
        }
    }
    

    // troca display de 7 segmentos ativo
    if (dispSwitch){
        dispSwitch = 0;
        char buffer[4];
        sprintf(buffer, "%04d", temperature);
        Wire.beginTransmission(32);
        byte pcfMsg = dispConvert[buffer[dispSelect]-48];
        if (dispSelect == 2){
            pcfMsg &= B01111111; //acende apenas DP3
        }
        Wire.write(pcfMsg);
        Wire.endTransmission();
        if (dispSelect == 0){
            digitalWrite(dispCA1, HIGH);
            PORTC = (PORTC & B11110001);
        }else{
            dispEnMask = 1 << dispSelect;
            PORTC = (PORTC & B11110001) | dispEnMask;
            digitalWrite(dispCA1, LOW);
        }
    }
}

//função de varredura do teclado matricial: para cada linha, configura o respectivo pino como saída,
//o qual passa de alta impedância para nível 0, entao lê cada coluna e no final retorna pino para alta impedância.
//Se alguma tecla foi pressionada, aguarda tempo de bouncing e espera soltar a tecla, retornando o respectivo caracter
char keypadScan(){
    for (int i = 0; i <= 3; i++){
        pinMode(rowPins[i], OUTPUT);
        for (int j = 0; j <= 2; j++){
            if(digitalRead(colPins[j]) == 0){
                delay(5);
                if(digitalRead(colPins[j]) == 0){
                    while (digitalRead(colPins[j]) == 0){
                        ; //espera soltar tecla
                    }
                    delay(5); 
                    pinMode(rowPins[i], INPUT);
                    return keyMatrix[i][j];
                }
            }
        }
        pinMode(rowPins[i], INPUT);
    }
    return 'z';
}

//decodifação das teclas. Se for tecla de comando, registra comando e mostra requisição de confirmação no LCD.
//Se for "#", executa comando registrado, se "*" cancela.
void commandDecode(char key){
    switch (key){
        case '1':
            command = 1;
            printLCD("Apagar memoria ?", "#sim    *cancela");
            break;
        case '2':
            command = 2;
            printLCD("Mostrar status ?", "#sim    *cancela");
            break;
        case '3':
            command = 3;
            printLCD("Iniciar coleta ?", "#sim    *cancela");
            break;
        case '4':
            command = 4;
            printLCD(" Parar coleta ? ", "#sim    *cancela");
            break;
        case '5':
            command = 5;
            printLCD("Transferir dados", "#sim    *cancela");
            break;
        case '#':
            commandExec();
            break;
        case '*':
            lcd.clear();
            command = -1;
            break;
        default:
            break;
    }
}

//execução dos comandos. Se for 5, solicita entrada da quantidade de medidas, se não executa comando
//e mostra confirmação no LCD.
void commandExec(){
    switch (command){
        case 1:
            printLCD("Memoria  apagada", "                ");
            eepromSetPointer(0); //resetar memoria
            break;
        case 2:{
            //mostra numero de dados gravados;
            int ndata = eepromGetPointer()/2;
            printLCD("Gravados: "+String(ndata)+"     ", "Disponivel: "+String(1022-ndata)+"   ");
            statusUpdate = 1;
            break;
        }
        case 3:
            printLCD("Coleta de dados ", "    iniciada    ");
            loggerEn = 1;
            break;
        case 4:
            printLCD("Coleta de dados ", "   finalizada   ");
            loggerEn = 0;
            break;
        case 5:
            printLCD(" Quantidade  de ", " medidas:       ");
            quantityStr = "";
            quantityWait = 1;
            quantityCount = 0;
            break;
        default:
            break;
    }
}

//escreve texto em todo o LCD.
void printLCD(String row0, String row1){
    lcd.home();
    lcd.print(row0);
    lcd.setCursor(0, 1);
    lcd.print(row1);
}

//execução do comando 5. Verifica se a quantidade de medidas solicitadas é válida e transfere a quantidade desejada
//ou mostra mensagem de erro no LCD. 
void transferData(int quantity){
    if (quantity > 0 && quantity < 1023){
        Serial.begin(9600);
        for(int i=0; i<2*quantity; i+=2){
            Serial.println(eepromRead(i>>8 | B01010000, i & 0xFF)/10);
        }
        Serial.end();
        printLCD(quantityStr+" medida(s)     ", "transferida(s)  ");
    }else{
        printLCD("   Quantidade   ", "    invalida    ");
    }
    quantityWait = 0;
}


void set_Timer(){
    //configura timer0 com 4ms entre interrupções para troca do display de 7 seg
    TCCR0A = 0x02;
    OCR0A = 64;
    TIMSK0 = 0x02;
    TCCR0B = 0x05;
    
    //configura timer1 com 100ms entre interrupções para calculo da velocidade
    TCCR1A = 0;
    OCR1A = 0x061A;
    TIMSK1 = 0x02;
    TCCR1B = 13;
}

//interrupção timer0 seleciona qual display de 7 seg deve acender e levanta flag para realizar a troca no loop principal
ISR(TIMER0_COMPA_vect){
    //t0Count++;
    dispSelect++;   
    if (dispSelect >= 4){
        dispSelect = 0;
    }
    dispSwitch = 1;
}

//interrupção do timer1 conta intervalos de 2 segundoS e registra temperatura
ISR(TIMER1_COMPA_vect){
    t1Count++;
    if (t1Count >= 20){
        t1Count = 0;
        temperature = analogRead(0)*4.9 - 2; //T = read*4.9mV/10mV*10 (resoluacao do AD/ganho do sensor)
        storeData = 1;
    }
}

//escreve 2 bytes de dados consecutivos na EEPROM
void eepromWrite(byte deviceAddr, byte wordAddr, word data) {
  Wire.beginTransmission(deviceAddr);
  Wire.write(wordAddr);
  Wire.write(data>>8 & 0xFF);
  Wire.write(data & 0xFF);
  Wire.endTransmission();
  delay(3);
}

//escreve bytes nos endereços x[2046] e x[2047] da EEPROM para armazenar o número de registros
void eepromSetPointer(word dataAddr){
  Wire.beginTransmission(B01010111);
  Wire.write(B11111110);
  Wire.write(dataAddr>>8 & 0xFF); //MSB
  Wire.write(dataAddr & 0xFF); //LSB
  Wire.endTransmission();
  delay(3);
}

//lê 2 bytes consecutivos da EEPROM
word eepromRead(byte deviceAddr, byte wordAddr) {
  word data;
  Wire.beginTransmission(deviceAddr);
  Wire.write(wordAddr);
  Wire.endTransmission();
  Wire.requestFrom(deviceAddr, 2);
  if(Wire.available()){
    data = Wire.read()<<8;
    if(Wire.available()){
      data |= Wire.read();
      return data;
    }
  }
  return 0xFFFF;
}

//lê 2 bytes dos endereços x[2046] e x[2047] da EEPROM para obter o próximo endereço disponível
word eepromGetPointer() {
  word addr;
  Wire.beginTransmission(B01010111);
  Wire.write(B11111110);
  Wire.endTransmission();
  Wire.requestFrom(B01010111, 2);
  if(Wire.available()){
    addr = Wire.read()<<8;
    if(Wire.available()){
      addr |= Wire.read();
      return addr;
    }
  }
  return 0xFFFF;
}