#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <map>  // Cache de IDs do SD
#include <set>
#include <WiFi.h>
#include <time.h>
#include <IPAddress.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define FECHADURA           25 //Atuador
#define SUCESSO             2  //Led buit-in da placa que será usado para sinalizar que o dispositivo esta operacional

// Configuração dos botões
#define SWITCH_MAIN_STATE   33  // Botão para alternar o estado principal
#define SWITCH_LOCAL_STATE  32  // Botão para alternar o estado local dentro do estado principal

// Configuração da máquina de estados
#define NUM_MAIN_STATES     4  //Quantidade de estados principais
#define NUM_LOCAL_STATES    3  //Quantidade de estados locais para cada estado principal

#define STATE_UPDATE        0  //Estado de atualizacao
#define STATE_OPERATION     1  //Estado de operacao
#define STATE_INFO          2  //Estado de visualizacao
#define STATE_ADM           3  //Estado de administrador


// Pinos para o MicroSD no HSPI
#define SS_SD               15
#define SCK_SD              14
#define MISO_SD             12
#define MOSI_SD             13

// Pinos para o RFID-RC522
#define SS_RFID             5
#define SCK_RFID            18
#define MISO_RFID           19
#define MOSI_RFID           23
#define RST_RFID            26
#define INTERVAL_UPDATE     60000

IPAddress server_addr = {192,168,2,100};          //IP do servidor mysql

const String DEVICE_ID = "00001"; // Este eh o codigo deste dispositivo

bool sdDisponivel = false;

String ssid = "";
String senha = "";
String ipLocal = "0.0.0.0";

char logFilename[] = "/log.txt";
char authorizedsFilename[] = "/identidades.txt";
char unsignedFilename[] = "/unsigned.txt";
char admFilename[]="/administrators.txt";
char netFilename[] = "/netconf.txt";

String ntpServer = "pool.ntp.org";  // Valor padrão
long gmtOffset_sec = -10800;        // GMT-3 padrão
int daylightOffset_sec = 0;         // Horário de verão (default = 0)

File myFile;
SPIClass SPI2(HSPI);
SPIClass SPI3(VSPI);
HardwareSerial mySerial(2);  // UART2 (RX2=GPIO16, TX2=GPIO17)
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
LiquidCrystal_I2C lcd(0x27, 20, 4);
MFRC522 rfid(SS_RFID, RST_RFID);
std::map<String, std::pair<String, int>> idCache;  // Mapeia idCartao -> (nome, idFingerPrint)
std::set<String> adminCards;  // Armazena os IDs de administradores
std::map<String, std::pair<String, int>> unsignedCache;  // Mapeia idCard -> (nome, id)

// Controle dos estados
volatile int mainState = 0;
volatile int localState = 0;

// Controle de debounce
volatile unsigned long lastInterruptTimeMain = 0;
volatile unsigned long lastInterruptTimeLocal = 0;
volatile bool proxima = false;
const unsigned long debounceDelay = 300; // 300ms debounce

// Protótipos das funções
void inicializaLCD();
void logMessage(const char* level, const char* message);
void inicializaSensorBiometrico();
void configuraSPI();
void inicializaSDCard();
bool lerParametrosRede();
void conectarWiFi();
void configurarRelogioNTP();
String getTimeStamp();
void inicializaRFID();
void carregarCacheIDs();

void displayMessage(const char *line1, const char *line2 = "", const char *line3 = "", const char *line4 = "", bool clear = true);
int getFingerprintID(int timeout = 5000);
String getIdCard(int estado, const char *line1 = "", const char *line2 = "", const char *line3 = "");
bool autenticacao(int fingerPrintId);
bool identificacao(String idCartao);

void executeMainState();
void executeLocalState();

void mainStateUpdate();
void mainStateOperation();
void mainStateInfo();

void localState0();
void localState1();

void carregarAdministradores();
bool verificaAdministrador(String idCartao);
void mainStateAdm();  // Estado Administrador

// Novos protótipos
void carregarUnsignedCache();  // Carrega os dados do arquivo unsigned  no cache
void enrollFingerprintsFromUnsignedCache();  // Realiza o enroll das digitais a partir do cache
bool enrollFingerprint(String nome,int id);  // Realiza o processo de enroll no sensor biométrico
bool adicionarIdentidadeAoArquivo(String idCard, String nome, int id);
void limparArquivoUnsigned();

//void exibirQuantidadeDigitais();

void atualizarCacheDeAutorizados();
void atualizarCacheDeAdministradores();
void enviarLogsDoSDCard();
bool enviarLogParaServidor(String log);
void atualizarCacheDePendentes();
bool atualizarFingerprint(String idcard, int pessoa_id, String codigo_dispositivo);
bool servidorDisponivel();
void limparDigitaisUnsigned();




// Rotinas de interrupção
void IRAM_ATTR handleMainState() {
    unsigned long currentTime = millis();
    if (currentTime - lastInterruptTimeMain > debounceDelay) {
        mainState = (mainState + 1) % NUM_MAIN_STATES;
        localState = 0;
        lastInterruptTimeMain = currentTime;
    }
}

void IRAM_ATTR handleLocalState() {
    unsigned long currentTime = millis();
    if (currentTime - lastInterruptTimeLocal > debounceDelay) {
        localState = (localState + 1) % NUM_LOCAL_STATES;
        proxima = true;
        lastInterruptTimeLocal = currentTime;
    }
}

