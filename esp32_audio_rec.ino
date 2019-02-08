#include "CloudSpeechClient.h"
#include "I2S.h"
#include "time.h"
#include "esp_system.h"
#include <WebServer.h>
#include <Preferences.h>

const IPAddress apIP(192, 168, 4, 1);
const char* apSSID = "ESP32SETUP";
boolean settingMode = false;
String ssidList1;
String ssidList2;
//Preferences 的参数重烧固件会仍会存在！


//配置参数：存在固件里，可修改
String set_index = ""; //索引：wifi
String speak_mode = "" ; //是否调用wifi 喇叭 1无 2开关灯 3语音tulin
String report_mode = ""; //是否将识别的文字报告出去 1否 2是
String report_address = "";
String report_url = "";

String baidu_key;
String baidu_secert;  //注意：名称过长会有问题！！！
String tulin_api;

String volume_low; //静音音量值
String volume_high ; //噪音音量值
String volume_double ; //音量乘以倍数

String define_max1 ;
String define_avg1 ;
String define_zero1 ;

String define_max2 ;
String define_avg2;
String define_zero2 ;

String wifi_ssid1 ;
String  wifi_password1  ;
String speak_address1  ;
String speak_led_on1  ;
String speak_led_off1 ;
String speak_tulin1 ;

String wifi_ssid2  ;
String wifi_password2  ;
String speak_address2  ;
String speak_led_on2  ;
String speak_led_off2  ;
String speak_tulin2  ;

WebServer webServer(80);
Preferences preferences;

//程序功能：声音监听并文字识别，将识别的文字可转发到其它设备上，如树莓派，连接有speaker的esp32等
//         平均文字识别时间3-25秒不等，看网络情况
//硬件要求：自带4M psram esp32 淘宝上约55元左右 + INMP441全向麦克风模块 淘宝上约18元左右 
//          接线参考：https://github.com/MhageGH/esp32_SoundRecorder
//本版本需要 esp32自带psram , 编译时需要 PSRAM 设置成 enabled ，否则运行时会无限重启!!!
//一般的esp32会有4M SPISSF, SPIFFS因为读写速度的原因,在录制声音文件时会拖慢声音文件50%的速度,无法达到足够的声音识别率
//如果想外接sd卡，必须到少10倍速的，4倍速的TF卡已确认保存速度不行,软件代码需要修改调整，不难
//INMP441-ESP接线定义见I2S.h
// SCK IO26
// WS  IO33
// SD  IO34
// L/R GND
//
//LED指示灯-ESP32接线定义 用于对路由器连接，声音识别状态显示，可以不接
// 正极 IO27
// 负极 GND
//
//使用说明：
//  1.arduino1.8.7软件，安装esp32的官方开发包，开发板选择 ESP Dev Module, PSRAM 设置成 enabled  连接esp32烧写固件
//  2.将INMP441 麦克风模块按上面的配置用线连接到esp32
//  3.上电运行，首次运行时需要配置esp32连接路由器的参数，esp32会自动进入路由器模式创建一个ESP32SETUP的路由器，电脑连接此路由输入http://192.168.4.1进行配置
//  4.第3步的路由器配置好后，语音识别才可以正常运行
//  注意：
//  # baidu_key  baidu_secert 这两个参数用到了百度语音服务,需要到如下网址注册获取
//  # http://yuyin.baidu.com/
//
//软件运行原理
//  esp上电后开始读取声音信号，当检测到声音后自动转入录音模式
//  当检查到无声音或声音达到20秒后停止录音，将声音数据传给百度服务进行语音转文字
//  如果识别到文字，将文字传给配置好的外部设备
//  esp32受限于内存和库的原因，这里只开发了语音转文字，并将文字发送出去的功能，更多的功能不适合在esp32上直接处理
//
//用电
//5v 100ma电流
//声音数据: 16khz 16位 wav数据，经测试，此数据百度文字识别效果会较好  8khz 8位wav识别效果会很差
//
//其它
//   1.软件编写中照抄了网上很多别人代码，一般尽量原样引用，因为是自已练习用，应该不会有太大问题
//   2.英文不好，代码注释中文为主
//   3.偶尔会发现esp32自动重启，发生时一般在esp32连接wifi的代码附件，怀疑是电流不够造成
//   供电充足时出现机率较低，很快恢复录音状态，影响不大
//   4.受限于算法，没法支持唤醒词，采用声音高峰值及声音平均值做为判断声音开始，调用百度语音服务次数较多
//   好在目前百度文字识别服务不限次数，问题不大。
//   5.为避免程序死掉，使用了看门狗技术，如果15分钟卡在某处，自动重启。
//   6.esp32连续上电连续运行1个月左右，未发现问题，使用中路由器断网等均能自动恢复
//
//15分钟watchdog
const int wdtTimeout = 15 * 60 * 1000; //设置秒数 watchdog
hw_timer_t *timer = NULL;
//int speaker = 0;
const char* ntpServer = "ntp1.aliyun.com";
const long  gmtOffset_sec = 3600 * 8;
const int   daylightOffset_sec = 0;
long  last_check = 0;
const int record_time = 10;  // 录音秒数
const int waveDataSize = record_time * 32000 ;
const int numCommunicationData = 8000;
//数组：8000字节缓冲区
char communicationData[numCommunicationData];   //1char=8bits 1byte=8bits 1int=8*2 1long=8*4
long writenum = 0;
bool sd_ok = false;
struct tm timeinfo;

