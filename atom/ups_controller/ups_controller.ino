/*******************************************************************************
UPS VRLA Batteries Controller for ESP32 / ATOM / ATOM Lite
********************************************************************************

Usage and Design Information:
    https://git.bokunimo.com/ups/

Attention:
    The values of definition ADC_CHG_DIV and ADC_BAT_DIV need to be adjusted.

WARNING:
    I do not take any responsibility for safety.
    For example, mishandling batteries may result in danger to your life or
    loss of your property.

ご注意:
    安全性に関して当方は一切の責任を負いません。
    仮に電池の誤制御によって損害が発生した場合であっても補償いたしません。

CSVxUDP Format:
    myups_5, charging(W), battery(V), Outage, UPS mode⏎(\r\n)

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

#define PIN_LED_RGB 27                          // 内蔵 RGB LED
#define FET_CHG_PIN 25                          // 充電FET G25 
#define FET_DIS_PIN 26                          // 放電FET G26 (Grove)
#define OUTAGE_PIN 21                           // 停電検出 G21
#define ADC_CHG_PIN 32                          // 充電出力 G32 ADC1_4
#define ADC_BAT_PIN 33                          // 電池電圧 G33 ADC1_5(Grove)
#define ADC_CHG_DIV 12./(110.+12.)              // 抵抗分圧の比率
// #define ADC_CHG_DIV 1.353 / 13.78            // 実測ADC電圧÷実測電池電圧
#define ADC_BAT_DIV 12./(110.+12.)              // 抵抗分圧の比率
// #define ADC_BAT_DIV 1.363 / 13.72            // 実測ADC電圧÷実測電池電圧
#define SENSOR_R_OHM 1.8                        // 電流センサの抵抗値(Ω)
#define MAX_CHD_CURRENT 2.0                     // 最大充電電流(  2.1 A)
#define MAX_DIS_CURRENT 2.0                     // 最大放電電流(105.0 A)
#define MAX_TOLERANCE_W 0.1                     // 電力測定の許容偏差(W)
#define ALG_MAX_V 14.2                          // 電池の最大電圧(異常検出用)
#define ALG_MIN_V 10.8                          // 電池の終止電圧(異常検出用)

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
 動作モードの定義
 *****************************************************************************/
#define MODE_FAULT  -2
#define MODE_STOP   -1
#define MODE_FULL    0
#define MODE_CHG     1
#define MODE_OUTAGE  2
#define MODE_MEASURE 3

/******************************************************************************
 main
 *****************************************************************************/

RTC_DATA_ATTR int line_stat = 0; // LINEへの送信状態
                    // 0:未送信 1:停電送信済み -2:故障送信済み -1:停止送信済
RTC_DATA_ATTR int MODE = 0;
                    // -2:故障, -1:手動停止, 0:停止, 1:充電, 2:停電放電, 3:測定

float BAT_V = -0.1; // 電池電圧の測定結果
bool Ac;            // ACアダプタからの電源供給状態
bool Chg, Dis;      // 充電FETと放電FETの状態または制御値

float adc(int pin){
    delay(10);
    float div = 0.;
    if(pin == ADC_CHG_PIN) div = ADC_CHG_DIV;
    if(pin == ADC_BAT_PIN) div = ADC_BAT_DIV;
    if(div == 0.) return 0.;
    return analogReadMilliVolts(pin) / div / 1000.;
}

String getChgDisMode_S(int mode){
    String S;
    switch(mode){
        case MODE_FAULT:                        // 故障停止(過放電)
            S = "BATTERY EXHAUSTION ";
            break;
        case MODE_STOP:                         // 手動停止(未使用)
            S = "STOP ";
            break;
        case MODE_CHG:                          // 充電中
            S = "Charging ";
            break;
        case MODE_OUTAGE:                       // 停電中
            S = "POWER OUTAGE "; 
            break;
        case MODE_MEASURE:                      // 測定中
            S = "Measureing ";
            break;
        case MODE_FULL:                         // 満充電
        default:
            S = "Fully charged ";
            break;
    }
    return S;
}