void setup() {

    Serial.begin(9600);

    pinMode(FECHADURA, OUTPUT);
    pinMode(SUCESSO, OUTPUT);
    pinMode(SWITCH_MAIN_STATE, INPUT);
    pinMode(SWITCH_LOCAL_STATE, INPUT);

    inicializaLCD();

    configuraSPI();

    inicializaSDCard();
    if (sdDisponivel) {
        logMessage("INFO", "Cartão SD inicializado com sucesso.");
    } else {
        logMessage("ERROR", "Falha ao inicializar cartão SD.");
    }


    // WiFi e NTP
    if (lerParametrosRede()) {
        conectarWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            configurarRelogioNTP();
        }
    } else {
        logMessage("WARNING", "WiFi não configurado no SD. Continuando offline.");
    }

    inicializaRFID();
    logMessage("INFO", "Módulo RFID inicializado.");

    carregarCacheIDs();
    logMessage("INFO", "Cache de IDs carregado.");

    carregarUnsignedCache(); // Carrega o cache de IDs não cadastrados

    carregarAdministradores();
    logMessage("INFO", "Lista de administradores carregada.");

    inicializaSensorBiometrico();
    logMessage("INFO", "Sensor biométrico inicializado.");

    digitalWrite(SUCESSO, HIGH);
    logMessage("INFO", "Sistema pronto.");
    

    attachInterrupt(digitalPinToInterrupt(SWITCH_MAIN_STATE), handleMainState, FALLING);
    attachInterrupt(digitalPinToInterrupt(SWITCH_LOCAL_STATE), handleLocalState, FALLING);    
}

void loop() {  
    static unsigned long lastUpdate = 0;
    const unsigned long updateInterval = INTERVAL_UPDATE; // 3 minutos em milissegundos

    executeMainState();

    if (mainState == 0 && millis() - lastUpdate >= updateInterval) {
        digitalWrite(SUCESSO, LOW);
        if(servidorDisponivel()){
            displayMessage("Atualizando...");
            atualizarCacheDeAutorizados() ;
            atualizarCacheDeAdministradores();
            atualizarCacheDePendentes();
            enviarLogsDoSDCard();
        }
        lastUpdate = millis();
        digitalWrite(SUCESSO, HIGH);
    }
}

// Função para executar o estado principal atual
void executeMainState() {
    switch (mainState) {
        case STATE_UPDATE: mainStateUpdate(); break;
        case STATE_OPERATION: mainStateOperation(); break;
        case STATE_INFO: mainStateInfo(); break;
        case STATE_ADM: mainStateAdm(); break;
        default: break;
    }
}

// Funções para cada estado principal
void mainStateUpdate() {
    displayMessage("Estado Principal", "0: Modo Atualizacao","",getTimeStamp().c_str(),false);
    delay(1000);
}

void mainStateInfo() {
    int tamanhUnsigned = unsignedCache.size(); 
    int tamanhoAutorizados =  idCache.size();
    displayMessage("Estado Principal", "1: Modo informacao","","");
    switch (localState)
    {
    case 0:
        displayMessage("","",String("Device Id: "+DEVICE_ID).c_str(),String("IP Loc:"+ipLocal).c_str(),false);
        break;  
    case 1:
        displayMessage("","",String("Srv: "+server_addr.toString()).c_str(),String("Ntp:"+ntpServer).c_str(),false);
        break;  
    case 2:
        displayMessage("","",String("Reg Pend.: "+tamanhUnsigned).c_str(),String("Reg Aut.: "+tamanhoAutorizados).c_str(),false);
        break;        
    default:
        break;
    }
    delay(1000);
}

void mainStateOperation() {
    displayMessage("Modo de operacao", "Aproxime a Tag RFID");  
    String codigo = getIdCard(1,"Modo de operacao", "Aproxime a Tag RFID");
    if(mainState !=1) return;

    logMessage("INFO", ("Tag RFID lido: " + codigo).c_str());

    displayMessage("Cartao RFID:", codigo.c_str());
    delay(1000);

    if (!identificacao(codigo)) {
        logMessage("WARNING", ("Tag não cadastrada: " + codigo).c_str());
        displayMessage("Cartao nao", "cadastrado!");
        delay(1500);
        return;
    }

    String nome = idCache[codigo].first;
    int idFp = idCache[codigo].second;

    if (idFp == -1) {
        logMessage("ERROR", ("Acesso negado para Tag: " + codigo).c_str());
        displayMessage("Acesso negado!");
        delay(3000);
    } else {
        displayMessage("Posicione o dedo","no leitor biometrico");
        if (autenticacao(idFp)) {
            logMessage("INFO", ("Acesso liberado para: " + nome).c_str());
            displayMessage("Bem-vindo(a),", nome.c_str(), "Acesso liberado!");
            digitalWrite(FECHADURA, HIGH);
            delay(3000);
            digitalWrite(FECHADURA, LOW);
        } else {
            logMessage("ERROR", ("Falha na autenticação biométrica para: " + nome).c_str());
            displayMessage("Acesso negado!");
            delay(2000);
        }
    }
}

// Adicionando a função para carregar os IDs de administradores do SD
void carregarAdministradores() {
    File file = SD.open(admFilename, FILE_READ);
    if (!file) {
        Serial.println("Erro ao abrir administrators.txt");
        displayMessage("Erro ao abrir", admFilename);
        delay(3000);
        return;
    }

    adminCards.clear();  // Limpa qualquer dado anterior
    while (file.available()) {
        String linha = file.readStringUntil('\n');
        linha.trim();  // Remove espaços e quebras de linha

        if (linha.length() > 0) {
            adminCards.insert(linha);
        }
    }
    file.close();

    Serial.println("Lista de administradores carregada!");
    displayMessage("Administradores", "Carregados!");
    delay(1000);
}

