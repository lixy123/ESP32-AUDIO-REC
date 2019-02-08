#include "CloudSpeechClient.h"

CloudSpeechClient::CloudSpeechClient() {
  sound_bodybuff = (byte*)ps_malloc(maxnum_bodysound__buff);
  sound_bodybuff_p = 0;

  pre_sound_buff = (byte*)ps_malloc(pre_maxnum_sound_buff);
  zero_pre_push_sound_buff();

  //wav head
  wav_head = (byte*)ps_malloc(headerSize + 4);
  CreateWavHeader(wav_head, headerSize);
}

CloudSpeechClient::~CloudSpeechClient() {
  //client.stop();
  //WiFi.disconnect();
}



void CloudSpeechClient::zero_pre_push_sound_buff()
{
  pre_sound_buf_p = 0;
  for (unsigned long loop1 = 0; loop1 < pre_maxnum_sound_buff; loop1++)
    pre_sound_buff[loop1] = 0;
}

void CloudSpeechClient::pre_push_sound_buff(byte * src_buff, uint32_t len)
{
  memcpy(pre_sound_buff + pre_sound_buf_p, src_buff, len);
  pre_sound_buf_p = pre_sound_buf_p + len;
  if (pre_sound_buf_p >= pre_maxnum_sound_buff)
    pre_sound_buf_p = 0;
}

void CloudSpeechClient::push_bodybuff_buff(byte * src_buff, uint32_t len)
{
  memcpy(sound_bodybuff + sound_bodybuff_p, src_buff, len);
  sound_bodybuff_p = sound_bodybuff_p + len;
  if (sound_bodybuff_p >= maxnum_bodysound__buff)
    sound_bodybuff_p = 0;
}

String CloudSpeechClient::Findkey(String line)
{
  int firstDividerIndex = line.indexOf("access_token");
  int secondDividerIndex = line.indexOf("session_key");
  if (firstDividerIndex == -1 ||  secondDividerIndex == -1) return "";
  String gatheredStr = line.substring(firstDividerIndex + 15, secondDividerIndex - 3);
  return ( gatheredStr);
}


String CloudSpeechClient::getToken(String api_key, String api_secert)
{
  while (!client.connect("tsn.baidu.com", 80)) {
    Serial.println("connection failed");
  }

  //以下信息通过 Fiddler 工具分析协议，Raw部分获得
  String url = "https://openapi.baidu.com/oauth/2.0/token?grant_type=client_credentials&client_id=" + api_key + "&client_secret=" + api_secert;
  String  HttpHeader = String("GET ") + String(url) +
                       String(" HTTP/1.1\r\nHost: openapi.baidu.com\r\nConnection: keep-alive") + String("\r\n\r\n");
#ifdef SHOW_DEBUG
  Serial.println(HttpHeader);
#endif
  client.print(HttpHeader);
  String retstr = "";
  String line = "";
  uint32_t starttime = 0;
  uint32_t stoptime = 0;
  starttime = millis() / 1000;

  while (!client.available())
  {
    stoptime = millis() / 1000;
    if (stoptime - starttime >= 5)
    {
#ifdef SHOW_DEBUG
      Serial.println("timeout >5s");
#endif
      return "";
    }
  }
  while (client.available())
  {
    line = client.readStringUntil('\n');
    //Serial.println(line);
    retstr = Findkey(line);
    if ( retstr.length() > 0)
      break;
  }
  delay(10);
  client.stop();
  baidu_Token = retstr;
  return (retstr);
}