const int led = 27;
CloudSpeechClient* cloudSpeechClient;


void writeparams()
{
  Serial.println("Writing params to EEPROM...");

  printparams();

  preferences.putString("set_index", set_index);
  preferences.putString("speak_mode", speak_mode);

  preferences.putString("report_mode", report_mode);
  preferences.putString("report_address", report_address);
  preferences.putString("report_url", report_url);


  preferences.putString("baidu_key", baidu_key);

  Serial.println("putString baidu_secert: " + baidu_secert);

  preferences.putString("baidu_secert", baidu_secert);
  preferences.putString("tulin_api", tulin_api);

  preferences.putString("volume_low", volume_low);
  preferences.putString("volume_high", volume_high);
  preferences.putString("volume_double", volume_double);


  preferences.putString("define_max1", define_max1);
  preferences.putString("define_avg1", define_avg1);
  preferences.putString("define_zero1", define_zero1);
  preferences.putString("define_max2", define_max2);
  preferences.putString("define_avg2", define_avg2);
  preferences.putString("define_zero2", define_zero2);

  preferences.putString("wifi_ssid1", wifi_ssid1);
  preferences.putString("wifi_password1", wifi_password1);
  preferences.putString("speak_address1", speak_address1);
  preferences.putString("speak_led_on1", speak_led_on1);
  preferences.putString("speak_led_off1", speak_led_off1);
  preferences.putString("speak_tulin1", speak_tulin1);

  preferences.putString("wifi_ssid2", wifi_ssid2);
  preferences.putString("wifi_password2", wifi_password2);
  preferences.putString("speak_address2", speak_address2);
  preferences.putString("speak_led_on2", speak_led_on2);
  preferences.putString("speak_led_off2", speak_led_off2);
  preferences.putString("speak_tulin2", speak_tulin2);
  Serial.println("Writing params done!");
}

bool readparams()
{
  set_index = preferences.getString("set_index");

  //如果这个值还没有，说明没有配置过，给个默认
  if (set_index == "")
  {
    Serial.println("首次运行，配置默认值");
    set_index = "1";
    speak_mode = "1";
    report_mode = "2";

    report_address = "192.168.1.20";
    report_url = "http://192.168.1.20:1990/method=info&txt=>>";
    baidu_key = "";
    baidu_secert =  ""; //注意：变量名称过长会有问题！！！

    volume_low = "15"; //静音音量值
    volume_high = "5000"; //噪音音量值
    volume_double = "40"; //音量乘以倍数

    define_max1 = "150";
    define_avg1 = "10";
    define_zero1 =  "3000";
    define_max2 = "120";
    define_avg2 =  "8";
    define_zero2 =  "2500";
    tulin_api = "";
    wifi_ssid1 = "CMCC-r3Ff";
    wifi_password1 = "9999900000";
    speak_address1 = "192.168.1.40";
    speak_led_on1 = "http://192.168.1.40:8080/led?show=on";
    speak_led_off1 = "http://192.168.1.40:8080/led?show=off";
    speak_tulin1 = "http://192.168.1.40:8080/voice?tulin=";

    wifi_ssid2 =  "tao";
    wifi_password2 = "9999900000";
    speak_address2 = "10.1.199.140";
    speak_led_on2 = "http://10.1.199.140:8080/led?show=on";
    speak_led_off2 = "http://10.1.199.140:8080/led?show=off";
    speak_tulin2 = "http://10.1.199.140:8080/voice?tulin=";

    writeparams();
    printparams();
    return false;
  }

  speak_mode = preferences.getString("speak_mode");
  report_mode = preferences.getString("report_mode");
  report_address = preferences.getString("report_address");
  report_url =  preferences.getString("report_url");
  baidu_key = preferences.getString("baidu_key");
  baidu_secert = preferences.getString("baidu_secert");

  volume_low = preferences.getString("volume_low");
  volume_high = preferences.getString("volume_high");
  volume_double = preferences.getString("volume_double");
  define_max1 = preferences.getString("define_max1");
  define_avg1 = preferences.getString("define_avg1");
  define_zero1 = preferences.getString("define_zero1");
  define_max2 = preferences.getString("define_max2");
  define_avg2 = preferences.getString("define_avg2");
  define_zero2 = preferences.getString("define_zero2");
  tulin_api = preferences.getString("tulin_api");
  wifi_ssid1 = preferences.getString("wifi_ssid1");
  wifi_password1 = preferences.getString("wifi_password1");
  speak_address1 = preferences.getString("speak_address1");
  speak_led_on1 = preferences.getString("speak_led_on1");
  speak_led_off1 = preferences.getString("speak_led_off1");
  speak_tulin1 = preferences.getString("speak_tulin1");

  wifi_ssid2 = preferences.getString("wifi_ssid2");
  wifi_password2 = preferences.getString("wifi_password2");
  speak_address2 = preferences.getString("speak_address2");
  speak_led_on2 = preferences.getString("speak_led_on2");
  speak_led_off2 = preferences.getString("speak_led_off2");
  speak_tulin2 = preferences.getString("speak_tulin2");

  printparams();
  return true;
}