// Função para verificar se uma Tag é de administrador
bool verificaAdministrador(String idCartao) {
    return adminCards.find(idCartao) != adminCards.end();
}

// Função do estado "Administrador"
void mainStateAdm() {
    
    //atualiza o cache com as autorizacoes pendentes para que o administrador possa fazer o enroll
    atualizarCacheDePendentes();

    static int lastLocalState = -1; // Variável para detectar mudanças no estado local

    char* estado[] = {"Modo: Standby       ", "Modo: Enroll        ", "Modo: Clean         "};

    // Atualiza a tela apenas se `localState` mudou
    if (localState != lastLocalState) {
        displayMessage("Modo Admin", estado[localState]);
        lastLocalState = localState; // Armazena o novo estado para evitar atualização desnecessária
    }    

    String msg = unsignedCache.empty() ? "Sem pendencias      " : "Acessos pendentes   ";

    String idCartao = getIdCard(STATE_ADM,"Modo Admin/Aprox Tag ", estado[localState],msg.c_str());  // Lê o ID da Tag RFID

    if (mainState != STATE_ADM) return;

    displayMessage("Tag RFID:", idCartao.c_str());
    delay(1000);

    if (verificaAdministrador(idCartao)) {

        String logm;
                
        displayMessage("Modo Admin", estado[localState]);
        switch (localState)
        {
        case 1:
            logm = "Modo Enroll ativado com cartao: "+idCartao;
            logMessage("INFO:",logm.c_str());
            enrollFingerprintsFromUnsignedCache(); // Inicia o processo de enroll
            break;
        case 2:
            logMessage("INFO", "Limpando digitais não autorizadas.");
            limparDigitaisUnsigned(); //limpa digitais de quem não tem associação
            break;        
        default:
            break;
        }
        
        Serial.println("Administrador autenticado! Acesso concedido.");
        delay(2000);
    } else {
        displayMessage("Acesso negado!");
        Serial.println("Acesso negado ao modo Administrador.");
        delay(2000);
    }
}

// Funções de estado local (exemplo)
void localState0() {
    displayMessage("Subestado 0", "Ativo");
}

void localState1() {
    displayMessage("Subestado 1", "Ativo");
}

// Função para executar o estado local atual
void executeLocalState() {
    switch (localState) {
        case 0: localState0(); break;
        case 1: localState1(); break;
        default: break;
    }
}

/**
 * Verifica se o ID da Tag RFID existe no mapeamento.
 */
bool identificacao(String idCartao) {
    return idCache.find(idCartao) != idCache.end();
}

/**
 * Inicializa o sensor biométrico e verifica se está respondendo.
 */
void inicializaSensorBiometrico() {
    
    finger.begin(57600);
    displayMessage("Configurando","Leitor biometrico...");
    delay(2000);
    bool resultado;
    do{
        resultado = finger.verifyPassword();
    } while (!resultado);

    displayMessage("Sensor Biometrico!", "Inicializado!");
    delay(3000);  // Tempo para leitura da mensagem
}

/**
 * Exibe mensagens no LCD 20x4 e aguarda 3 segundos.
 */
void displayMessage(const char *line1, const char *line2, const char *line3, const char *line4, bool clear) {
    if (clear) {
        lcd.clear();
    }
    lcd.setCursor(0, 0); lcd.print(line1);
    lcd.setCursor(0, 1); lcd.print(line2);
    lcd.setCursor(0, 2); lcd.print(line3);
    lcd.setCursor(0, 3); lcd.print(line4);    
}

/**
 * Inicializa o LCD e exibe uma mensagem de inicialização.
 */
void inicializaLCD() {
    String pontos = "";
    lcd.init();
    lcd.backlight();
    displayMessage(String("Dispositivo: "+DEVICE_ID).c_str(),"Iniciado!");
    delay(1000);
}

/**
 * Configura o barramento SPI para comunicação com SD e RFID.
 */
void configuraSPI() {
    SPI2.begin(SCK_SD, MISO_SD, MOSI_SD, SS_SD);
    SPI3.begin(SCK_RFID, MISO_RFID, MOSI_RFID, SS_RFID);
}

/**
 * Inicializa o cartão SD e verifica se o arquivo de identidades está presente.
 */
void inicializaSDCard() {
    if (!SD.begin(SS_SD, SPI2)) {
        displayMessage("Erro no SD!", "Sem cache de IDs.");
        logMessage("ERROR", "Falha ao inicializar o cartão SD!");
        delay(3000);
        sdDisponivel = false;
        return;
    }
    sdDisponivel = true;
    displayMessage("SDCard inicializado.");
    logMessage("INFO", "Cartão SD inicializado com sucesso.");
    delay(1000);
}

/**
 * Inicializa o módulo RFID-RC522.
 */
void inicializaRFID() {
    SPI = SPI3;
    rfid.PCD_Init();
    displayMessage("RFID Configurado!");
    delay(1000);  // Tempo para leitura da mensagem
}

/**
 * Captura o código da Tag RFID e retorna uma string com o ID.
 */
