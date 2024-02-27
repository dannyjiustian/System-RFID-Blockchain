#include <SPI.h>
#include <TFT_eSPI.h>
#include <MFRC522.h>
#include <DFRobotDFPlayerMini.h>
#include <Free_Fonts.h>
#include <OneButton.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <WiFi.h>

#define FPSerial Serial1
#define SS_PIN_RFID 5
#define RST_PIN_RFID 0
#define RX_PIN_DFPLAYER 26
#define TX_PIN_DFPLAYER 25
#define BTN_PIN 13

unsigned long
    timeBefore_SMART_CONFIG = 0,  // setting waktu sebelumnya untuk smartconfig
    interval_SMART_CONFIG = 1000, // interval waktu untuk refreshnya
    timeBefore_STATUS_WIFI = 0,   // setting waktu sebelumnya untuk led wifi
    interval_STATUS_WIFI = 300;   // interval waktu untuk kedipannya

bool
    WRITE_EEPROM_WIFI = false,       // mengsetting kondisi write eeprom wifi
    SMART_CONFIG_SUCCESS = false,    // mengsetting kondisi smartconfig
    CHECK_DISPLAY_ON_STANDBY = true; // mengsetting kondisi display standby

uint8_t displayTextLength;

int switchMode = 0;
bool checkSwitchMode = false;
String *displayMode = nullptr;

TFT_eSPI tft = TFT_eSPI();
MFRC522 rfid(SS_PIN_RFID, RST_PIN_RFID);
MFRC522::MIFARE_Key key;
DFRobotDFPlayerMini DFPlayer;
OneButton button(BTN_PIN, true);

// init wifi memiliki port UDP
WiFiUDP Udp;

// void DFPlayerInformation(uint8_t type, int value);

int arrayLength(String arr[])
{
  int count = 0;
  while (arr[count] != "")
  {
    count++;
  }
  return count;
}

// fungsi untuk membaca chip id yang ada didalam esp
String ReadChipId()
{
  String id;
  uint64_t chipid;
  char ssid[13];
  chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(ssid, 13, "%04X%08X", chip, (uint32_t)chipid);
  for (int i = 0; i < 12; i++)
  {
    id += String(ssid[i]);
  }
  return String(id);
}

// fungsi untuk menulis data ke eeprom
void Write_EEPROM(char add, String data)
{
  int _size = data.length();
  for (int i = 0; i < _size; i++)
  {
    EEPROM.write(add + i, data[i]);
  }
  EEPROM.write(add + _size, '\0'); // untuk menghentikan penulisan EEPROM
  EEPROM.commit();
}

// fungsi untuk membaca data di eeprom
String Read_EEPROM(char add)
{
  char data[100]; // Max 100 Bytes
  int length = 0;
  unsigned char karakter;
  karakter = EEPROM.read(add);
  // Baca EEPROM Sampai Null
  while (karakter != '\0' && length < 500)
  {
    karakter = EEPROM.read(add + length);
    data[length] = karakter;
    length++;
  }
  data[length] = '\0';
  return String(data);
}

// fungsi untuk mengambil uid rfid
String GetUIDString(MFRC522::Uid uid)
{
  String strID = "";
  for (byte i = 0; i < uid.size; i++)
    strID +=
        (uid.uidByte[i] < 0x10 ? "0" : "") +
        String(uid.uidByte[i], HEX) +
        (i != uid.size - 1 ? ":" : "");
  strID.toUpperCase();
  return strID;
}

// fungsi untuk membuat tampilan text pada lcd
void GenerateDisplay(String text[], uint8_t textCount, int32_t widthScreen, int32_t heightScreen)
{
  // hapus semua tampilan pada lcd
  tft.fillScreen(TFT_BLACK);
  // membuat text align center middle
  tft.setTextDatum(MC_DATUM);
  // proses cetak tulisan
  for (int i = 0; i < textCount; i++)
  {
    tft.setFreeFont(FSB18);
    tft.drawString(text[i], widthScreen, heightScreen, GFXFF);
    heightScreen += tft.fontHeight(GFXFF);
  }
}

