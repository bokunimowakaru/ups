/*******************************************************************************
UPS VRLA Batteries Controller for ESP32C3

https://git.bokunimo.com/ups/

                                          Copyright (c) 2023 Wataru KUNINO
*******************************************************************************/

#include <WiFi.h>                               // ESP32用WiFiライブラリ
#include <WiFiUdp.h>                            // UDP通信を行うライブラリ
#include <HTTPClient.h>                         // HTTPクライアント用ライブラリ
#include "driver/gpio.h"                        // sleep中で使用

#define SSID "1234ABCD"                         // 無線LANアクセスポイントのSSID
#define PASS "password"                         // パスワード
#define PORT 1024                               // 送信のポート番号
#define SLEEP_P 30*1000000ul                    // スリープ時間 30秒(uint32_t)
#define DEVICE "myups_5,"                       // デバイス名(5字+"_"+番号+",")

#define PIN_LED_RGB 8                           // IO8 に WS2812を接続(DevKitM)
#define FET_CHG_PIN 4                           // 充電FET GPIO 4 ピン
#define FET_DIS_PIN 5                           // 放電FET GPIO 5 ピン
#define OUTAGE_PIN 1                            // 停電検出 GPIO 1 ピン
#define ADC_CHG_PIN 2                           // 充電出力 GPIO 2 ピン(ADC1_2)
#define ADC_BAT_PIN 3                           // 電池電圧 GPIO 3 ピン(ADC1_3)
#define ADC_CHG_DIV 12./(110.+12.)              // 抵抗分圧の比率
// #define ADC_CHG_DIV 1.353 / 13.78            // 実測ADC電圧÷実測電池電圧
#define ADC_BAT_DIV 12./(110.+12.)              // 抵抗分圧の比率
// #define ADC_BAT_DIV 1.363 / 13.72            // 実測ADC電圧÷実測電池電圧
#define SENSOR_R_OHM 1.8                        // 電流センサの抵抗値(Ω)

/******************************************************************************
 Ambient 設定
 ******************************************************************************
 ※Ambientのアカウント登録と、チャネルID、ライトキーの取得が必要です。
    1. https://ambidata.io/ へアクセス
    2. 右上の[ユーザ登録(無料)]ボタンでメールアドレス、パスワードを設定して
       アカウントを登録
    3. [チャネルを作る]ボタンでチャネルIDを新規作成する
    4. 「チャネルID」を下記のAmb_Idのダブルコート(")内に貼り付ける
    5. 「ライトキー」を下記のAmb_Keyに貼り付ける
   (参考文献)
    IoTデータ可視化サービスAmbient(アンビエントデーター社) https://ambidata.io/
*******************************************************************************/
#define Amb_Id  "00000"                         // AmbientのチャネルID
#define Amb_Key "0000000000000000"              // Ambientのライトキー

/******************************************************************************
 LINE Notify 設定
 ******************************************************************************
 ※LINE アカウントと LINE Notify 用のトークンが必要です。
    1. https://notify-bot.line.me/ へアクセス
    2. 右上のアカウントメニューから「マイページ」を選択
    3. トークン名「esp32」を入力
    4. 送信先のトークルームを選択する(「1:1でLINE Notifyから通知を受け取る」等)
    5. [発行する]ボタンでトークンが発行される
    6. [コピー]ボタンでクリップボードへコピー
    7. 下記のLINE_TOKENのダブルコート(")内に貼り付け
 *****************************************************************************/
#define LINE_TOKEN  "your_token"                // LINE Notify トークン★要設定

/******************************************************************************
 UDP 宛先 IP アドレス設定
 ******************************************************************************
 カンマ区切りでUPD宛先IPアドレスを設定してください。
 末尾を255にすると接続ネットワーク(アクセスポイント)にブロードキャスト
 *****************************************************************************/
IPAddress UDPTO_IP = {255,255,255,255};         // UDP宛先 IPアドレス