void printparams()
{
  return;

  Serial.println(" set_index: " + set_index);
  Serial.println(" speak_mode: " + speak_mode);
  Serial.println(" report_mode: " + report_mode);
  Serial.println(" report_address: " + report_address);
  Serial.println(" report_url: " + report_url);
  Serial.println(" baidu_key: " + baidu_key);
  Serial.println(" baidu_secert: " + baidu_secert);
  Serial.println(" tulin_api: " + tulin_api);



  Serial.println(" volume_low: " + volume_low);
  Serial.println(" volume_high: " + volume_high);
  Serial.println(" volume_double: " + volume_double);

  Serial.println(" define_max1: " + define_max1);
  Serial.println(" define_avg1: " + define_avg1);
  Serial.println(" define_zero1: " + define_zero1);

  Serial.println(" define_max2: " + define_max2);
  Serial.println(" define_avg2: " + define_avg2);
  Serial.println(" define_zero2: " + define_zero2);

  Serial.println(" wifi_ssid1: " + wifi_ssid1);
  Serial.println(" wifi_password1: " + wifi_password1);

  Serial.println(" speak_address1: " + speak_address1);
  Serial.println(" speak_led_on1: " + speak_led_on1);
  Serial.println(" speak_led_off1: " + speak_led_off1);
  Serial.println(" speak_tulin1: " + speak_tulin1);

  Serial.println(" wifi_ssid2: " + wifi_ssid2);
  Serial.println(" wifi_password2: " + wifi_password2);
  Serial.println(" speak_address2: " + speak_address2);
  Serial.println(" speak_led_on2: " + speak_led_on2);
  Serial.println(" speak_led_off2: " + speak_led_off2);
  Serial.println(" speak_tulin2: " + speak_tulin2);

}



void IRAM_ATTR resetModule() {
  ets_printf("reboot\n");
  esp_restart_noos();
}

String GetLocalTime()
{
  String timestr = "";
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return (timestr);
  }
  timestr = String(timeinfo.tm_mon + 1) + "-" + String(timeinfo.tm_mday) + " " +
            String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec) ;
  return (timestr);
}

void flash_led()
{
  led_power(1);
  delay(500);
  led_power(0);
  delay(500);
  led_power(1);
  delay(500);
  led_power(0);
  delay(500);
  led_power(1);
  delay(500);
  led_power(0);
}


//1亮 0灭
void led_power(int flag)
{
  if (flag == 1)
    digitalWrite(led, HIGH);
  else
    digitalWrite(led, LOW);
}

int16_t max(int16_t a, int16_t b)
{
  if (a > b) return a;
  else return b;
}

int16_t min(int16_t a, int16_t b)
{
  if (b > a) return a;
  else return b;
}

//每半秒一次检测噪音
bool wait_loud()
{

  led_power(0);
  String timelong_str = "";
  float val_avg = 0;
  int16_t val_max = 0;
  int32_t all_val_zero = 0;
  int32_t tmpval = 0;
  int16_t val16 = 0;
  uint8_t val1, val2;
  bool aloud = false;
  Serial.println(">");
  int32_t j = 0;
  while (true)
  {
    j = j + 1;
    //每25秒处理一次即可
    if (j % 100 == 0)
      timerWrite(timer, 0); //reset timer (feed watchdog)

    //读满缓冲区8000字节
    //此函数会自动调节时间，只要后续的操作不要让缓冲区占满即可
    //1/4秒 8000字节 4000次
    I2S_Read(communicationData, numCommunicationData);

    for (int loop1 = 0; loop1 < numCommunicationData / 2 ; loop1++)
    {
      val1 = communicationData[loop1 * 2];
      val2 = communicationData[loop1 * 2 + 1] ;
      val16 = val1 + val2 *  256;
      if (val16 > 0)
      {
        val_avg = val_avg + val16;
        val_max = max( val_max, val16);
      }
      //有声音的次数
      if (abs(val16) > 15 )
        all_val_zero = all_val_zero + 1;

      //乘以40 ：音量提升20db
      tmpval = val16 * 40;
      if (abs(tmpval) > 32767 )
      {
        if (val16 > 0)
          tmpval = 32767;
        else
          tmpval = -32767;
      }
      //Serial.println(String(val1) + " " + String(val2) + " " + String(val16) + " " + String(tmpval));
      communicationData[loop1 * 2] =  (byte)(tmpval & 0xFF);
      communicationData[loop1 * 2 + 1] = (byte)((tmpval >> 8) & 0xFF);

    }

    //待开始录音时缓存3秒
    //每个缓存环存满了换下一个缓存环
    cloudSpeechClient->pre_push_sound_buff((byte *)communicationData, numCommunicationData);


    //半秒检查一次  16000字节 8000次数据记录
    if (j % 2 == 0 && j > 0)
    {
      val_avg = val_avg / numCommunicationData ;
      if ( val_max > define_max1.toInt() && val_avg > define_avg1.toInt()   && all_val_zero > define_zero1.toInt() )
        aloud = true;
      else
        aloud = false;

      if (aloud)
      {
#ifdef SHOW_DEBUG
        timelong_str = ">>>>> high_max:" + String(val_max) +  " high_avg:" + String(val_avg) +   " all_val_zero:" + String(all_val_zero) ;
        Serial.println(timelong_str);
#endif
        break;
      }
      val_avg = 0;
      val_max = 0;
      all_val_zero = 0;

      //防止溢出
      if (j >= 1000000)
        j = 0;
    }
  }
  last_check = millis() ;
  return (true);
}