//输入来源：sound_bodybuff, pre_sound_buff两秒缓冲环
uint32_t CloudSpeechClient::PrintHttpBody2()
{
  //当前已处理字节数
  uint32_t now_read_num = 0;
  //下一批次计划读取字节数
  uint32_t readnum = 0;

  String enc = "";
  uint32_t writenum = 0;
  //发送字节总计
  uint32_t writenum_enc = 0;
  //循环次数，用于输出进度用
  uint32_t   cnt = 0;

  //当前实际发送字节数
  uint32_t   active_write = 0;
  uint32_t  now_write = 0;

  //多出的4个字节是为了 base64 凑倍数用的
  uint32_t fileSizeMinus8 =  sound_bodybuff_p  + pre_maxnum_sound_buff + 4 + 44 - 8;
  wav_head[4] = (byte)(fileSizeMinus8 & 0xFF);
  wav_head[5] = (byte)((fileSizeMinus8 >> 8) & 0xFF);
  wav_head[6] = (byte)((fileSizeMinus8 >> 16) & 0xFF);
  wav_head[7] = (byte)((fileSizeMinus8 >> 24) & 0xFF);

  fileSizeMinus8 =   sound_bodybuff_p + pre_maxnum_sound_buff + 4 ;
  wav_head[40] = (byte)(fileSizeMinus8 & 0xFF);
  wav_head[41] = (byte)((fileSizeMinus8 >> 8) & 0xFF);
  wav_head[42] = (byte)((fileSizeMinus8 >> 16) & 0xFF);
  wav_head[43] = (byte)((fileSizeMinus8 >> 24) & 0xFF);


  //1.发送wav head
  enc = base64::encode((byte*)wav_head, headerSize + 4);
  enc.replace("\n", "");// delete last "\n"

  //注意:此处需要循环发送,确保数据正确发送完成
  enc.toCharArray((char *)buff_base64, enc.length() + 1);
  active_write = 0;
  //反复发送，确保都发送出去
  //注意：此处有坑！！！
  while (active_write < enc.length())
  {
    now_write = client.write(buff_base64 + active_write, enc.length() - active_write);
    if (now_write != (enc.length() - active_write))
    {
      //Serial.println("待发送：" + String( enc.length() - active_write) + " 实际发送:" + String(now_write));
      delay(10);
    }
    active_write = active_write + now_write;
    writenum_enc = writenum_enc + now_write;
  }
  writenum = headerSize + 4;

  //2.发送缓存环的2秒音频数据
  //2.1缓存环的后半段
  now_read_num = pre_sound_buf_p;
  //注意：  size must be multiple of 3 for Base64 encoding. Additional byte size must be even because wave data is 16bit.
  while (now_read_num < pre_maxnum_sound_buff)
  {
    //数值不宜过大，否则容易网络中断
    //用1020是为了必须是3的倍数，同时为2的倍数
    readnum = 1020;
    if (pre_maxnum_sound_buff - now_read_num >= readnum)
    {
      memcpy(buff , pre_sound_buff + now_read_num, readnum);
    }
    else
    {
      readnum = pre_maxnum_sound_buff - now_read_num;

      //不是6的整数倍,余数给去除
      int tmpint = readnum % 6;
      if (tmpint > 0)
      {
        readnum = readnum - tmpint;
        now_read_num = now_read_num + tmpint;
        writenum = writenum + tmpint;
        //Serial.println("pass:" + String(tmpint) );
#ifdef SHOW_DEBUG
        Serial.println(String(">loss: ") + String(tmpint));
#endif
      }
      memcpy(buff , pre_sound_buff + now_read_num, readnum);
    }


    if (readnum > 0)
    {
      enc = base64::encode((byte*)buff, readnum);
      enc.replace("\n", "");// delete last "\n"

      //注意:此处需要循环发送,确保数据正确发送完成
      enc.toCharArray((char *)buff_base64, enc.length() + 1);
      active_write = 0;
      //反复发送，确保都发送出去
      //注意：此处有坑！！！
      while (active_write < enc.length())
      {
        now_write = client.write(buff_base64 + active_write, enc.length() - active_write);
        if (now_write != (enc.length() - active_write))
        {
          //Serial.println("Content-Length已发送字节:" + String(writenum_enc) + " 待发送：" + String(enc.length() - active_write) + " 实际发送:" + String(now_write));
          delay(10);
        }
        active_write = active_write + now_write;
        writenum_enc = writenum_enc + now_write;
      }
#ifdef SHOW_DEBUG
      if (cnt % 10 == 0)
      {
        Serial.print(">");
        //Serial.println("Content-Length已发送字节:" + String(writenum_enc));
      }
#endif
      cnt = cnt + 1;
    }
    now_read_num = now_read_num + readnum;
    writenum = writenum + readnum;
  }


  //2.2 缓存环的前半段
  if (pre_sound_buf_p > 0)
  {
    now_read_num = 0;
    //注意：  size must be multiple of 3 for Base64 encoding. Additional byte size must be even because wave data is 16bit.
    while (now_read_num < pre_sound_buf_p)
    {
      //数值不宜过大，否则容易网络中断
      //用1020是为了必须是3的倍数，同时为2的倍数
      readnum = 1020;
      if (pre_sound_buf_p - now_read_num >= readnum)
      {
        memcpy(buff , pre_sound_buff + now_read_num, readnum);
      }
      else
      {
        readnum = pre_sound_buf_p - now_read_num;
        //不是6的整数倍,余数给去除
        int tmpint = readnum % 6;
        if (tmpint > 0)
        {
          readnum = readnum - tmpint;
          now_read_num = now_read_num + tmpint;
          writenum = writenum + tmpint;
          //Serial.println("pass:" + String(tmpint) );
#ifdef SHOW_DEBUG
          Serial.println(String(">loss: ") + String(tmpint));
#endif
        }
        memcpy(buff , pre_sound_buff + now_read_num, readnum);
      }


      if (readnum > 0)
      {
        enc = base64::encode((byte*)buff, readnum);
        enc.replace("\n", "");// delete last "\n"

        //注意:此处需要循环发送,确保数据正确发送完成
        enc.toCharArray((char *)buff_base64, enc.length() + 1);
        active_write = 0;
        //反复发送，确保都发送出去
        //注意：此处有坑！！！
        while (active_write < enc.length())
        {
          now_write = client.write(buff_base64 + active_write, enc.length() - active_write);
          if (now_write != (enc.length() - active_write))
            delay(10);
          active_write = active_write + now_write;
          writenum_enc = writenum_enc + now_write;
        }
#ifdef SHOW_DEBUG
        if (cnt % 10 == 0)
        {
          Serial.print(">");
          //Serial.println("Content-Length已发送字节:" + String(writenum_enc));
        }
#endif
        cnt = cnt + 1;
      }  //end if (readnum > 0)
      now_read_num = now_read_num + readnum;
      writenum = writenum + readnum;
    }
  }

  //3.发送主要的录音数据
  now_read_num = 0;
  //注意：  size must be multiple of 3 for Base64 encoding. Additional byte size must be even because wave data is 16bit.

  //Serial.println("待处理字节:" + String(sound_bodybuff_p) + " 预期发送字节:" + String(sound_bodybuff_p * 4 / 3));

  while (now_read_num < sound_bodybuff_p)
  {
    //Serial.println("0 now_read_num:" + String(now_read_num) + " sound_bodybuff_p=" + String(sound_bodybuff_p));

    //用1020是为了必须是3的倍数，同时为2的倍数
    readnum = 1020;
    if (sound_bodybuff_p - now_read_num >= readnum)
    {
      memcpy(buff , sound_bodybuff + now_read_num, readnum);
    }
    else
    {
      readnum = sound_bodybuff_p - now_read_num;
      //不是6的整数倍,余数给去除
      int tmpint = readnum % 6;
      if (tmpint > 0)
      {
        readnum = readnum - tmpint;
        now_read_num = now_read_num + tmpint;
        writenum = writenum + tmpint;
        //Serial.println("pass:" + String(tmpint) );
#ifdef SHOW_DEBUG
        Serial.println(String(">loss: ") + String(tmpint));
#endif
      }
      memcpy(buff , sound_bodybuff + now_read_num, readnum);
    }

    //Serial.println("1 now_read_num:" + String(now_read_num) + " sound_bodybuff_p=" + String(sound_bodybuff_p));
    //Serial.println("readnum:" + String(readnum) );

    if (readnum > 0)
    {
      enc = base64::encode((byte*)buff, readnum);
      enc.replace("\n", "");// delete last "\n"
      //注意:此处需要循环发送,确保数据正确发送完成
      enc.toCharArray((char *)buff_base64, enc.length() + 1);
      active_write = 0;
      //反复发送，确保buff_base64 的字节数enc.length() 都发送出去
      //注意：此处有坑！！！
      while (active_write < enc.length())
      {
        now_write = client.write(buff_base64 + active_write, enc.length() - active_write);
        if (now_write != (enc.length() - active_write))
        {
          //Serial.println("Content-Length已发送字节:" + String(writenum_enc) + " 待发送：" + String(enc.length() - active_write) + " 实际发送:" + String(now_write));
          delay(10);
        }
        active_write = active_write + now_write;
        writenum_enc = writenum_enc + now_write;
      }
#ifdef SHOW_DEBUG
      if (cnt % 10 == 0)
      {
        Serial.print(">");
        //Serial.println("Content-Length已发送字节:" + String(writenum_enc));
      }
#endif
      cnt = cnt + 1;
    }
    now_read_num = now_read_num + readnum;
    writenum = writenum + readnum;

  }

  //Serial.println("writenum:" + String(writenum) + " now_read_num= " + String(now_read_num) + " writenum_enc=" + String(writenum_enc));


  return (writenum_enc);
}