/******************************************************************************
 Wi-Fi コンシェルジェ照明担当（ワイヤレスLED子機） の設定
 ******************************************************************************
 ※ex01_led または ex01_led_io が動作する、別のESP32C3搭載デバイスが必要です
    1. ex01_led/ex01_led_io搭載デバイスのシリアルターミナルでIPアドレスを確認
    2. 下記のLED_IPのダブルコート(")内に貼り付け
 *****************************************************************************/
#define LED_IP "192.168.1.0"                    // LED搭載子のIPアドレス★要設定

/******************************************************************************
 main
 *****************************************************************************/

int MODE = 0;       // -2:故障, -1:手動停止, 0:停止, 1:充電, 2:停電放電, 3:測定
float BAT_V = -0.1; // 電池電圧の測定結果
bool ac;            // ACアダプタからの電源供給状態

float adc(int pin){
    delay(10);
    float div = 0.;
    if(pin == ADC_CHG_PIN) div = ADC_CHG_DIV;
    if(pin == ADC_BAT_PIN) div = ADC_BAT_DIV;
    if(div == 0.) return 0.;
    return analogReadMilliVolts(pin) / div / 1000.;
}

void setup(){                                   // 起動時に一度だけ実行する関数
    led_setup(PIN_LED_RGB);                     // WS2812の初期設定(ポート設定)
    if(FET_CHG_PIN == 4 && FET_DIS_PIN == 5){
        gpio_hold_dis(GPIO_NUM_4);
        gpio_hold_dis(GPIO_NUM_5);
    }else{
        gpio_deep_sleep_hold_dis();
    }
    pinMode(FET_CHG_PIN, OUTPUT);               // デジタル出力に
    pinMode(FET_DIS_PIN, OUTPUT);               // デジタル出力に
    pinMode(OUTAGE_PIN, INPUT);                 // デジタル入力に
    pinMode(ADC_CHG_PIN, ANALOG);               // アナログ入力に
    pinMode(ADC_BAT_PIN, ANALOG);               // アナログ入力に
    Serial.begin(115200);                       // 動作確認のためのシリアル出力
    Serial.println("UPS VRLA Batteries Controller");
    WiFi.mode(WIFI_STA);                        // 無線LANをSTAモードに設定
    Serial.println("WiFi.begin");
    WiFi.begin(SSID,PASS);                      // 無線LANアクセスポイントへ接続
    
    /* 測定 */
    ac = digitalRead(OUTAGE_PIN);               // 停電状態を確認
    if(!ac){                                    // 停電時に放電に切り替える
        digitalWrite(FET_CHG_PIN, LOW);
        digitalWrite(FET_DIS_PIN, HIGH);
    }else{                                      // 電源供給時に放電に切り替える
        digitalWrite(FET_CHG_PIN, LOW);
        digitalWrite(FET_DIS_PIN, LOW);
    }
    delay(10);                                  // 電圧の安定待ち
    BAT_V = adc(ADC_BAT_PIN);
    Serial.println("BAT_V="+String(BAT_V,2));
    if(ac){                                     // 電源供給時に充電を開始する
        digitalWrite(FET_CHG_PIN, HIGH);        // 電源が喪失するまでにON
    }
    while(WiFi.status() != WL_CONNECTED){       // 接続に成功するまで待つ
        led((millis()/50) % 10);                // (WS2812)LEDの点滅
        if(millis() > 30000) sleep();           // 30秒超過でスリープ
        delay(50);                              // 待ち時間処理
    }
    if(!ac){                                    // 停電時
        led(0,10,10);                           // (WS2812)LEDを黄色で点灯
    }else{                                      // 電源供給時
        led(0,20,0);                            // (WS2812)LEDを緑色で点灯
    }
}