int record_sound()
{
  uint32_t all_starttime;
  uint32_t all_endtime;
  uint32_t timelong = 0;
  String timelong_str = "";
  uint32_t last_starttime = 0;

  float val_avg = 0;
  int16_t val_max = 0;
  int32_t all_val_zero = 0;
  int16_t val16 = 0;
  uint8_t val1, val2;
  bool aloud = false;
  int32_t tmpval = 0;
  int all_alound;
  writenum = 0;

  led_power(1);

  //初始化0
  cloudSpeechClient->sound_bodybuff_p = 0;

  //用双声道，32位并没什么关系，因为拷数据时间很快！完全不占用多少时间！
  //Serial.println("record start 16k,16位,单声道");
  Serial.println( ">" + GetLocalTime() );
#ifdef SHOW_DEBUG
  Serial.println(GetLocalTime() + "> " + "record... 反应时间:" + String(millis() - last_check) + "毫秒");
#endif
  //Serial.println("录音中， 声音格式:16khz 16位 单声道， 最长10秒自动结束...");
  // last_press = millis() / 1000;
  all_starttime = millis() / 1000;
  last_starttime = millis() / 1000;
  led_power(1);
  timerWrite(timer, 0); //reset timer (feed watchdog)
  for (uint32_t j = 0; j < waveDataSize / numCommunicationData; ++j) {
    //timelong_str = "";
    // Serial.println("loop");
    //读满缓冲区8000字节
    //此函数会自动调节时间，只要后续的操作不要让缓冲区占满即可
    I2S_Read(communicationData, numCommunicationData);

    //timelong_str = timelong_str + "," + j;
    //Serial.println(timelong_str);

    //平均值，最大值记录，检测静音参数用
    for (int loop1 = 0; loop1 < numCommunicationData / 2 ; loop1++)
    {
      val1 = communicationData[loop1 * 2];
      val2 = communicationData[loop1 * 2 + 1] ;
      val16 = val1 + val2 *  256;
      if (val16 > 0)
      {
        val_avg = val_avg + val16;
        val_max = max( val_max, val16);
      }
      if (abs(val16) > volume_low.toInt() )
        all_val_zero = all_val_zero + 1;

      //乘以40 ：音量提升20db
      tmpval = val16 * volume_double.toInt();
      if (abs(tmpval) > 32767 )
      {
        if (val16 > 0)
          tmpval = 32767;
        else
          tmpval = -32767;
      }
      communicationData[loop1 * 2] =  (byte)(tmpval & 0xFF);
      communicationData[loop1 * 2 + 1] = (byte)((tmpval >> 8) & 0xFF);
    }

    //声音信息保存
    cloudSpeechClient->push_bodybuff_buff((byte*)communicationData, numCommunicationData);

    writenum = writenum + numCommunicationData;
    //半秒检查一次静音  16000字节 8000次数据记录
    if (j % 2 == 0 && j > 0)
    {
      val_avg = val_avg / numCommunicationData ;

      if ( val_max > define_max2.toInt() && val_avg > define_avg2.toInt()   && all_val_zero > define_zero2.toInt() )
        aloud = true;
      else
        aloud = false;


      if (aloud)
      {
        all_alound = all_alound + 1;
        //录音过程中，调试输出不要轻易用，会影响识别率！
        //timelong_str = ">>>>> " + String( millis() / 1000 - all_starttime) + String("秒 ");
        //timelong_str = timelong_str + " high_max:" + String(val_max) +  " high_avg:" + String(val_avg) +   " all_val_zero:" + String(all_val_zero) ;
        //Serial.println(timelong_str);
        last_starttime = millis() / 1000;
      }

      val_avg = 0;
      val_max = 0;
      all_val_zero = 0;
    }

    //3秒仍静音，中断退出
    if ( millis() / 1000 - last_starttime > 2)
    {
#ifdef SHOW_DEBUG
      Serial.println("静音检测，退出");
#endif
      break;
    }
  }
  all_endtime = millis() / 1000;

#ifdef SHOW_DEBUG
  Serial.println("文件字节数:" + String(writenum) + ",理论秒数:" + String(writenum / 32000) + "秒") ;
  Serial.println("录音结束,时长:" + String(all_endtime - all_starttime) + "秒" );
#endif
  return (all_alound);
}