//examples:
//{"err_msg":"json read error.","err_no":3300,"sn":""}
//{"corpus_no":"6641105603733134892","err_msg":"success.","err_no":0,"result":["今天天气不错。"],"sn":"747471463051546252892"}
String CloudSpeechClient::Find_baidutext(String line)
{
  const char* p_result;
  const char* p_err_msg;
  String err_msg = "";
  String result_msg = "";
#ifdef SHOW_DEBUG
  Serial.println(">" + line);
#endif
  if (line.indexOf("err_msg") > 0)
  {
    StaticJsonBuffer<512> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(line);
    p_err_msg =  root["err_msg"];
    err_msg = String(p_err_msg);
    if (err_msg.startsWith("success."))
    {
      p_result = root["result"][0];
      result_msg = String(p_result );
    }
    if (err_msg.startsWith("speech quality error."))
    {
      result_msg = "speech quality error";
    }

  }
  return (result_msg);
}

//语音转文字
String CloudSpeechClient::getVoiceText()
{
  if (baidu_Token.length() == 0)
    return ("");

  uint32_t wav_filesize =  headerSize + 4 + sound_bodybuff_p + pre_maxnum_sound_buff ;
  //Serial.println(String("FileName:") + fn + " FileSize=" + String(wav_filesize));
  //保证字节数是6的倍数，否则base64转换会异常
  //wav 除不尽, 损失几个字节无关紧要

  //声音文件，丢掉一些数据
  uint32_t tmpint = sound_bodybuff_p % 6;
  if (tmpint > 0)
  {
#ifdef SHOW_DEBUG
    Serial.println(String(">loss: ") + String(tmpint));
#endif
    wav_filesize = wav_filesize - tmpint;
  }

  //缓存后半段丢掉一些数据
  tmpint = (pre_maxnum_sound_buff - pre_sound_buf_p) % 6;

  if (tmpint > 0)
  {
#ifdef SHOW_DEBUG
    Serial.println(String(">loss: ") + String(tmpint));
#endif
    wav_filesize = wav_filesize - tmpint;
  }
  // 缓存前半段丢掉一些数据
  tmpint = pre_sound_buf_p % 6;

  if (tmpint > 0)
  {
#ifdef SHOW_DEBUG
    Serial.println(String(">loss: ") + String(tmpint));
#endif
    wav_filesize = wav_filesize - tmpint;
  }


  //Serial.println(wav_filesize);
  if (!client.connect("vop.baidu.com", 80)) {
#ifdef SHOW_DEBUG
    Serial.println("connection failed");
#endif
    return ("");
  }

  uint32_t httpBody2Length = wav_filesize * 4 / 3; //  4/3 is from base64 encoding
#ifdef SHOW_DEBUG
  Serial.println(String("httpBody2Length:") + String(httpBody2Length));
#endif
  String HttpBody1 = String("") + "{\"format\":\"wav\", \"channel\":1, \"cuid\":\"python_test\"," +
                     "\"token\":\"" + baidu_Token + "\"," +
                     " \"rate\":16000, \"len\":" + String(wav_filesize) + ",\"speech\":\"";
  String HttpBody3 = "\"}";
  String ContentLength = String(HttpBody1.length() + httpBody2Length + HttpBody3.length());
#ifdef SHOW_DEBUG
  Serial.println("ContentLength:" + String(ContentLength));
#endif
  String HttpHeader = String("POST ") + String(upvoice_url) + " HTTP/1.1\r\n" +
                      "Content-Type: application/json\r\n" +
                      "Host: vop.baidu.com\r\n" +
                      "Content-Length: " + String(ContentLength) + "\r\n" +
                      "Connection: keep-alive\r\n\r\n";

#ifdef SHOW_DEBUG
  Serial.println(String(HttpHeader) + String(HttpBody1));
#endif

  client.print(HttpHeader);
#ifdef SHOW_DEBUG
  Serial.println("send HttpBody");
#endif
  client.print(HttpBody1);

  uint32_t writenum_enc = PrintHttpBody2();

#ifdef SHOW_DEBUG
  Serial.println(String("bodylen:") + String(HttpBody1.length() + writenum_enc + HttpBody3.length()));
  Serial.println(HttpBody3);
#endif

  client.print(HttpBody3);

#ifdef SHOW_DEBUG
  Serial.println("wait response");
#endif
  String line = "";
  bool http_ok = false;
  bool head_ok = false;
  uint32_t resultsize = 0;
  String findresult = "";
  String tmpstr = "";

  uint32_t starttime = 0;
  uint32_t stoptime = 0;
  starttime = millis() / 1000;

  while (!client.available())
  {
    stoptime = millis() / 1000;
    if (stoptime - starttime >= 5)
    {
#ifdef SHOW_DEBUG
      Serial.println("response time out >5s");
#endif
      return "";
    }
    delay(200);
  }

  //HTTP/1.1 500 Internal Server Error
  //{"corpus_no":"6641105603733134892","err_msg":"success.","err_no":0,"result":["今天天气不错。"],"sn":"747471463051546252892"}
  while (client.available())
  {
    if (head_ok == true)
    {
      //Serial.println("head_ok quit");
      if (http_ok  && resultsize > 0)
      {
        if (resultsize <= 1024)
        {
          // Serial.println(resultsize);
          client.read(buff, resultsize);
          char tmparray[resultsize + 1] ;
          memcpy(tmparray, buff, resultsize);

          tmparray[resultsize] = 0;
          // Serial.println(resultsize);
          findresult = Find_baidutext( String(tmparray));
        }
      }
      break;
    }
    line = client.readStringUntil('\n');

    if (line.startsWith("HTTP/1.1"))
    {
#ifdef SHOW_DEBUG
      Serial.println(">" + line);
#endif
      if (line.startsWith("HTTP/1.1 200 OK"))
        http_ok = true;
      //line不含'\n'
      //Serial.println(String("HTTP/1.1 200 OK").length());
      // Serial.println(line.length());
    }
    if (line.startsWith("Content-Length: "))
    {
      tmpstr = line.substring(String("Content-Length: ").length(), line.length());
#ifdef SHOW_DEBUG
      Serial.println(">Content-Length: " + tmpstr);
#endif
      //如果超过65535可能会有问题!
      resultsize = tmpstr.toInt();
    }
    if (line.length() == 1 && line.startsWith("\r"))
    {
      head_ok = true;
      //Serial.println(">end");
    }

    //Serial.println(line);

  }
  delay(10);
  client.stop();
  return (findresult);
}