void setChgDisFET(int mode){
    switch(mode){
        case MODE_FAULT:                        // 故障停止(過放電)
            Chg = 0;
            Dis = 0;
            led(20,0,0);                        // (WS2812)LEDを赤色に
            break;
        case MODE_STOP:                         // 手動停止(未使用)
            Chg = 0;
            Dis = 0;
            led(20,0,0);                        // (WS2812)LEDを赤色に
            break;
        case MODE_CHG:                          // 充電中
            Chg = 1;
            Dis = 1;
            led(0,20,0);                        // (WS2812)LEDを緑色に
            break;
        case MODE_OUTAGE:                       // 停電中
            Chg = 1;
            Dis = 1;
            led(10,10,0);                       // (WS2812)LEDを黄色に
            break;
        case MODE_MEASURE:                      // 測定中
            if(MODE >0){
                Chg = 1;
                Dis = 1;
            }
            break;
        case MODE_FULL:                         // 満充電
        default:
            Chg = 0;
            Dis = 1;
            led(20,0,20);                       // (WS2812)LEDを紫色に
            break;
    }
    digitalWrite(FET_CHG_PIN, Chg);
    digitalWrite(FET_DIS_PIN, Dis);
    return;
}

int algoModeControl(){
    int mode = MODE;
    /* MODE制御 */
    Ac = digitalRead(OUTAGE_PIN);               // 停電状態を確認
    Serial.print("algoModeControl: AC="+String(Ac)+", BAT_V="+String(BAT_V,2));
    if(BAT_V > ALG_MAX_V) mode = MODE_FULL;     // 充電電圧の超過時に充電停止
    else if(BAT_V < ALG_MIN_V) mode = MODE_FAULT; // 終止電圧未満でに故障停止
    else if(Ac == 0 && MODE >= 0) mode = MODE_OUTAGE; // 停電時に放電
    else if(MODE >= 0) mode = MODE_CHG;         // 充電
    Serial.print(", Mode=" + String(MODE));
    Serial.print(" -> " + String(mode) + ": ");
    Serial.println(getChgDisMode_S(mode));      // 測定モードを表示
    return mode;
}

float Prev_w = 0.;                              // 前回測定値(繰り返し判定用)
float Chg_v, Bat_v, Chg_w;