String getIdCard(int estado, const char *line1, const char *line2, const char *line3) {

    static unsigned long lastUpdate = 0;
    static unsigned long lastDisplayUpdate = 0;
    const unsigned long updateInterval = INTERVAL_UPDATE;
    const unsigned long updateDisplayInterval = 1000;
    
    displayMessage("","","",getTimeStamp().c_str());

    while ((!(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) && mainState == estado)) {

        char* estadoLocalAdm[] = {"Modo: Standby       ", "Modo: Enroll        ", "Modo: Clean         "};
        
        if (millis() - lastUpdate >= updateInterval) {
            digitalWrite(SUCESSO, LOW);
            if(servidorDisponivel()){
                atualizarCacheDeAutorizados() ;
                atualizarCacheDeAdministradores();
                atualizarCacheDePendentes();
                enviarLogsDoSDCard();
            }
            lastUpdate = millis();
            digitalWrite(SUCESSO, HIGH);
        }          
        if(millis() - lastDisplayUpdate >= updateDisplayInterval){
            if(mainState == STATE_ADM) {
                displayMessage(line1,estadoLocalAdm[localState],line3,getTimeStamp().c_str(),false);
            } else {
                displayMessage(line1,line2,line3,getTimeStamp().c_str(),false);
            }
        }
        delay(100);
    }
    String idCard = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        String byteHex = String(rfid.uid.uidByte[i], HEX);
        byteHex.toUpperCase();
        if (rfid.uid.uidByte[i] < 0x10) idCard += "0";
        idCard += byteHex;
    }
    return idCard;
}


/**
 * Captura uma digital e retorna o ID do usuário ou códigos de erro.
 */
int getFingerprintID(int timeout) {

    logMessage("INFO", "Aguardando impressão digital...");
    displayMessage("Posicione o dedo","no leitor biometrico");

    int result;
    unsigned long startTime = millis();
    
    do {
        result = finger.getImage();
        delay(100);

        if (millis() - startTime >= timeout) {
            logMessage("WARNING", "Tempo limite de leitura biométrica atingido.");
            displayMessage("Tempo esgotado!");
            return -3;
        }
    } while (result != FINGERPRINT_OK);

    result = finger.image2Tz();
    if (result != FINGERPRINT_OK) {
        logMessage("ERROR", "Falha ao converter imagem para template biométrico.");
        displayMessage("Erro na conversão!");
        return -2;
    }

    result = finger.fingerFastSearch();
    if (result == FINGERPRINT_OK) {
        logMessage("INFO", ("Impressão digital reconhecida, ID: " + String(finger.fingerID)).c_str());
        return finger.fingerID;
    } else {
        logMessage("WARNING", "Nenhuma impressão digital correspondente encontrada.");
        return -1;
    }
}


/**
 * Compara a digital lida com a armazenada no banco de dados no Id especificado no parametro.
 */
bool autenticacao(int fingerPrintId) {
    uint16_t score;
    return (finger.verifyFingerprint1to1(fingerPrintId,score) == FINGERPRINT_OK);
}


/**
 * Carrega os IDs armazenados no cartão SD para um cache na memória.
 */

void carregarCacheIDs() {
    if (!sdDisponivel) {
        logMessage("ERROR", "Tentativa de carregar IDs sem SD disponível.");
        return;
    }

    File file = SD.open(authorizedsFilename, FILE_READ);
    if (!file) {
        logMessage("ERROR", "Erro ao abrir identidades.txt.");
        displayMessage("Erro ao abrir", authorizedsFilename);
        delay(3000);
        return;
    }

    while (file.available()) {
        String linha = file.readStringUntil('\n');
        int sep1 = linha.indexOf(',');
        int sep2 = linha.indexOf(',', sep1 + 1);

        if (sep1 != -1 && sep2 != -1) {
            String idCartao = linha.substring(0, sep1);
            String nome = linha.substring(sep1 + 1, sep2);
            String idFingerPrintStr = linha.substring(sep2 + 1);

            idCartao.trim();
            nome.trim();
            idFingerPrintStr.trim();

            int idFingerPrint = idFingerPrintStr.toInt();
            if (idFingerPrint == 0 && idFingerPrintStr != "0") continue;

            idCache[idCartao] = std::make_pair(nome, idFingerPrint);
        }
    }
    file.close();
    logMessage("INFO", "Cache de IDs carregado com sucesso.");
}

void carregarUnsignedCache() {
    if (!sdDisponivel) {
        logMessage("ERROR", "Tentativa de carregar IDs sem SD disponível.");
        return;
    }

    File file = SD.open(unsignedFilename, FILE_READ); // Abre o arquivo CSV
    if (!file) {
        logMessage("ERROR", "Erro ao abrir unsigned.txt.");
        displayMessage("Erro ao abrir", unsignedFilename);
        delay(3000);
        return;
    }

    // Pula o cabeçalho do CSV (primeira linha)
    file.readStringUntil('\n');

    while (file.available()) {
        String linha = file.readStringUntil('\n');
        linha.trim();  // Remove espaços e quebras de linha

        // Divide a linha em campos separados por vírgula
        int sep1 = linha.indexOf(','); // Encontra a primeira vírgula
        int sep2 = linha.indexOf(',', sep1 + 1); // Encontra a segunda vírgula

        if (sep1 != -1 && sep2 != -1) {
            String idCard = linha.substring(0, sep1); // Extrai o idCard
            String nome = linha.substring(sep1 + 1, sep2); // Extrai o nome
            String idStr = linha.substring(sep2 + 1); // Extrai o id

            // Remove espaços em branco
            idCard.trim();
            nome.trim();
            idStr.trim();

            // Converte o id para inteiro
            int id = idStr.toInt();

            // Adiciona ao cache de IDs não cadastrados
            unsignedCache[idCard] = std::make_pair(nome, id);
        }
    }

    file.close();
    logMessage("INFO", "Cache de IDs não cadastrados carregado com sucesso.");
}