bool CloudSpeechClient::savemp3(long file_size)
{
  long writenum = 0;
  if (SD.exists(textfile))
  {
    SD.remove(textfile);
    Serial.println(String(textfile) + " remove");
  }

  File  file = SD.open(textfile, FILE_WRITE);
  if (!file)
  {
    Serial.println("open file error!");
    return false;
  }

  while (client.available())
  {
    int readnum = client.read(buff, 1024);
    file.write(buff, readnum);
    writenum = writenum + readnum;
  }

  file.close();

  #ifdef SHOW_DEBUG
  Serial.println("savemp3 success FileSize=" + String(writenum));
  #endif
  return (true);
}

//文字转语音
String CloudSpeechClient::getVoice(String audio_text)
{
  if (baidu_Token.length() == 0)
    return ("");

  if (!client.connect("tsn.baidu.com", 80)) {
    Serial.println("connection failed");
  }
  audio_text = urlencode(audio_text);
  //Serial.println(audio_text);
  //以下信息通过 Fiddler 工具分析协议，Raw部分获得
  String url = "http://tsn.baidu.com/text2audio";
  //示例
  //tex=%E4%BD%A0%E5%A5%BD%E5%8C%97%E4%BA%AC%E9%82%AE%E7%94%B5%E5%A4%A7%E5%AD%A6%21&lan=zh&tok=24.5ba107afc08c6833511d17ceac4ff424.2592000.1548770176.282335-9406754&ctp=1&cuid=test_python";
  //# 4为pcm-16k；5为pcm-8k；6为wav 16k16位带文件头 pcm不带wav头
  String body = "tex=" + audio_text + "&lan=zh&tok=" + baidu_Token + "&ctp=1&aue=5&cuid=test_python";

  String  HttpHeader = "POST " + String(url) + " HTTP/1.1\r\n" +
                       "Host: tsn.baidu.com\r\n" +
                       "Connection: keep-alive\r\n" +
                       "Content-Type: application/json\r\n" +
                       "Content-Length: " + String(body.length()) +  "\r\n\r\n";
  Serial.println(HttpHeader);
  //Serial.println(body);
  client.print(HttpHeader);
  client.print(body);
  String retstr = "";
  String line = "";
  bool audio_ok = false;
  bool http_ok = false;
  bool head_ok = false;
  uint32_t filesize = 0;

  String tmpstr = "";
  uint32_t starttime = 0;
  uint32_t stoptime = 0;
  starttime = millis() / 1000;
  while (!client.available())
  {
    stoptime = millis() / 1000;
    if (stoptime - starttime >= 5)
    {
      Serial.println("timeout >5s");
      return "";
    }
  }
  while (client.available())
  {

    if (head_ok == true)
    {
      //Serial.println("head_ok quit");
      if (http_ok && audio_ok && filesize > 0)
        savemp3(filesize);
      break;
    }
    line = client.readStringUntil('\n');

    if (line.startsWith("HTTP/1.1 200 OK"))
    {
      http_ok = true;
      Serial.println(">HTTP/1.1 200 OK");

      //line不含'\n'
      //Serial.println(String("HTTP/1.1 200 OK").length());
      // Serial.println(line.length());
    }
    if (line.startsWith("Content-Length: "))
    {
      tmpstr = line.substring(String("Content-Length: ").length(), line.length());
      Serial.println(">Content-Length: " + tmpstr );
      //Serial.println();
      //如果超过65535可能会有问题!
      filesize = tmpstr.toInt();
    }
    if (line.startsWith("Content-Type: audio/mp3"))
    {
      audio_ok = true;
      Serial.println(">Content-Type: audio/mp3");
    }
    if (line.length() == 1 && line.startsWith("\r"))
    {
      head_ok = true;
      //Serial.println(">end");
    }
    //Serial.println(line);
  }
  delay(10);
  client.stop();
  return ("succ");
}