bool getChargingPower_w(){                      // 測定の実行,応答=安定状態
    /* 充電／放電・電力の測定 */
    Chg_v = adc(ADC_CHG_PIN);
    Bat_v = adc(ADC_BAT_PIN);
    Chg_w = Bat_v * (Chg_v - Bat_v) / SENSOR_R_OHM;
    float diode_vf=0., fet_vds=0., chg_i=0., dis_i=0;
    Serial.print("getChargingPower: Chg_v="+String(Chg)); // DEBUG
    Serial.print("*"+String(Chg_v,2));          // DEBUG
    Serial.print(", Dis_v="+String(Dis));       // DEBUG
    Serial.print("*"+String(Bat_v,2));          // DEBUG
    Serial.print(", C1_w="+String(Chg_w,3));    // DEBUG
    
    if(Chg_v >= Bat_v){                         // 充電時
        if(!Chg){                               // 充電FETがOFF
            // 電流が流れない一方で電位差が生じるので0に強制する
            Chg_w = 0.;
        }else{
            if(!Dis){                           // 放電がOFFのとき
                // DisがOFFのときはFETの逆電圧防止DのVF分の電圧降下が生じる
                // VF=0.5として概算電流chg_iを求める
                chg_i = (Chg_v - Bat_v - 0.5) / SENSOR_R_OHM;
                if(chg_i <0.) chg_i = 0.001;
                // 概算電流値chg_iからVFを求める
                // IRFU9024NPBF 1A:0.76V 0.1A:0.69V (Aは対数)
                //      b = 0.76
                //      0.69 = a * -1 + 0.76    -> a = 0.07
                //      ∴VF = 0.07 * log10(chg_i) + 0.76
                diode_vf = 0.07 * log10(chg_i) + 0.76;
                Serial.print(", vf="+String(diode_vf,2)); // DEBUG
                if(diode_vf <0.) diode_vf = 0.;
            }
            // ダイオードのVFを考慮した電流値を求める
            chg_i = (Chg_v - Bat_v - diode_vf) / SENSOR_R_OHM;
            Serial.print(", C2_w="+String(chg_i * Bat_v,3)); // DEBUG
            // 概算電流値chg_iからVDSを求める
            // IRFU9024NPBF 1A:0.2V 0.44A:0.1V (両対数)
            //               0:-0.699  -0.357:-1
            //      b = -0.699
            //      -1 = -0.357 * a - 0.699 -> a = (1 - 0.699) / 0.357 = 0.844
            //      ∴VDS = 10^( 0.844 * log10(dis_i) - 0.699)
            fet_vds = pow(10, 0.844 * log10(chg_i) - 0.699);
            // DisがONのときは2つのFETで電圧降下が生じる
            if(Dis) fet_vds *= 2.;
            Serial.print(", vds="+String(fet_vds,2)); // DEBUG
            // 算出したdiode_vfとfet_vdsから正確な電力値を求める
            if(Chg_v - Bat_v - diode_vf - fet_vds > 0){   // 充電が正の時
                Chg_w = Bat_v * (Chg_v - Bat_v - diode_vf - fet_vds) / SENSOR_R_OHM;
            }else{
                Chg_w = 0.;
            }
        }
    }else{ // Chg_v < Bat_v                     // 放電時
        if(!Dis){                               // 放電FETがOFF
            // 電流が流れない一方で電位差が生じるので0に強制する
            Chg_w = 0.;
        }else{
            if(!Chg){                           // 充電FETがOFF(外部ダイオード)
                // VF=0.5として概算電流dis_iを求める
                dis_i = (Bat_v - Chg_v - 0.5) / SENSOR_R_OHM;
                if(dis_i < 0.) dis_i = 0.001;
                // 概算電流値dis_iからVFを求める
                // 11EQS04 1A:0.53V 0.1A:0.37V (Aは対数)
                //      0.53 = a * 0 + b        -> b = 0.53
                //      0.37 = a * -1 + 0.53    -> a = 0.16
                //      ∴VF = 0.16 * log10(dis_i) + 0.53
                diode_vf = 0.16 * log10(dis_i) + 0.53;
                Serial.print(", vf="+String(diode_vf,2)); // DEBUG
                if(diode_vf <0.) diode_vf = 0.;
            }
            // ダイオードのVFを考慮した電流値を求める
            dis_i = (Bat_v - Chg_v - diode_vf) / SENSOR_R_OHM;
            Serial.print(", C2_w="+String(-dis_i * Bat_v,3)); // DEBUG
            // 概算電流値dis_iからVDSを求める
            // IRFU9024NPBF 1A:0.2V 0.44A:0.1V (両対数)
            //               0:-0.699  -0.357:-1
            //      b = -0.699
            //      -1 = -0.357 * a - 0.699 -> a = (1 - 0.699) / 0.357 = 0.844
            //      ∴VDS = 10^( 0.844 * log10(dis_i) - 0.699)
            fet_vds = pow(10, 0.844 * log10(dis_i) - 0.699);
            // ChgがONのときは2つのFETで電圧降下が生じる
            if(Chg) fet_vds *= 2.;
            Serial.print(", vds="+String(fet_vds,2)); // DEBUG
            // 算出したdiode_vfとfet_vdsから正確な電力値を求める
            if(Bat_v - Chg_v - diode_vf - fet_vds > 0){   // 放電が正の時
                Chg_w = -Bat_v * (Bat_v - Chg_v - diode_vf - fet_vds)
                      / SENSOR_R_OHM;
            }else{
                Chg_w = 0.;
            }
        }
    }
    Serial.println(" -> C3_w="+String(Chg_w,3)); // DEBUG
    if(Chg_w/Bat_v > MAX_CHD_CURRENT || Chg_w/Bat_v < -MAX_DIS_CURRENT){
        MODE=MODE_FAULT;                        // 故障
    }
    if(fabs(Prev_w - Chg_w) <= MAX_TOLERANCE_W) return true;
    Prev_w = Chg_w;
    return false;
}

float getBatteryVoltage_v(){
    /* 電池電圧の測定 */
    Ac = digitalRead(OUTAGE_PIN);               // 停電状態を確認
    if(!Ac){                                    // 停電時の処理
        led(10,10,0);                           // (WS2812)LEDを黄色で点灯
        // 停電と電圧測定時が重なると、充電FETの逆流ダイオードの電圧降下によって
        // 完全に電源喪失する場合があるかもしれません。もし、外部にダイオードを
        // 追加しても改善できない場合は、次の行を削除してください。
        // ただし、削除すると電池電圧測定時に電池内部抵抗の影響を受けます。
        digitalWrite(FET_CHG_PIN, LOW);         // 充電FETをOFF
    }else{                                      // 電源供給時に
        led(0,20,0);                            // (WS2812)LEDを緑色で点灯
        digitalWrite(FET_CHG_PIN, LOW);         // 充電FETをOFF
        // Setting the FET_DIS_PIN of the discharge FET to LOW in the line below
        // increases the accuracy of the voltage measurement. But it may be
        // caused to lose all power during measurement in an outage condition.
        // 下記の放電FETのOFFを有効にすると電圧測定の正確性が増しますが、
        // 測定中に停電したときに完全に電源喪失する場合があります。
    //  digitalWrite(FET_DIS_PIN, LOW);         // 放電FETをOFF
    }
    delay(2);                                   // 電圧の安定待ち
    float bat_v = adc(ADC_BAT_PIN);
    digitalWrite(FET_CHG_PIN, Chg);             // 充電FETを復帰
    digitalWrite(FET_DIS_PIN, Dis);             // 放電FETを復帰
    return bat_v;
}