void enrollFingerprintsFromUnsignedCache() {
    if (unsignedCache.empty()) {
        logMessage("WARNING", "Nenhum dado no cache de IDs não cadastrados.");
        displayMessage("Aproxime a Tag RFID", "Nenhum dado no cache");
        return;
    }

    while ((!unsignedCache.empty()) && (mainState == STATE_ADM) && (localState == 1 )) { 
        displayMessage("Aproxime a Tag RFID", "para cadastrar digital");
        String idCard = getIdCard(STATE_ADM,"Aproxime a Tag RFID", "para cadastrar digital"); // Aguarda uma Tag RFID ser aproximado
        if (idCard.isEmpty()) continue;

        logMessage("INFO", ("Tag aproximado: " + idCard).c_str());
        displayMessage("Tag RFID lida:", idCard.c_str());
        delay(1000);

        auto it = unsignedCache.find(idCard);
        if (it == unsignedCache.end()) {
            logMessage("WARNING", "Tag não encontrada na lista de pendentes.");
            displayMessage("Tag nao encontrada", "na lista de pendentes");
            delay(2000);
            continue;
        }

        String nome = it->second.first;
        int id = it->second.second;
        displayMessage("Cadastrando:", nome.c_str(), "ID: ", String(id).c_str());
        delay(3000);

        if (servidorDisponivel()){ 
            if (enrollFingerprint(nome,id)) {
                logMessage("INFO", ("Digital cadastrada com sucesso para: " + nome).c_str());
                displayMessage("Sucesso!", "Digital cadastrada.");
                delay(2000);

                if (adicionarIdentidadeAoArquivo(idCard, nome, id)) {
                    idCache[idCard] = std::make_pair(nome, id);
                    logMessage("INFO", ("Entrada adicionada ao arquivo de identidades: " + nome).c_str());

                    Serial.println("atualizarFingerprint(idCard, id, DEVICE_ID)");
                    String s = String("idCard: "+idCard+" Nome: "+nome+" Id: "+id);
                    Serial.println(s);
                    Serial.println();

                    atualizarFingerprint(idCard, id, DEVICE_ID);

                    displayMessage("Cadastro concluido", "Autorizado com sucesso");
                } else {
                    logMessage("ERROR", "Falha ao adicionar entrada ao arquivo de identidades.");
                    displayMessage("Erro no cadastro", "Tente novamente");
                }

                unsignedCache.erase(it);
                limparArquivoUnsigned();
                for (const auto& entry : unsignedCache) {
                    adicionarIdentidadeAoArquivo(entry.first, entry.second.first, entry.second.second);
                }
            } 
        }else {
            logMessage("ERROR", "Falha ao cadastrar digital.");
            displayMessage("Erro no cadastro", "Tente novamente");
        }
    }
}


bool enrollFingerprint(String nome,int id) {
    int result = -1;

    displayMessage("Coloque o dedo", "no sensor...","","",false);

    // Passo 1: Captura da primeira imagem
    while (result != FINGERPRINT_OK) {
        result = finger.getImage();
        if (result == FINGERPRINT_OK) {
            displayMessage("Imagem capturada.");
        } else if (result == FINGERPRINT_NOFINGER) {
            displayMessage(nome.c_str(),"Coloque o dedo","no sensor","",false);
        } else {
            displayMessage("Erro na captura.");
            return false;
        }
        delay(100);
    }

    // Passo 2: Converte a imagem para template
    result = finger.image2Tz(1);
    if (result != FINGERPRINT_OK) {
        displayMessage("Erro na conversão.");
        return false;
    }
    displayMessage(nome.c_str(),"remova o dedo","do sensor");
    delay(2000);

    // Passo 3: Captura da segunda imagem
    displayMessage(nome.c_str(),"coloque o mesmo","dedo no sensor");
    result = -1;
    while (result != FINGERPRINT_OK) {
        result = finger.getImage();
        if (result == FINGERPRINT_OK) {
            displayMessage("Imagem capturada.");
        } else if (result == FINGERPRINT_NOFINGER) {
            displayMessage(nome.c_str(),"coloque o mesmo","dedo no sensor","",false);
        } else {
            displayMessage("Erro na captura.");
            return false;
        }
        delay(100);
    }

    // Passo 4: Converte a segunda imagem para template
    result = finger.image2Tz(2);
    if (result != FINGERPRINT_OK) {
        displayMessage("Erro na conversão.");
        return false;
    }

    // Passo 5: Cria o modelo da digital
    result = finger.createModel();
    if (result != FINGERPRINT_OK) {
        displayMessage("Erro ao criar modelo.");
        return false;
    }

    // Passo 6: Armazena o modelo no sensor
    result = finger.storeModel(id);
    if (result != FINGERPRINT_OK) {
        displayMessage("Erro ao armazenar.");
        return false;
    }
    displayMessage(nome.c_str(),"Digital cadastrada!", "com sucesso!");
    delay(3000);
    return true;
}