//如果flag 1 必须连接才算over,  如果为0 只试30秒
bool connectwifi(int flag)
{
  if (WiFi.status() == WL_CONNECTED) return true;

  while (true)
  {
    if (WiFi.status() == WL_CONNECTED) break;

    int trynum = 0;
    Serial.print("Connecting to ");
    if  ( set_index == "1")
      Serial.println(wifi_ssid1);
    else
      Serial.println(wifi_ssid2);
    //静态IP有时会无法被访问，原因不明！
    WiFi.disconnect(true); //关闭网络
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_STA);
    if ( set_index == "1")
      WiFi.begin(wifi_ssid1.c_str(), wifi_password1.c_str());
    else
      WiFi.begin(wifi_ssid2.c_str(), wifi_password2.c_str());
    while (WiFi.status() != WL_CONNECTED) {
      delay(2000);
      Serial.print(".");
      trynum = trynum + 1;
      //30秒 退出
      if (trynum > 14) break;
    }
    if (flag == 0) break;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("");
    Serial.println("WiFi connected with IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.subnetMask());
    Serial.println(WiFi.dnsIP(0));
    Serial.println(WiFi.dnsIP(1));
    return true;
  }
  else
    return false;

}

void setup() {
  Serial.begin(115200);
  pinMode(led, OUTPUT);
  led_power(0);

  //即使进入配置模式，也只10分钟!
  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000 , false); //set time in us
  timerAlarmEnable(timer);                          //enable interrupt

  preferences.begin("wifi-config");
  readparams(); 
  flash_led();

  //有限模式进入连接，如果30秒连接不上，返回false
  bool ret_bol = connectwifi(0);

  //wifi连接不上，进入配置模式
  if (ret_bol == false)
  {
    settingMode = true;
    led_power(1);
    setupMode();
    return;
  }
  else
  {
    //wifi连接，但有跳线，也进入配置模式
    //32,35 是否相连接
    if ( check_pin())
    {
      settingMode = true;
      led_power(1);
      setupMode();
      return;
    }
  }

  flash_led();


  sd_ok = true;
  //如果传I2S_BITS_PER_SAMPLE_8BIT 运行会报错，最小必须16BIT,然后通过拆分方式处理音频
  I2S_Init(I2S_MODE_RX, 16000, I2S_BITS_PER_SAMPLE_16BIT);


  cloudSpeechClient = new CloudSpeechClient();
  //此方法必须调用成功，否则语音识别会无法进行
  while (true)
  {
    String baidu_Token = cloudSpeechClient->getToken(baidu_key, baidu_secert);
    Serial.println("baidu_Token:" + baidu_Token);
    if (baidu_Token.length() > 0 )
      break;
  }

  //NTP 时间
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  if (report_mode  == "2")
    cloudSpeechClient->posturl(report_address, 1990, report_url  +    urlencode(">>启动") );

  Serial.println("start...");
}


bool check_pin()
{
  int pin_link = 0;

  pinMode(32, OUTPUT);
  pinMode(35, INPUT_PULLUP);

  digitalWrite(32, LOW);
  if (digitalRead(35) == LOW)
    pin_link = 1;
  digitalWrite(32, HIGH);

  if (pin_link == 0)
    return false;
  else
    return true;


}

void check_pin_old()
{
  /*
    pinMode(12, OUTPUT);
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);

    digitalWrite(12, LOW);


    if (digitalRead(13) == LOW)
      speaker = 1;
    if (digitalRead(14) == LOW)
      speaker = 2;
    digitalWrite(12, HIGH);
  */

  /*
      pinMode(32, OUTPUT);
      pinMode(35, INPUT_PULLUP);
      digitalWrite(32, LOW);
      if (digitalRead(35) == LOW)
        ip_address = 1;
      digitalWrite(32, HIGH);
  */
}