void sendToLine(HTTPClient &http, String message){
    String url = "https://notify-api.line.me/api/notify"; // LINEのURLを代入
    http.begin(url);                            // HTTPリクエスト先を設定する
    http.addHeader("Content-Type","application/x-www-form-urlencoded");
    http.addHeader("Authorization","Bearer " + String(LINE_TOKEN));
    Serial.println(url);                        // 送信URLを表示
    http.POST("message=" + message);
    http.end();                                 // HTTP通信を終了する
    while(http.connected()) delay(100);         // 送信完了の待ち時間処理
}

void setup(){                                   // 起動時に一度だけ実行する関数
    Chg = digitalRead(FET_CHG_PIN);
    Dis = digitalRead(FET_DIS_PIN);
    pinMode(FET_CHG_PIN, OUTPUT);               // 充電FETをデジタル出力に
    digitalWrite(FET_CHG_PIN, Chg);             // 充電FETを復帰
    pinMode(FET_DIS_PIN, OUTPUT);               // 放電FETをデジタル出力に
    digitalWrite(FET_DIS_PIN, Dis);             // 放電FETを復帰
    if(FET_CHG_PIN == 25 && FET_DIS_PIN == 26){
        gpio_hold_dis(GPIO_NUM_25);
        gpio_hold_dis(GPIO_NUM_26);
    }else{
        gpio_deep_sleep_hold_dis();
    }
    led_setup(PIN_LED_RGB);                     // WS2812の初期設定(ポート設定)
    pinMode(OUTAGE_PIN, INPUT);                 // 停電検出をデジタル入力に
    pinMode(ADC_CHG_PIN, ANALOG);               // 充電側電圧をアナログ入力に
    pinMode(ADC_BAT_PIN, ANALOG);               // 電池側電圧をアナログ入力に
    Serial.begin(115200);                       // 動作確認のためのシリアル出力
    Serial.println("UPS VRLA Batteries Controller");
    WiFi.mode(WIFI_STA);                        // 無線LANをSTAモードに設定
    Serial.println("WiFi.begin");
    WiFi.begin(SSID,PASS);                      // 無線LANアクセスポイントへ接続
    
    BAT_V = getBatteryVoltage_v();              // 電池電圧を取得

    /* MODE制御 */
    MODE = algoModeControl();                   // モード値の変更

    /* FET制御(電流測定モード) */
    delay(1);                                   // 切り替え待ち
    setChgDisFET(MODE_MEASURE);                 // 測定モードに切り替え
    delay(10);                                  // 切り替え待ち
}