bool lerParametrosRede() {
    File file = SD.open(netFilename, FILE_READ);
    if (!file) {
        Serial.println("[ERROR] Falha ao abrir "+ String(netFilename));
        return false;
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim(); // Remove espaços extras e quebras de linha

        if (line.startsWith("SSID=")) {
            ssid = line.substring(5);
        } else if (line.startsWith("SENHA=")) {
            senha = line.substring(6);
        } else if (line.startsWith("SERVER_ADDR=")) {
            String ipString = line.substring(12);
            sscanf(ipString.c_str(), "%hhu.%hhu.%hhu.%hhu", &server_addr[0], &server_addr[1], &server_addr[2], &server_addr[3]);
        } else if (line.startsWith("SERVER=")) {
            ntpServer = line.substring(7);
        } else if (line.startsWith("GMT=")) {
            gmtOffset_sec = line.substring(4).toInt();
        } else if (line.startsWith("DST=")) {
            daylightOffset_sec = line.substring(4).toInt();
        }
    }

    file.close();

    if (ssid.length() > 0 && senha.length() > 0) {
        Serial.println("[INFO] parametros de rede lidas do SD:");
        Serial.println("SSID: " + ssid);
        Serial.println("Senha: ******");
        Serial.print("IP Servidor: ");
        Serial.println(server_addr);
        Serial.println("Servidor NTP: "+ntpServer);
        Serial.print("GMT: ");
        Serial.println(gmtOffset_sec);
        Serial.print("DST: ");
        Serial.println(daylightOffset_sec);
        Serial.println("");
        return true;
    } else {
        Serial.println("[ERROR] erro ao recuperar parametros de rede.");
        return false;
    }
}

void conectarWiFi() {
    logMessage("INFO", "Iniciando conexão WiFi...");
    displayMessage("Conectando ao WiFi...", "", String("SSID: "+ssid).c_str());

    WiFi.begin(ssid.c_str(), senha.c_str());

    unsigned long tempoInicial = millis();
    int tentativa = 0;

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        tentativa++;
        Serial.print(".");
        
        // Atualiza o LCD a cada 3 tentativas
        if (tentativa % 3 == 0) {
            String msgTentativas = "Tentativa: " + String(tentativa);
            displayMessage("Conectando ao WiFi", "", msgTentativas.c_str());
        }

        // Timeout de 15 segundos (caso não conecte)
        if (millis() - tempoInicial > 25000) {
            logMessage("ERROR", "Falha ao conectar ao WiFi!");
            displayMessage("Erro: WiFi falhou!", "Verifique o SD", "e reinicie.");
            return;
        }
    }

    // WiFi conectado com sucesso
    logMessage("INFO", "WiFi conectado com sucesso!");
    ipLocal = WiFi.localIP().toString();

    displayMessage("WiFi Conectado!","", String("IP: "+ ipLocal).c_str());
    Serial.println("\n[INFO] Conectado ao WiFi!");
    Serial.print("IP: ");
    Serial.println(ipLocal);
    delay(1000);  // Tempo para o usuário visualizar no LCD
}

void logMessage(const char* level, const char* message) {
    String timestamp = getTimeStamp();  // Obtém o horário atual
    String logEntry = "["+DEVICE_ID+"] [" + String(level) + "] [" + timestamp + "] " + String(message);

    // Exibir no Monitor Serial
    Serial.println(logEntry);

    // Gravar no cartão SD
    File logFile = SD.open(logFilename, FILE_APPEND);
    if (logFile) {
        logFile.println(logEntry);
        logFile.close();
    } else {
        Serial.println("[ERROR] Falha ao abrir log.txt para escrita!");
    }
}

void configurarRelogioNTP() {
    logMessage("INFO", "Sincronizando relógio via NTP...");
    displayMessage("Ajustando Hora...", "", "Aguarde...");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str());

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        logMessage("ERROR", "Falha ao obter horário via NTP.");
        displayMessage("Erro: NTP falhou!", "Verifique a rede.");
        return;
    }

    char horaFormatada[25];
    strftime(horaFormatada, sizeof(horaFormatada), "%d/%m/%Y %H:%M:%S", &timeinfo);

    logMessage("INFO", ("Hora sincronizada: " + String(horaFormatada)).c_str());
    displayMessage("Horario Atualizado!", horaFormatada);

    Serial.print("[INFO] Hora Atual: ");
    Serial.println(horaFormatada);
}

String getTimeStamp() {
    struct tm timeinfo;
    
    // Obtém o horário atual
    if (!getLocalTime(&timeinfo)) {
        Serial.println("[ERROR] Falha ao obter horário!");
        return "00/00/0000 00:00:00";  // Retorno padrão em caso de erro
    }

    // Formatar data e hora
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
    
    return String(buffer);
}

bool adicionarIdentidadeAoArquivo(String idCard, String nome, int id) {
    if (!sdDisponivel) {
        logMessage("ERROR", "Tentativa de adicionar identidade sem SD disponível.");
        return false;
    }

    File file = SD.open(authorizedsFilename, FILE_APPEND); // Abre o arquivo em modo de append
    if (!file) {
        logMessage("ERROR", "Erro ao abrir identidades.txt.");
        displayMessage("Erro ao abrir", authorizedsFilename);
        return false;
    }

    // Formata a linha a ser adicionada
    String linha = idCard + "," + nome + "," + String(id) + "\n";

    // Escreve a linha no arquivo
    if (file.print(linha)) {
        file.close();
        logMessage("INFO", ("Entrada adicionada ao arquivo: " + linha).c_str());
        return true;
    } else {
        file.close();
        logMessage("ERROR", "Falha ao escrever no arquivo.");
        return false;
    }
}

