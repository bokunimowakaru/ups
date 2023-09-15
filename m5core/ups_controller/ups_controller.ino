/*******************************************************************************
UPS VRLA Batteries Controller

https://git.bokunimo.com/ups/

                                          Copyright (c) 2023 Wataru KUNINO
*******************************************************************************/

#include <M5Stack.h>                            // M5Stack用ライブラリの組み込み
#include <WiFi.h>                               // ESP32用WiFiライブラリ
#include <WiFiUdp.h>                            // UDP通信を行うライブラリ
#include <HTTPClient.h>                         // HTTPクライアント用ライブラリ

#define SSID "1234ABCD"                         // 無線LANアクセスポイントのSSID
#define PASS "password"                         // パスワード
#define PORT 1024                               // 送信のポート番号
#define SLEEP_P 30*1000000ul                    // スリープ時間 30秒(uint32_t)
#define DEVICE "myups_5,"                       // デバイス名(5字+"_"+番号+",")

#define FET_CHG_PIN 2                           // 充電FET GPIO 2 ピン
#define FET_DIS_PIN 5                           // 放電FET GPIO 5 ピン
#define OUTAGE_PIN 26                           // 停電検出 GPIO 26 ピン
#define ADC_CHG_PIN 35                          // 充電出力 GPIO 35 ピン(ADC1_7)
#define ADC_BAT_PIN 36                          // 電池電圧 GPIO 36 ピン(ADC1_0)
#define ADC_CHG_DIV 12./(110.+12.)              // 抵抗分圧の比率
// #define ADC_CHG_DIV 1.353 / 13.78            // 実測ADC電圧÷実測電池電圧
#define ADC_BAT_DIV 12./(110.+12.)              // 抵抗分圧の比率
// #define ADC_BAT_DIV 1.363 / 13.72            // 実測ADC電圧÷実測電池電圧
#define SENSOR_R_OHM 1.8                        // 電流センサの抵抗値(Ω)
#define MAX_CHD_CURRENT 2.0                     // 最大充電電流(  2.1 A)
#define MAX_DIS_CURRENT 2.0                     // 最大放電電流(105.0 A)

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

/* 動作モードの定義 */
#define MODE_FAULT  -2
#define MODE_STOP   -1
#define MODE_FULL    0
#define MODE_CHG     1
#define MODE_OUTAGE  2
#define MODE_MEASURE 3

int MODE = 0;       // -2:故障, -1:手動停止, 0:停止, 1:充電, 2:停電放電, 3:測定
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
            S = "BATTERY FAULT ";
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
            break;
        case MODE_STOP:                         // 手動停止(未使用)
            Chg = 0;
            Dis = 0;
            break;
        case MODE_CHG:                          // 充電中
            Chg = 1;
            Dis = 1;
            break;
        case MODE_OUTAGE:                       // 停電中
            Chg = 1;
            Dis = 1;
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
            break;
    }
    digitalWrite(FET_CHG_PIN, Chg);
    digitalWrite(FET_DIS_PIN, Dis);
    return;
}

float Prev_w = 0.;                              // 前回測定値(繰り返し判定用)
float Chg_v, Bat_v, Chg_w;