// fungsi untuk menampilkan informasi dfplayer
void DFPlayerInformation(uint8_t type)
{
  switch (type)
  {
  case DFPlayerCardInserted:
  {
    String insertedText[] = {"Kartu SD", "Masuk!"};
    GenerateDisplay(insertedText, 2, tft.width() / 2, 90);
    delay(2000);
  }
  break;
  case DFPlayerCardRemoved:
  {
    String removedText[] = {"Kartu SD", "Keluar!"};
    GenerateDisplay(removedText, 2, tft.width() / 2, 90);
  }
  break;
  default:
    break;
  }
}

// fungsi tombol untuk mengubah mode baca rfid
void SwitchMode()
{
  checkSwitchMode = true;

  // Free previously allocated memory to avoid memory leaks
  delete[] displayMode;

  // Define modes
  String tempMode[] = {"Perangkat", "Dalam Mode", (switchMode < 1 ? "Pengisian!" : "Pembayaran!")};
  int tempSize = sizeof(tempMode) / sizeof(tempMode[0]);

  // Dynamically allocate memory for displayMode
  displayMode = new String[tempSize];

  // Copy content from tempMode to displayMode
  for (int i = 0; i < tempSize; ++i)
  {
    displayMode[i] = tempMode[i];
  }

  // Toggle switchMode
  switchMode = 1 - switchMode;
}

// fungsi untuk restart ESP
void RestartESP()
{
  ESP.restart();
}

// fungsi untuk reset ESP
void ResetESP()
{
  String displayText[] = {"Proses", "Reset", "Jaringan", "Perangkat"};
  GenerateDisplay(displayText, 4, tft.width() / 2, 50);

  // menghapus ssid dan password dari eeprom
  Write_EEPROM(20, "kosong");
  Write_EEPROM(60, "kosong");

  delay(3000);

  // merestart kembali espnya
  ESP.restart();
}

void setup()
{
  Serial.begin(115200);
  // init eeprom yang bisa diisi sebesar 512 bit
  EEPROM.begin(512);

  FPSerial.begin(9600, SERIAL_8N1, RX_PIN_DFPLAYER, TX_PIN_DFPLAYER);
  tft.init();

  SPI.begin();
  rfid.PCD_Init();

  // write serial number sensor
  Write_EEPROM(0, ReadChipId()); // C4C07C842178

  // matikan wifi
  WiFi.mode(WIFI_OFF);
  delay(100);

  // hidupkan wifi (mode Station AP)
  WiFi.mode(WIFI_STA);

  String displayText[] = {"Selamat", "Datang"};
  GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 80);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(FSB9);
  tft.drawString("VERSI SISTEM 1.0.0", 120, 200, GFXFF);
  if (!DFPlayer.begin(FPSerial, true, true))
  {
    String displayText[] = {"Kartu SD", "Bermasalah!", "Silakan", "Restart", "Manual!"};
    GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 30);
    while (true)
    {
      delay(0);
    }
  }

  DFPlayer.volume(30);
  DFPlayer.play(1);
  delay(2000);

  // mengecek apakah udah ada ssid dan password didalam eeprom
  if (Read_EEPROM(20) != "kosong" && Read_EEPROM(60) != "kosong")
  {
    String displayText1[] = {"Proses", "Koneksi", "Jaringan"};
    GenerateDisplay(displayText1, arrayLength(displayText1), tft.width() / 2, 70);
    delay(2000);
    // mengkoneksikan wifi dengan ssid dan password dalam eeprom
    WiFi.begin(Read_EEPROM(20).c_str(), Read_EEPROM(60).c_str());
    // setting bool smartconfig jadi true
    SMART_CONFIG_SUCCESS = !SMART_CONFIG_SUCCESS;
  }
  else
  {
    // menyalakan smartconfig
    WiFi.beginSmartConfig();
  }

  // menambahkan fungsi pada button
  button.attachClick(SwitchMode);
  button.attachDoubleClick(RestartESP);
  button.attachLongPressStart(ResetESP);

  // hapus semua tampilan pada lcd
  tft.fillScreen(TFT_BLACK);
}