void loop(){                                    // 繰り返し実行する関数
    /* 充電電流の測定 */
    float chg_v = adc(ADC_CHG_PIN);
    float bat_v = adc(ADC_BAT_PIN);
    float chg_w = chg_w = bat_v * (chg_v - bat_v) / SENSOR_R_OHM;
    Serial.print("ac="+String(int(ac)));      // AC状態を表示
    Serial.print(", chg_v="+String(chg_v,3));
    Serial.print(", bat_v="+String(bat_v,3));
    Serial.print(", chg_w="+String(chg_w,3));
    Serial.print(", mode="+String(MODE));

    /* MODE制御 */
    if(BAT_V > 14.7) MODE = 0;                  // 充電電圧の超過時に停止
    else if(BAT_V < 10.8) MODE = -2;            // 終止電圧時に停止
    else if(ac ==0 && MODE >= 0) MODE = 2;      // 放電可能かつ停電時に放電
    else if(MODE >= 0)MODE = 1;                 // 充電
    Serial.print(" -> "+String(MODE));

    /* FET制御 */
    String S;
    switch(MODE){
        case -2:                                // 故障停止(過放電)
            S = "BATTERY FAULT ";
            digitalWrite(FET_CHG_PIN, LOW);
            digitalWrite(FET_DIS_PIN, LOW);
            break;
        case -1:                                // 手動停止
            S = "STOP ";
            digitalWrite(FET_CHG_PIN, LOW);
            digitalWrite(FET_DIS_PIN, LOW);
            break;
        case 1:                                 // 充電中
            if(chg_w > -0.001) S = "Charging "; else S = "Discharging ";
            digitalWrite(FET_CHG_PIN, HIGH);
            digitalWrite(FET_DIS_PIN, HIGH);
            break;
        case 2:                                 // 停電中
            S = "POWER OUTAGE "; 
            digitalWrite(FET_CHG_PIN, LOW);
            digitalWrite(FET_DIS_PIN, HIGH);
            break;
        case 3:                                 // 測定中
            S = "Measureing ";
            break;
        case 0:                                 // 停止中
        default:                                // 未定義
            S = "Stopped ";
            digitalWrite(FET_CHG_PIN, HIGH);
            digitalWrite(FET_DIS_PIN, LOW);
            break;
    }
    Serial.println(" " + S);
    
    /* データ送信 */
    S = String(DEVICE);                         // 送信データSにデバイス名を代入
    S += String(chg_w,3) + ", ";                // 変数chg_wの値を追記
    S += String(BAT_V,3) + ", ";                // 変数BAT_Vの値を追記
    S += String(int(!ac)) + ", ";               // 変数acの反転値を追記
    S += String(MODE);                          // 変数MODEの値を追記
    Serial.println(S);                          // 送信データSをシリアル出力表示
    WiFiUDP udp;                                // UDP通信用のインスタンスを定義
    udp.beginPacket(UDPTO_IP, PORT);            // UDP送信先を設定
    udp.println(S);                             // 送信データSをUDP送信
    udp.endPacket();                            // UDP送信の終了(実際に送信する)

    HTTPClient http;                            // HTTPリクエスト用インスタンス
    http.setConnectTimeout(15000);              // タイムアウトを15秒に設定する
    http.setReuse(false);
    String url;                                 // URLを格納する文字列変数を生成
    if(!ac && strlen(LINE_TOKEN) > 42){         // LINE_TOKEN設定時
        url = "https://notify-api.line.me/api/notify";  // LINEのURLを代入
        http.begin(url);                        // HTTPリクエスト先を設定する
        http.addHeader("Content-Type","application/x-www-form-urlencoded");
        http.addHeader("Authorization","Bearer " + String(LINE_TOKEN));
        Serial.println(url);                    // 送信URLを表示
        http.POST("message=停電中です。(" + S.substring(8) + ")");
        http.end();                             // HTTP通信を終了する
        while(http.connected()) delay(100);     // 送信完了の待ち時間処理
    }
    if(strcmp(Amb_Id,"00000") != 0){            // Ambient設定時に以下を実行
        S = "{\"writeKey\":\""+String(Amb_Key); // (項目)writeKey,(値)ライトキー
        S += "\",\"d1\":\"" + String(chg_w,3);  // (項目)d1,(値)chg_w
        S += "\",\"d2\":\"" + String(BAT_V,3);  // (項目名)d2,(値)BAT_V
        S += "\",\"d3\":\"" + String(MODE>0?MODE:0); // (項目名)d3,(値)MODE
        S += "\",\"d4\":\"" + String(int(!ac)); // (項目名)d4,(値)acの反転値
        S += "\"}";
        url = "http://ambidata.io/api/v2/channels/"+String(Amb_Id)+"/data";
        http.begin(url);                        // HTTPリクエスト先を設定する
        http.addHeader("Content-Type","application/json"); // JSON形式を設定する
        Serial.println(url);                    // 送信URLを表示
        http.POST(S);                           // センサ値をAmbientへ送信する
        http.end();                             // HTTP通信を終了する
        while(http.connected()) delay(100);     // 送信完了の待ち時間処理
    }
    if(strcmp(LED_IP,"192.168.1.0")){           // 子機IPアドレス設定時
        url = "http://" + String(LED_IP) + "/?L="; // アクセス先URL
        url += String(ac ? 1 : 0);              // true時1、false時0
        http.begin(url);                        // HTTPリクエスト先を設定する
        Serial.println(url);                    // 送信URLを表示
        http.GET();                             // ワイヤレスLEDに送信する
        http.end();                             // HTTP通信を終了する
        while(http.connected()) delay(100);     // 送信完了の待ち時間処理
    }
    delay(100);                                 // 送信完了の待ち時間処理
    WiFi.disconnect();                          // Wi-Fiの切断
    sleep();
}