bool getChargingPower_w(){
    /* 充電／放電・電力の測定 */
    Chg_v = adc(ADC_CHG_PIN);
    Bat_v = adc(ADC_BAT_PIN);
    Chg_w = Bat_v * (Chg_v - Bat_v) / SENSOR_R_OHM;
    float diode_vf=0., fet_vds=0., chg_i=0., dis_i=0;
    Serial.print("Chg="+String(Chg));           // DEBUG
    Serial.print(", Dis="+String(Dis));         // DEBUG
    Serial.print(", Chg0_w="+String(Chg_w,3));  // DEBUG
    if(Chg_v >= Bat_v){                         // 充電時
        if(!Dis){                               // 放電がOFFのとき
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
        Serial.print(", Chg1_w="+String(chg_i * Bat_v,3)); // DEBUG
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
    }else{ // Chg_v < Bat_v                     // 放電時
        if(!Dis){                               // 放電FETがOFF
            // 原理的には電流が流れない(測定誤差はありうるが)
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
            Serial.print(", Chg1_w="+String(-dis_i * Bat_v,3)); // DEBUG
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
    Serial.println(" -> Chg2_w="+String(Chg_w,3)); // DEBUG
    if(Chg_w/Bat_v > MAX_CHD_CURRENT || Chg_w < -MAX_DIS_CURRENT){
        MODE=MODE_FAULT;                        // 故障
        setChgDisFET(MODE);                     // 故障
    }
    if(fabs(Prev_w - Chg_w) <= 0.1) return true;
    Prev_w = Chg_w;
    return false;
}

void setup(){                                   // 起動時に一度だけ実行する関数
    M5.begin();                                 // M5Stack用ライブラリの起動
    pinMode(FET_CHG_PIN, OUTPUT);               // デジタル出力に
    pinMode(FET_DIS_PIN, OUTPUT);               // デジタル出力に
    pinMode(OUTAGE_PIN, INPUT);                 // デジタル入力に
    pinMode(ADC_CHG_PIN, ANALOG);               // アナログ入力に
    pinMode(ADC_BAT_PIN, ANALOG);               // アナログ入力に
    M5.Lcd.setBrightness(31);                   // 輝度を下げる（省エネ化）
    analogMeterInit("W",-16,16,"V",10,14);      //メータ初期化
    analogMeterSetNames("Wattage","Battery");   // メータのタイトルを登録
    lineGraphInit(-16,16);                      // グラフ初期化(縦軸の範囲指定)
    M5.Lcd.println("UPS VRLA Batteries Controller");
    Serial.println("UPS VRLA Batteries Controller");
    WiFi.mode(WIFI_STA);                        // 無線LANをSTAモードに設定
}

void loop(){                                    // 繰り返し実行する関数
    M5.update();                                // ボタン状態の取得
    Ac = digitalRead(OUTAGE_PIN);               // 停電状態を確認

    delay(1);                                   // ボタンの誤作動防止
    int btn=M5.BtnA.wasPressed()+2*M5.BtnB.wasPressed()+4*M5.BtnC.wasPressed();
    switch(btn){
        case 1: MODE = -1; break;               // 停止
        case 2: MODE = 3; break;                // 測定
        case 4: MODE = 1; break;                // 充電
        default: btn = 0; break;
    }
    if(millis()%(SLEEP_P/1000) == 0 || BAT_V < 0 || MODE == 3){
        /* 電池電圧の測定 */
        if(!Ac){                                    // 停電時に
            digitalWrite(FET_CHG_PIN, LOW);             // 充電FETをOFF
        }else{                                      // 電源供給時に
            digitalWrite(FET_CHG_PIN, LOW);             // 充電FETをOFF
            digitalWrite(FET_DIS_PIN, LOW);             // 放電FETをOFF
        }
        delay(10);                              // 電圧の安定待ち
        BAT_V = adc(ADC_BAT_PIN);
        digitalWrite(FET_CHG_PIN, Chg);             // 充電FETを復帰
        digitalWrite(FET_DIS_PIN, Dis);             // 放電FETを復帰
        analogMeterNeedle(1,BAT_V);             // メータに電圧を表示

        /* MODE制御 */
        Serial.println("BAT_V="+String(BAT_V,2));
        if(BAT_V > 14.7) MODE = MODE_FULL;          // 充電電圧の超過時に充電停止
        else if(BAT_V < 10.8) MODE = MODE_FAULT;    // 終止電圧未満でに故障停止
        else if(Ac ==0 && MODE >= 0) MODE = MODE_OUTAGE; // 停電時に放電
        else if(MODE >= 0) MODE = MODE_CHG;         // 充電
        Serial.print(" -> " + String(MODE) + ": ");
        Serial.println(getChgDisMode_S(MODE));      // 測定モードを表示
        setChgDisFET(MODE);                         // モードの切り替え

        if(WiFi.status() != WL_CONNECTED){
            Serial.println("WiFi.begin");
            WiFi.begin(SSID,PASS);              // 無線LANアクセスポイントへ接続
        }
    }
    if(millis()%1000) return;                   // 以下は1秒に1回だけ実行する

    /* FET制御(電流測定モード) */
    delay(1);                                   // 切り替え待ち
    setChgDisFET(MODE_MEASURE);                 // 測定モードに切り替え
    delay(10);                                  // 切り替え待ち

    /* 電力測定 */
    while(!getChargingPower_w()) delay(50);
    setChgDisFET(MODE);                         // 現在のモードに設定
    Serial.print("ac="+String(int(Ac)));        // AC状態を表示
    Serial.print(", Chg_v="+String(Chg_v,3));
    Serial.print(", Bat_v="+String(Bat_v,3));
    Serial.print(", Chg_w="+String(Chg_w,3));
    Serial.println(", mode="+String(MODE));

    /* 描画 */
    String S = getChgDisMode_S(MODE)+" "+String(Chg)+" "+String(Dis);
    M5.Lcd.fillRect(283, 194, 37, 8, BLACK);    // Wi-Fi接続の待ち時間
    M5.Lcd.setCursor(283, 194);                 // 文字位置を設定
    M5.Lcd.printf("(%d) ",WiFi.status());        // Wi-Fi状態番号を表示
    M5.Lcd.print((SLEEP_P/1000 - millis()%(SLEEP_P/1000))/1000);
    analogMeterNeedle(0,Chg_w);                 // メータに電力を表示
    lineGraphPlot(Chg_w);                       // 電力をグラフ表示
    if(MODE < 0 || MODE == MODE_OUTAGE){        // モード -1,-2,2のとき
        M5.Lcd.fillRect(0,210, 320,30,TFT_RED); // 表示部の背景を塗る
    }else{
        M5.Lcd.fillRect(0,210, 320,30, BLACK);  // 表示部の背景を塗る
    }
    M5.Lcd.drawCentreString(S, 160, 212, 4);    // 文字列を表示
    if(WiFi.status() != WL_CONNECTED) return;   // Wi-Fi未接続のときに戻る

    /* データ送信 */
    M5.Lcd.fillRect(0, 202, 320, 8, BLACK);     // 表示部の背景を塗る
    M5.Lcd.setCursor(0, 202);                   // 文字位置を設定
    M5.Lcd.println(WiFi.localIP());             // 本機のアドレスをLCDに表示

    S = String(DEVICE);                         // 送信データSにデバイス名を代入
    S += String(Chg_w,3) + ", ";                // 変数chg_wの値を追記
    S += String(BAT_V,3) + ", ";                // 変数BAT_Vの値を追記
    S += String(int(!Ac)) + ", ";               // 変数Acの反転値を追記
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
    if(!Ac && strlen(LINE_TOKEN) > 42){         // LINE_TOKEN設定時
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
        S += "\",\"d1\":\"" + String(Chg_w,3);  // (項目)d1,(値)chg_w
        S += "\",\"d2\":\"" + String(BAT_V,3);  // (項目名)d2,(値)BAT_V
        S += "\",\"d3\":\"" + String(MODE>0?MODE:0); // (項目名)d3,(値)MODE
        S += "\",\"d4\":\"" + String(int(!Ac)); // (項目名)d4,(値)acの反転値
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
}

/******************************************************************************
【参考文献】
Arduino IDE 開発環境イントール方法：
https://docs.m5stack.com/en/quick_start/m5core/arduino

M5Stack Arduino Library API 情報：
https://docs.m5stack.com/en/api/core/system

【引用コード】
https://github.com/bokunimowakaru/esp/tree/master/2_example/example09_hum_sht31
https://github.com/bokunimowakaru/esp/tree/master/2_example/example41_hum_sht31
https://github.com/bokunimowakaru/m5s/tree/master/example04d_temp_hum_sht
https://github.com/bokunimowakaru/esp32c3/tree/master/learning/ex05_hum
*******************************************************************************/