void loop(){                                    // 繰り返し実行する関数
    /* 電力測定 */
    while(!getChargingPower_w() && WiFi.status()!=WL_CONNECTED){
        led((millis()/50) % 10);                // (WS2812)LEDの点滅
        if(millis() > 10000){                   // 10秒超過
            setChgDisFET(MODE);                 // 現在のモードに設定
            sleep();                            // スリープ
        }
        delay(50);                              // 待ち時間処理
    }
    /*
    Serial.print("ac="+String(int(Ac)));        // AC状態を表示
    Serial.print(", Chg_v="+String(Chg_v,3));
    Serial.print(", Bat_v="+String(Bat_v,3));
    Serial.print(", Chg_w="+String(Chg_w,3));
    Serial.println(", mode="+String(MODE));
    */
    
    /* データ送信 */
    String S = String(DEVICE);                  // 送信データSにデバイス名を代入
    S += String(Chg_w,3) + ", ";                // 変数Chg_wの値を追記
    S += String(BAT_V,3) + ", ";                // 変数BAT_Vの値を追記
    S += String(int(!Ac)) + ", ";               // 変数acの反転値を追記
    S += String(MODE);                          // 変数MODEの値を追記
    Serial.println(S);                          // 送信データSをシリアル出力表示
    WiFiUDP udp;                                // UDP通信用のインスタンスを定義
    udp.beginPacket(UDPTO_IP, PORT);            // UDP送信先を設定
    udp.println(S);                             // 送信データSをUDP送信
    udp.endPacket();                            // UDP送信の終了(実際に送信する)

    HTTPClient http;                            // HTTPリクエスト用インスタンス
    http.setConnectTimeout(15000);              // タイムアウトを15秒に設定する
    http.setReuse(false);
    if(strlen(LINE_TOKEN) > 42){                // LINE_TOKEN設定時
        if(!Ac && !line_stat){                  // 停電時
            sendToLine(http, "停電中です。(" + S.substring(8) + ")");
            line_stat = 1;                      // 停電中を送信済み
        }else if(Ac && line_stat == 1){
            sendToLine(http, "復電しました。(" + S.substring(8) + ")");
            line_stat = 0;                      // 停電中を送信済み
        }
        if(MODE == MODE_FAULT && !line_stat){
            sendToLine(http, "故障中です。(" + S.substring(8) + ")");
            line_stat = MODE_FAULT;             // 故障中を送信済み
        }else if(line_stat == MODE_FAULT){
            sendToLine(http, "復帰しました。(" + S.substring(8) + ")");
            line_stat = 0;                      // 故障復帰を送信済み
        }
        if(MODE == MODE_STOP && !line_stat){
            sendToLine(http, "手動停止中です。(" + S.substring(8) + ")");
            line_stat = MODE_STOP;              // 故障中を送信済み
        }else if(line_stat == MODE_STOP){
            sendToLine(http, "再開しました。(" + S.substring(8) + ")");
            line_stat = 0;                      // 故障復帰を送信済み
        }
    }
    String url;                                 // URLを格納する文字列変数を生成
    if(strcmp(Amb_Id,"00000") != 0){            // Ambient設定時に以下を実行
        S = "{\"writeKey\":\""+String(Amb_Key); // (項目)writeKey,(値)ライトキー
        S += "\",\"d1\":\"" + String(Chg_w,3);  // (項目)d1,(値)Chg_w
        S += "\",\"d2\":\"" + String(BAT_V,3);  // (項目名)d2,(値)BAT_V
        S += "\",\"d3\":\"" + String(int(!Ac)); // (項目名)d3,(値)acの反転値
        S += "\",\"d4\":\"" + String(MODE);     // (項目名)d4,(値)MODE
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
        url += String(Ac ? 1 : 0);              // true時1、false時0
        http.begin(url);                        // HTTPリクエスト先を設定する
        Serial.println(url);                    // 送信URLを表示
        http.GET();                             // ワイヤレスLEDに送信する
        http.end();                             // HTTP通信を終了する
        while(http.connected()) delay(100);     // 送信完了の待ち時間処理
    }
    delay(100);                                 // 送信完了の待ち時間処理
    WiFi.disconnect();                          // Wi-Fiの切断
    setChgDisFET(MODE);                         // 現在のモードに設定
    sleep();
}

void sleep(){
    delay(100);                                 // 送信待ち時間
    // led_off();                               // (WS2812)LEDの消灯
    Serial.println("Sleep...");                 // 「Sleep」をシリアル出力表示
    if(FET_CHG_PIN == 25 && FET_DIS_PIN == 26){
        gpio_hold_en(GPIO_NUM_25);
        gpio_hold_en(GPIO_NUM_26); /* esp_err_t gpio_hold_en(gpio_num_t gpio_num)
          * @brief Enable gpio pad hold function.
          * When the pin is set to hold, the state is latched at that moment and
          * will not change no matter how the internal signals change or how the
          * IO MUX/GPIO configuration is modified (including input enable,
          * output enable, output value, function, and drive strength values).*/
    }else{
        gpio_deep_sleep_hold_en(); /* void gpio_deep_sleep_hold_en(void)
          * @brief Enable all digital gpio pads hold function during Deep-sleep.
          * Enabling this feature makes all digital gpio pads be at the holding
          * state during Deep-sleep. The state of each pad holds is its active 
          * configuration (not pad's sleep configuration!). */
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