void loop()
{
  // mendefinisikan waktu sekarang dengan value dari millis
  unsigned long timeNow = millis();

  // memanggil trigger ketika button ditekan
  button.tick();
  delay(10);

  // mengecek apakah jaringan terkoneksi
  if (WiFi.status() == WL_CONNECTED)
  {
    // mengecek apakah eeprom sudah di isi atau belum
    if (!WRITE_EEPROM_WIFI)
    {
      // tulis ssid dan password yang telah didapatkan kedalam eeprom
      Write_EEPROM(20, WiFi.SSID());
      Write_EEPROM(60, WiFi.psk());
      // mengubah nilai bool menjadi true
      WRITE_EEPROM_WIFI = !WRITE_EEPROM_WIFI;
    }

    // double check apakah eeprom sudah di isi atau belum
    if (Read_EEPROM(20) == "kosong" && Read_EEPROM(60) == "kosong")
    {
      // tulis ssid dan password yang telah didapatkan kedalam eeprom
      Write_EEPROM(20, WiFi.SSID());
      Write_EEPROM(60, WiFi.psk());
    }

    // setting bool smartconfig jadi true
    SMART_CONFIG_SUCCESS = !SMART_CONFIG_SUCCESS;

    if (CHECK_DISPLAY_ON_STANDBY)
    {
      String displayText[] = {"Perangkat", "Terkoneksi", "Jaringan!"};
      GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 70);
      CHECK_DISPLAY_ON_STANDBY = !CHECK_DISPLAY_ON_STANDBY;
      delay(3000);
    }

    if (checkSwitchMode)
    {
      GenerateDisplay(displayMode, sizeof(displayMode) - 1, tft.width() / 2, 70);
      delay(2000);
      checkSwitchMode = false;
      String displayText[] = {"Silakan Scan", "Kartu Kamu", "Terima Kasih!"};
      GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 70);
    }

    if (DFPlayer.available())
    {
      String displayText[] = {"Silakan Scan", "Kartu Kamu", "Terima Kasih!"};
      GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 70);
      DFPlayerInformation(DFPlayer.readType()); // Print the detail message from DFPlayer to handle different errors and states.
    }

    if ((!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) || DFPlayer.readType() == 3)
      return;

    Serial.print(F("PICC type: "));
    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
    Serial.println(rfid.PICC_GetTypeName(piccType));

    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
        piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
        piccType != MFRC522::PICC_TYPE_MIFARE_4K)
    {
      Serial.println(F("Your tag is not of type MIFARE Classic."));
      return;
    }

    Serial.print("Kartu ID Anda : ");
    Serial.println(GetUIDString(rfid.uid));
    String displayText[] = {"Scan Kartu", "Berhasil", "Terima Kasih!"};
    GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 70);
    DFPlayer.play(2);

    rfid.PICC_HaltA();

    rfid.PCD_StopCrypto1();
  }
  else // apabila jaringan tidak terkoneksi
  {
    // mengecek apakah udah ada ssid dan password didalam eeprom
    if (Read_EEPROM(20) == "kosong" && Read_EEPROM(60) == "kosong" && SMART_CONFIG_SUCCESS == false)
    {
      // mengubah nilai bool menjadi false
      WRITE_EEPROM_WIFI = !WRITE_EEPROM_WIFI;
      // mengulang smartconfig sampai sudah terconfig setiap 1s
      if ((unsigned long)(timeNow - timeBefore_SMART_CONFIG) >= interval_SMART_CONFIG)
      {
        String displayText[] = {"Silakan", "Koneksikan", "Jaringan", "Perangkat"};
        GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 50);
        // setting bool smartconfig tetap false
        SMART_CONFIG_SUCCESS = SMART_CONFIG_SUCCESS;

        CHECK_DISPLAY_ON_STANDBY = !CHECK_DISPLAY_ON_STANDBY;
        timeBefore_SMART_CONFIG = millis();
      }
    }

    // mengecek apakah smartconfig bernilai true ketika koneksi jaringan terputus
    if (SMART_CONFIG_SUCCESS)
    {
      // mengecek apakah waktu sekarang sudah sesuai dengan interval yang di setting
      if ((unsigned long)(timeNow - timeBefore_STATUS_WIFI) >= interval_STATUS_WIFI)
      {
        String displayText[] = {"Koneksi", "Jaringan", "Perangkat", "Bermasalah!"};
        GenerateDisplay(displayText, arrayLength(displayText), tft.width() / 2, 50);

        CHECK_DISPLAY_ON_STANDBY = !CHECK_DISPLAY_ON_STANDBY;
        // mengreset waktu sebelum status wifi dengan waktu sekarang
        timeBefore_STATUS_WIFI = millis();
      }
    }
  }
}