void sleep(){
    delay(100);                                 // 送信待ち時間
    // led_off();                               // (WS2812)LEDの消灯
    Serial.println("Sleep...");                 // 「Sleep」をシリアル出力表示
    if(FET_CHG_PIN == 4 && FET_DIS_PIN == 5){
        gpio_hold_en(GPIO_NUM_4);
        gpio_hold_en(GPIO_NUM_5);
        /** esp_err_t gpio_hold_en(gpio_num_t gpio_num);
          * @brief Enable gpio pad hold function.
          * When the pin is set to hold, the state is latched at that moment and will not change no matter how the internal
          * signals change or how the IO MUX/GPIO configuration is modified (including input enable, output enable,
          * output value, function, and drive strength values). It can be used to retain the pin state through a
          * core reset and system reset triggered by watchdog time-out or Deep-sleep events.
          * The gpio pad hold function works in both input and output modes, but must be output-capable gpios.
          * If pad hold enabled:
          *   in output mode: the output level of the pad will be force locked and can not be changed.
          * The state of the digital gpio cannot be held during Deep-sleep, and it will resume to hold at its default pin state
          * when the chip wakes up from Deep-sleep. If the digital gpio also needs to be held during Deep-sleep,
          * `gpio_deep_sleep_hold_en` should also be called.
          * Power down or call `gpio_hold_dis` will disable this function.
          * @param gpio_num GPIO number, only support output-capable GPIOs
          */
    }else{
        gpio_deep_sleep_hold_en();
        /** void gpio_deep_sleep_hold_en(void);
          * @brief Enable all digital gpio pads hold function during Deep-sleep.
          *
          * Enabling this feature makes all digital gpio pads be at the holding state during Deep-sleep. The state of each pad
          * holds is its active configuration (not pad's sleep configuration!).
          *
          * Note that this pad hold feature only works when the chip is in Deep-sleep mode. When the chip is in active mode,
          * the digital gpio state can be changed freely even you have called this function.
          *
          * After this API is being called, the digital gpio Deep-sleep hold feature will work during every sleep process. You
          * should call `gpio_deep_sleep_hold_dis` to disable this feature.
          */
    }
    esp_deep_sleep(SLEEP_P);                    // Deep Sleepモードへ移行
}

/******************************************************************************
【引用コード】
https://github.com/bokunimowakaru/esp/tree/master/2_example/example09_hum_sht31
https://github.com/bokunimowakaru/esp/tree/master/2_example/example41_hum_sht31
https://github.com/bokunimowakaru/m5s/tree/master/example04d_temp_hum_sht
https://github.com/bokunimowakaru/esp32c3/tree/master/learning/ex05_hum
*******************************************************************************/