void begin_recordsound()
{
  //Serial.println("press 最长录音10秒，检测静音会提前结束" );

  int rec_ok = record_sound();
  String retstr = "";
  led_power(0);
  if (rec_ok == 0)
  {
#ifdef SHOW_DEBUG
    Serial.println("无用声音信号...");
#endif

    //String res = cloudSpeechClient->sound_berry(last_voice);
    //Serial.println("sound_berry:" + res);

    return;
  }
  else
  {
#ifdef SHOW_DEBUG
    Serial.println("进行文字识别" );
#endif
    uint32_t all_starttime = millis() / 1000;
    String VoiceText = cloudSpeechClient->getVoiceText();
    Serial.println("识别用时: " + String ( millis() / 1000 - all_starttime) + "秒" );
    VoiceText.replace("speech quality error", "");
    VoiceText.replace("。", "");
    if (VoiceText.length() > 0)
    {
      //每个汉字占3个长度
      Serial.println(String("识别结果:") + GetLocalTime() + "> " + VoiceText + " len=" + VoiceText.length());

      if (speak_mode != "1" && VoiceText.length() > 3)
      {
        if (VoiceText.indexOf("关灯") > -1)
        {
          if (set_index == "1")
            retstr = cloudSpeechClient->posturl(speak_address1, 8080, speak_led_off1);
          else
            retstr = cloudSpeechClient->posturl(speak_address2, 8080, speak_led_off2);
          Serial.println("retstr:" + retstr);
          if (report_mode == "2")
            cloudSpeechClient->posturl(report_address, 1990, report_url +    urlencode(VoiceText) );
          return;
        }
        else if (VoiceText.indexOf("开灯") > -1)
        {
          if  (set_index == "1")
            retstr = cloudSpeechClient->posturl(speak_address1, 8080,  speak_led_on1);
          else
            retstr = cloudSpeechClient->posturl(speak_address2, 8080,  speak_led_on2);
          Serial.println("retstr:" + retstr);
          if (report_mode == "2")
            cloudSpeechClient->posturl(report_address, 1990, report_url +    urlencode(VoiceText) );
          return;
        }
        else if (speak_mode == "3")
        {
          if (set_index == "1")
            retstr = cloudSpeechClient->posturl(speak_address1, 8080, speak_tulin1 + urlencode(VoiceText));
          else
            retstr = cloudSpeechClient->posturl(speak_address2, 8080, speak_tulin2 + urlencode(VoiceText));
          Serial.println("retstr:" + retstr);
          if (report_mode == "2")
            cloudSpeechClient->posturl(report_address, 1990, report_url +    urlencode(VoiceText) );
          return;
        }
        else
        {
          flash_led();
          if  (report_mode == "2")
            cloudSpeechClient->posturl(report_address, 1990, report_url +    urlencode(VoiceText) );
        }
      }
      else
      {
        flash_led();
        if (report_mode == "2")
          cloudSpeechClient->posturl(report_address, 1990, report_url +    urlencode(VoiceText) );
      }
    }
  }
}

void loop() {
  //if (sd_ok == false) return;

  //处理网页服务（必须有)
  if (settingMode)
  {
    webServer.handleClient();
    return;
  }


  connectwifi(1);

  wait_loud();
  begin_recordsound();
  delay(5000);
}



void setupMode() {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  delay(100);
  Serial.println("scanNetworks");


  //  wifi1_position = -1;
  //  wifi2_position = -1;

  //  wifi_ssid1 = preferences.getString("wifi_ssid1");
  //  wifi_ssid2 = preferences.getString("wifi_ssid2");
  ssidList1 = "";
  ssidList2 = "";
  for (int i = 0; i < n; ++i) {
    ssidList1 += "<option value=\"";
    ssidList1 += WiFi.SSID(i);
    ssidList1 += "\"";

    if (WiFi.SSID(i) == wifi_ssid1)
      ssidList1 += " selected ";


    ssidList1 += ">";
    ssidList1 += WiFi.SSID(i);
    ssidList1 += "</option>";


    ssidList2 += "<option value=\"";
    ssidList2 += WiFi.SSID(i);
    ssidList2 += "\"";

    if (WiFi.SSID(i) == wifi_ssid2)
      ssidList2 += " selected ";

    ssidList2 += ">";
    ssidList2 += WiFi.SSID(i);
    ssidList2 += "</option>";

    //    if (WiFi.SSID(i) == wifi_ssid1)
    //      wifi1_position = i;
    //    if (WiFi.SSID(i) == wifi_ssid2)
    //      wifi2_position = i;
  }
  delay(100);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID);
  WiFi.mode(WIFI_MODE_AP);
  startWebServer();
  Serial.println("Starting Access Point at \"" + String(apSSID) + "\"");

}



String makePage(String title, String contents) {
  String s = "<!DOCTYPE html><html><head>";
  s += "<meta name=\"viewport\" content=\"width=device-width,user-scalable=0\">";
  s += "<title>";
  s += title;
  s += "</title></head><body>";
  s += contents;
  s += "</body></html>";
  return s;
}

String new_urlDecode(String input) {
  String s = input;
  s.replace("%20", " ");
  s.replace("+", " ");
  s.replace("%21", "!");
  s.replace("%22", "\"");
  s.replace("%23", "#");
  s.replace("%24", "$");
  s.replace("%25", "%");
  s.replace("%26", "&");
  s.replace("%27", "\'");
  s.replace("%28", "(");
  s.replace("%29", ")");
  s.replace("%30", "*");
  s.replace("%31", "+");
  s.replace("%2C", ",");
  s.replace("%2E", ".");
  s.replace("%2F", "/");
  s.replace("%2C", ",");
  s.replace("%3A", ":");
  s.replace("%3A", ";");
  s.replace("%3C", "<");
  s.replace("%3D", "=");
  s.replace("%3E", ">");
  s.replace("%3F", "?");
  s.replace("%40", "@");
  s.replace("%5B", "[");
  s.replace("%5C", "\\");
  s.replace("%5D", "]");
  s.replace("%5E", "^");
  s.replace("%5F", "-");
  s.replace("%60", "`");
  return s;
}