void limparArquivoUnsigned() {
    if (!sdDisponivel) {
        logMessage("ERROR", "Tentativa de limpar arquivo sem SD disponível.");
        return;
    }

    File file = SD.open(unsignedFilename, FILE_WRITE); // Abre o arquivo em modo de escrita
    if (!file) {
        logMessage("ERROR", "Erro ao abrir unsigned.txt para escrita.");
        displayMessage("Erro ao abrir", unsignedFilename);
        return;
    }

    // Escreve o conteúdo do cache atualizado no arquivo
    for (const auto& entry : unsignedCache) {
        file.println(entry.first + "," + entry.second.first + "," + String(entry.second.second));
    }
    
    file.close(); // Fecha o arquivo
    logMessage("INFO", "Arquivo unsigned.txt atualizado com o cache.");
    displayMessage("Arquivo atualizado:", unsignedFilename);
}

/* ll;

void exibirQuantidadeDigitais() {
    if (finger.getTemplateCount() != FINGERPRINT_OK) {
        logMessage("ERROR", "Falha ao obter quantidade de digitais armazenadas.");
        displayMessage("Erro ao obter", "quantidade de digitais!");
        return;
    }
    int totalDigitais = finger.templateCount; // Obtém o total de digitais
    logMessage("INFO", ("Total de digitais: " + String(totalDigitais)).c_str());
    displayMessage("Total de digitais:", String(totalDigitais).c_str());
    delay(3000);
} */

bool atualizarFingerprint(String idcard, int pessoa_id, String codigo_dispositivo) {
    if (WiFi.status() == WL_CONNECTED) { // Verifica se o WiFi está conectado
        HTTPClient http;
        String url = "http://"+server_addr.toString()+"/post_update_fprint.php";
        http.begin(url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        // Monta os dados para envio na requisição POST
        String postData = "idcard=" + idcard + "&pessoa_id=" + String(pessoa_id) + "&codigo_dispositivo=" + codigo_dispositivo;

        Serial.println("atualizarFingerprint(String idcard, int pessoa_id, String codigo_dispositivo)");
        Serial.print("API url: ");
        Serial.println(url); 
        Serial.print("postData: ");
        Serial.println(postData); 

        // Envia a requisição POST e obtém o código de resposta
        int httpResponseCode = http.POST(postData);
        String response = http.getString(); // Obtém a resposta do servidor

        Serial.print("Código de resposta HTTP: ");
        Serial.println(httpResponseCode);
        Serial.println("Resposta do servidor: " + response);

        http.end(); // Finaliza a conexão HTTP

        // Verifica se a atualização foi bem-sucedida analisando a resposta do servidor
        if (httpResponseCode == 200 && response.indexOf("\"sucesso\"") != -1) {
            return true; // A atualização foi bem-sucedida
        } else {
            return false; // Falha na atualização
        }
    } else {
        Serial.println("Erro: WiFi não está conectado!");
        return false;
    }
}

bool enviarLogParaServidor(String log) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://"+server_addr.toString()+"/post_auditoria.php";
        http.begin(url);
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String postData = "log=" + log;
        int httpResponseCode = http.POST(postData);
        String response = http.getString();

        Serial.println("enviarLogParaServidor(String log)");
        Serial.print("API url: ");
        Serial.println(url); 
        Serial.print("postData: ");
        Serial.println(postData); 

        Serial.print("Código HTTP: ");
        Serial.println(httpResponseCode);
        Serial.println("Resposta: " + response);

        http.end();
        return (httpResponseCode == 200 && response.indexOf("\"sucesso\"") != -1);
    } else {
        Serial.println("WiFi desconectado!");
        return false;
    }
}

void enviarLogsDoSDCard() {

    bool enviou = false;

    File arquivo = SD.open(logFilename, "r");
    if (!arquivo) {
        Serial.println("Arquivo de auditoria não encontrado!");
        return;
    }

    Serial.println("Enviando logs...");
    while (arquivo.available()) {
        String linha = arquivo.readStringUntil('\n');
        linha.trim();

        if (linha.length() > 0) {
            if (enviarLogParaServidor(linha)) {
                Serial.println("Log enviado com sucesso: " + linha);
                enviou = true;
            } else {
                Serial.println("Falha ao enviar log: " + linha);
                enviou = false;
            }
        }
    }

    arquivo.close();

    // Opcional: Apagar o arquivo após o envio
    if(enviou){
        if (SD.remove(logFilename)) {
            Serial.println("Arquivo de auditoria deletado após envio.");
        } else {
            Serial.println("Erro ao deletar arquivo.");
        }
    }
}

void atualizarCacheDeAutorizados() {
    if (WiFi.status() != WL_CONNECTED) {
        logMessage("ERROR", "WiFi não está conectado. Não é possível atualizar os autorizados.");
        return;
    }

    HTTPClient http;
    String url = "http://" + server_addr.toString() + "/get_pessoas_autorizadas.php?codigo_dispositivo=" + DEVICE_ID;
    http.begin(url);

    Serial.println("atualizarCacheDeAutorizados()");
    Serial.print("API url: ");
    Serial.println(url);      

    int httpResponseCode = http.GET();
    if (httpResponseCode != 200) {
        logMessage("ERROR", "Falha ao obter dados da API de autorizados.");
        http.end();
        return;
    }

    String response = http.getString();
    http.end();

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        logMessage("ERROR", "Erro ao processar JSON da API.");
        return;
    }

    File file = SD.open(authorizedsFilename, FILE_WRITE);
    if (!file) {
        logMessage("ERROR", "Erro ao abrir o arquivo de autorizados para escrita.");
        return;
    }

    idCache.clear();
    for (JsonObject obj : doc.as<JsonArray>()) {
        String idCartao = obj["idcard"].as<String>();
        String nome = obj["nome"].as<String>();
        int idFingerPrint = obj["id"].as<int>();

        file.println(idCartao + "," + nome + "," + String(idFingerPrint));
        idCache[idCartao] = std::make_pair(nome, idFingerPrint);
    }
    file.close();
    logMessage("INFO", "Arquivo de autorizados atualizado com sucesso.");
}