//有些路由器对自建服务器会支持不好，原因不明
//自制 LED类控制，connect 这一步会失败，原因不明！
String CloudSpeechClient::posturl(String host, int port, String url)
{

  // Serial.println(String("host:") + host + " url:" + url);

  //可能有啥技巧不了解
  if (!client.connect(host.c_str(), port))
  {
#ifdef SHOW_DEBUG
    Serial.println("posturl connection failed");
#endif
    return ("posturl connection failed");
  }
  String  HttpHeader = String("GET ") + url + " HTTP/1.1\r\n" +
                       "Host: " + host + ":" + String(port) + "\r\n" +
                       "Connection: keep-alive\r\n\r\n" ;

#ifdef SHOW_DEBUG
  Serial.println(HttpHeader);
#endif
  client.print(HttpHeader);

  String retstr = "";
  String line = "";

  bool http_ok = false;
  bool head_ok = false;
  uint32_t resultsize = 0;

  String tmpstr = "";
  uint32_t starttime = 0;
  uint32_t stoptime = 0;
  starttime = millis() / 1000;
  while (!client.available())
  {
    stoptime = millis() / 1000;
    if (stoptime - starttime >= 5)
    {
#ifdef SHOW_DEBUG
      Serial.println("timeout >5s");
#endif
      return "";
    }
  }
  //Content-Type: text/plain
  while (client.available())
  {
    if (head_ok == true)
    {
      if (http_ok && resultsize > 0)
      {
        //Serial.println(resultsize);
        if (resultsize < 1024)
        {
          client.read(buff, resultsize);
          char tmparray[resultsize + 1] ;
          memcpy(tmparray, buff, resultsize);
          tmparray[resultsize] = 0;
          retstr =  String(tmparray);
        }
        break;
      }
    }
    line = client.readStringUntil('\n');

    if (line.startsWith("HTTP/1.1 200 OK"))
    {
      http_ok = true;
#ifdef SHOW_DEBUG
      Serial.println(">HTTP/1.1 200 OK");
#endif
      //line不含'\n'
      //Serial.println(String("HTTP/1.1 200 OK").length());
      // Serial.println(line.length());
    }
    if (line.startsWith("Content-Length: "))
    {
      tmpstr = line.substring(String("Content-Length: ").length(), line.length());
#ifdef SHOW_DEBUG
      Serial.println(">Content-Length: " + tmpstr );
#endif
      //Serial.println();
      //如果超过65535可能会有问题!
      resultsize = tmpstr.toInt();
    }

    if (line.length() == 1 && line.startsWith("\r"))
    {
      head_ok = true;
      //Serial.println(">end");
    }
    //Serial.println(line);
  }
  delay(10);
  client.stop();
#ifdef SHOW_DEBUG
  Serial.println("用时:" + String( millis() / 1000 - starttime) + "秒");
#endif
  return (retstr);
}