void startWebServer() {

  //设置模式
  //if (settingMode) {
  //if (true)
  //{
  Serial.print("Starting Web Server at ");
  if (settingMode)
    Serial.println(WiFi.softAPIP());
  else
    Serial.println(WiFi.localIP());
  //设置主页

  // readparams();

  //selectedIndex相当于一个下拉列表数组，顺序按0，1，2，3....来设。
  //如果未选择，值为-1.
  //    int tmpindex = 0;
  //    if (set_index.length() > 0)
  //      tmpindex = set_index.toInt();
  webServer.on("/settings", []() {
    String s = "<h1>Wi-Fi Settings</h1><p>Please enter your password by selecting the SSID.</p>";
    s += "<form method=\"get\" action=\"setap\">index: " ;

    if (set_index == "1")
      s += "<select name=\"set_index\" ><option  value=\"1\" selected>1</option> <option  value=\"2\">2</option>  </select>";
    else if    (set_index == "2")
      s += "<select name=\"set_index\" ><option  value=\"1\">1</option> <option  value=\"2\" selected>2</option>  </select>";
    else
      s += "<select name=\"set_index\" ><option  value=\"1\">1</option> <option  value=\"2\">2</option>  </select>";

    if (speak_mode == "1")
      s += "<br>speak_mode: <select name=\"speak_mode\" ><option  value=\"1\" selected >None</option> <option  value=\"2\">light</option> <option  value=\"3\">light&voice</option>  </select>";
    else if (speak_mode == "2")
      s += "<br>speak_mode: <select name=\"speak_mode\" ><option  value=\"1\">None</option> <option  value=\"2\" selected>light</option> <option  value=\"3\">light&voice</option>  </select>";
    else if (speak_mode == "3")
      s += "<br>speak_mode: <select name=\"speak_mode\" ><option  value=\"1\">None</option> <option  value=\"2\" >light</option> <option  selected value=\"3\">light&voice</option>  </select>";
    else
      s += "<br>speak_mode: <select name=\"speak_mode\" ><option  value=\"1\">None</option> <option  value=\"2\">light</option><option  value=\"3\">light&voice</option>   </select>";

    if (report_mode == "1")
      s += "<br>report_mode: <select name=\"report_mode\" ><option  value=\"1\" selected>no</option> <option  value=\"2\">yes</option>  </select>";
    else if   (report_mode == "2")
      s += "<br>report_mode: <select name=\"report_mode\" ><option  value=\"1\">no</option> <option  value=\"2\" selected>yes</option>  </select>";
    else
      s += "<br>report_mode: <select name=\"report_mode\" ><option  value=\"1\">no</option> <option  value=\"2\">yes</option>  </select>";

    s += "<br>report_address: <input name=\"report_address\" style=\"width:350px\" value='" + report_address + "'type=\"text\">";
    s += "<br>report_url: <input name=\"report_url\" style=\"width:350px\"  value='" + report_url + "'type=\"text\">";

    s += "<br>baidu_key: <input name=\"baidu_key\" style=\"width:350px\"  value='" + baidu_key + "'type=\"text\">";
    s += "<br>baidu_secert: <input name=\"baidu_secert\" style=\"width:350px\"  value='" + baidu_secert + "'type=\"text\">";
    s += "<br>tulin_api: <input name=\"tulin_api\" style=\"width:350px\"  value='" + tulin_api + "'type=\"text\">";
    s += " <hr>";

    s += "<br>volume_low: <input name=\"volume_low\" style=\"width:100px\"  value='" + volume_low + "'type=\"text\">";
    s += "volume_high: <input name=\"volume_high\" style=\"width:100px\"  value='" + volume_high + "'type=\"text\">";
    s += "volume_double: <input name=\"volume_double\" style=\"width:100px\"  value='" + volume_double + "'type=\"text\">";

    s += "<br>define_max1: <input name=\"define_max1\" style=\"width:100px\"  value='" + define_max1 + "'type=\"text\">";
    s += "define_avg1: <input name=\"define_avg1\" style=\"width:100px\"  value='" + define_avg1 + "'type=\"text\">";
    s += "define_zero1: <input name=\"define_zero1\" style=\"width:100px\" value='" + define_zero1 + "'type=\"text\">";
    s += "<br>define_max2: <input name=\"define_max2\" style=\"width:100px\" value='" + define_max2 + "'type=\"text\">";
    s += "define_avg2: <input name=\"define_avg2\" style=\"width:100px\" value='" + define_avg2 + "'type=\"text\">";
    s += "define_zero2: <input name=\"define_zero2\" style=\"width:100px\" value='" + define_zero2 + "'type=\"text\">";

    s += " <hr>";
    s += "<label>SSID1: </label><select style=\"width:200px\"  name=\"wifi_ssid1\" >" + ssidList1 +  "</select>";
    s += "Password1: <input name=\"wifi_password1\" style=\"width:100px\"  value='" + wifi_password1 + "' type=\"text\">";
    s += "<br>speak address1: <input name=\"speak_address1\" style=\"width:350px\"  value='" + speak_address1 + "' type=\"text\">";
    s += "<br>speak led on1: <input name=\"speak_led_on1\" style=\"width:350px\"  value='" + speak_led_on1 + "'type=\"text\">";
    s += "<br>speak led off1: <input name=\"speak_led_off1\" style=\"width:350px\"  value='" + speak_led_off1 + "'type=\"text\">";
    s += "<br>speak tulin1: <input name=\"speak_tulin1\" style=\"width:350px\" value='" + speak_tulin1 + "'type=\"text\">";
    s += "<hr>";

    s += "<label>SSID2: </label><select style=\"width:200px\"  name=\"wifi_ssid2\" >" + ssidList2  + "</select>";
    s += "Password2: <input name=\"wifi_password2\" style=\"width:100px\"  value='" + wifi_password2 + "' type=\"text\">";
    s += "<br>speak address2: <input name=\"speak_address2\" style=\"width:350px\"  value='" + speak_address2 + "'type=\"text\">";
    s += "<br>speak led on2: <input name=\"speak_led_on2\" style=\"width:350px\"  value='" + speak_led_on2 + "'type=\"text\">";
    s += "<br>speak led off2: <input name=\"speak_led_off2\" style=\"width:350px\"   value='" + speak_led_off2 + "'type=\"text\">";
    s += "<br>speak tulin2: <input name=\"speak_tulin2\" style=\"width:350px\"   value='" + speak_tulin2 + "'type=\"text\">";
    s += "<br><input type=\"submit\"></form>";
    webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
  });
  //设置写入页(后台)
  webServer.on("/setap", []() {
    set_index = new_urlDecode(webServer.arg("set_index"));

    speak_mode = new_urlDecode(webServer.arg("speak_mode"));

    report_mode = new_urlDecode(webServer.arg("report_mode"));
    report_address = new_urlDecode(webServer.arg("report_address"));
    report_url = new_urlDecode(webServer.arg("report_url"));

    baidu_key = new_urlDecode(webServer.arg("baidu_key"));
    baidu_secert = new_urlDecode(webServer.arg("baidu_secert"));
    tulin_api = new_urlDecode(webServer.arg("tulin_api"));

    volume_low = new_urlDecode(webServer.arg("volume_low"));
    volume_high = new_urlDecode(webServer.arg("volume_high"));

    volume_double = new_urlDecode(webServer.arg("volume_double"));

    define_max1 = new_urlDecode(webServer.arg("define_max1"));
    define_avg1 = new_urlDecode(webServer.arg("define_avg1"));
    define_zero1 = new_urlDecode(webServer.arg("define_zero1"));
    define_max2 = new_urlDecode(webServer.arg("define_max2"));
    define_avg2 = new_urlDecode(webServer.arg("define_avg2"));
    define_zero2 = new_urlDecode(webServer.arg("define_zero2"));

    wifi_ssid1 = new_urlDecode(webServer.arg("wifi_ssid1"));
    wifi_password1 = new_urlDecode(webServer.arg("wifi_password1"));
    speak_address1 = new_urlDecode(webServer.arg("speak_address1"));
    speak_led_on1 = new_urlDecode(webServer.arg("speak_led_on1"));
    speak_led_off1 = new_urlDecode(webServer.arg("speak_led_off1"));
    speak_tulin1 = new_urlDecode(webServer.arg("speak_tulin1"));

    wifi_ssid2 = new_urlDecode(webServer.arg("wifi_ssid2"));
    wifi_password2 = new_urlDecode(webServer.arg("wifi_password2"));
    speak_address2 = new_urlDecode(webServer.arg("speak_address2"));
    speak_led_on2 = new_urlDecode(webServer.arg("speak_led_on2"));
    speak_led_off2 = new_urlDecode(webServer.arg("speak_led_off2"));
    speak_tulin2 = new_urlDecode(webServer.arg("speak_tulin2"));

    Serial.print("baidu_secert: " + baidu_secert);

    //写入配置
    writeparams();
    String wifi_ssid = "";
    String wifi_password = "";
    if (set_index == "" || set_index == "1")
    {
      wifi_ssid = wifi_ssid1;
      wifi_password = wifi_password1;
    }
    else
    {
      wifi_ssid = wifi_ssid2;
      wifi_password = wifi_password2;
    }


    String s = "<h1>Setup complete.</h1><p>device will be connected to \"";
    s += wifi_ssid;
    s += "\" after the restart.";
    webServer.send(200, "text/html", makePage("Wi-Fi Settings", s));
    delay(3000);
    ESP.restart();
  });
  webServer.onNotFound([]() {
    String s = "<h1>AP mode</h1><p><a href=\"/settings\">Wi-Fi Settings</a></p>";
    webServer.send(200, "text/html", makePage("AP mode", s));
  });

  webServer.begin();
}