void atualizarCacheDeAdministradores() {
    if (WiFi.status() != WL_CONNECTED) {
        logMessage("ERROR", "WiFi não está conectado. Não é possível atualizar os administradores.");
        return;
    }

    HTTPClient http;
    String url = "http://" + server_addr.toString() + "/get_administradores.php";
    http.begin(url);
    int httpResponseCode = http.GET();

    Serial.println("atualizarCacheDeAdministradores()");
    Serial.print("API url: ");
    Serial.println(url);    

    if (httpResponseCode != 200) {
        logMessage("ERROR", "Falha ao obter dados da API de administradores.");
        http.end();
        return;
    }

    String response = http.getString();
    http.end();

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        logMessage("ERROR", "Erro ao processar JSON da API.");
        return;
    }

    File file = SD.open(admFilename, FILE_WRITE);
    if (!file) {
        logMessage("ERROR", "Erro ao abrir o arquivo de administradores para escrita.");
        return;
    }

    adminCards.clear();
    for (JsonVariant idcard : doc.as<JsonArray>()) {
        String id = idcard.as<String>();
        if (!id.isEmpty()) {
            file.println(id);
            adminCards.insert(id);
        }
    }
    file.close();
    logMessage("INFO", "Cache e arquivo de administradores atualizados com sucesso.");
}

void atualizarCacheDePendentes() {
    if (WiFi.status() != WL_CONNECTED) {
        logMessage("ERROR", "WiFi não está conectado. Não é possível atualizar pendentes.");
        return;
    }

    HTTPClient http;
    String url = "http://" + server_addr.toString() + "/get_pessoas_sem_fingerprint.php?codigo=" + DEVICE_ID;
    Serial.println("atualizarCacheDePendentes()");
    Serial.print("API url: ");
    Serial.println(url);
    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode != 200) {
        logMessage("ERROR", "Falha ao obter dados da API de pendentes.");
        http.end();
        return;
    }

    String response = http.getString();
    http.end();

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        logMessage("ERROR", "Erro ao processar JSON da API.");
        return;
    }

    File file = SD.open(unsignedFilename, FILE_WRITE);
    if (!file) {
        logMessage("ERROR", "Erro ao abrir o arquivo de pendentes para escrita.");
        return;
    }

    unsignedCache.clear();
    file.println("idCard,nome,id"); // Escreve o cabeçalho
    for (JsonObject obj : doc.as<JsonArray>()) {
        String idCard = obj["pessoa_idcard"].as<String>();
        String nome = obj["pessoa_nome"].as<String>();
        int id = obj["pessoa_id"].as<int>();

        if (!idCard.isEmpty()) {
            file.println(idCard + "," + nome + "," + String(id));
            unsignedCache[idCard] = std::make_pair(nome, id);
        }
    }
    file.close();
    logMessage("INFO", "Cache e arquivo de pendentes atualizados com sucesso.");
}

bool servidorDisponivel() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[ERROR] WiFi desconectado. Não é possível testar o servidor.");
        return false;
    }

    HTTPClient http;
    String url = "http://" + server_addr.toString() + "/status.php";  // Endpoint para teste

    Serial.println("[INFO] Testando disponibilidade do servidor...");
    http.begin(url);
    int httpResponseCode = http.GET();
    String response = http.getString();
    http.end();

    // Se o servidor responder corretamente
    if (httpResponseCode == 200 && response.indexOf("OK") != -1) {
        Serial.println("[INFO] Servidor API disponivel.");
        return true;
    } else {
        Serial.println("[ERROR] Falha na conexao com a API. Codigo HTTP: " + String(httpResponseCode));
        return false;
    }
}

/**
 * @author Luiz Carlos
 * @brief Retira do sensor digitais de pessoas que não estão autorizadas
 */

 void limparDigitaisUnsigned() {
    logMessage("INFO", "Iniciando limpeza de digitais não autorizadas...");

    if (unsignedCache.empty()) {
        logMessage("INFO", "Nenhuma digital pendente para remoção.");
        return;
    }

    int removidas = 0;
    int naoEncontradas = 0;

    for (const auto& entry : unsignedCache) {
        int id = entry.second.second; // Obtém o ID da digital

        if (id <= 0) {
            logMessage("WARNING", ("ID inválido encontrado no cache: " + String(entry.first)).c_str());
            continue;
        }

        // Tenta deletar diretamente a digital sem verificar antes
        int result = finger.deleteModel(id);

        if (result == FINGERPRINT_OK) {
            logMessage("INFO", ("Digital ID: " + String(id) + " removida com sucesso.").c_str());
            removidas++;
        } else {
            logMessage("INFO", ("Nenhuma digital encontrada para ID: " + String(id) + ". Nenhuma ação necessária.").c_str());
            naoEncontradas++;
        }
    }

    logMessage("INFO", ("Limpeza finalizada. Digitais removidas: " + String(removidas) + ", não encontradas: " + String(naoEncontradas)).c_str());
}
