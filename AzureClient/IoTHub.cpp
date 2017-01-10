#include "IoTHub.h"

IoT::IoT(CloudConfig* cloud)
{ 
  _cloud = cloud;
}

String IoT::urlEncode(const char* msg)
{
    const char *hex = "0123456789abcdef";
    String encodedMsg = "";

    while (*msg!='\0'){
        if( ('a' <= *msg && *msg <= 'z')
                || ('A' <= *msg && *msg <= 'Z')
                || ('0' <= *msg && *msg <= '9') ) {
            encodedMsg += *msg;
        } else {
            encodedMsg += '%';
            encodedMsg += hex[*msg >> 4];
            encodedMsg += hex[*msg & 15];
        }
        msg++;
    }
    return encodedMsg;
}

String IoT::createSas(char* key, String url){    
  _cloud->sasExpiryTime = now() + _cloud->sasExpiryPeriodInSeconds;
  String stringToSign = url + "\n" + _cloud->sasExpiryTime;
    
  // START: Create signature
  // https://raw.githubusercontent.com/adamvr/arduino-base64/master/examples/base64/base64.ino
  
  int keyLength = strlen(key);
  
  int decodedKeyLength = base64_dec_len(key, keyLength);
  char decodedKey[decodedKeyLength];  //allocate char array big enough for the base64 decoded key
  
  base64_decode(decodedKey, key, keyLength);  //decode key
  
  Sha256.initHmac((const uint8_t*)decodedKey, decodedKeyLength);
  Sha256.print(stringToSign);  
  char* sign = (char*) Sha256.resultHmac();
  // END: Create signature
  
  // START: Get base64 of signature
  int encodedSignLen = base64_enc_len(HASH_LENGTH);
  char encodedSign[encodedSignLen];
  base64_encode(encodedSign, sign, HASH_LENGTH); 
  
  // SharedAccessSignature
  return "sr=" + url + "&sig="+ urlEncode(encodedSign) + "&se=" + _cloud->sasExpiryTime;
  // END: create SAS  
}

String IoT::buildHttpRequest(String data){  
  return "POST " + _cloud->endPoint + " HTTP/1.1\r\n" +
    "Host: " + _cloud->host + "\r\n" +
    "Authorization: SharedAccessSignature " + _cloud->fullSas + "\r\n" +
    "Content-Type: application/atom+xml;type=entry;charset=utf-8\r\n" +
    "Content-Length: " + data.length() + "\r\n\r\n" + data;
}

const char* IoT::GetStringValue(String value){
  int len = value.length() + 1;
  char *temp = new char[len];
  value.toCharArray(temp, len);
  return temp;
}

void IoT::initialiseHub(){
  _cloud->sasUrl = urlEncode(_cloud->host) + urlEncode(TARGET_URL) + (String)_cloud->deviceId;
  _cloud->endPoint = (String)TARGET_URL + (String)_cloud->deviceId + (String)IOT_HUB_END_POINT;
}

bool IoT::generateSas(){
  if (timeStatus() == timeNotSet) { return false; }
    
  if (!azureInitialised) { 
    initialiseHub();
    azureInitialised = true;
  }

  if (now() > _cloud->sasExpiryTime){
    Serial.println("\n\r\n\rgenerating new sas\n\r");
    delete[] _cloud->fullSas;
    _cloud->fullSas = (char*)GetStringValue(createSas(_cloud->key, _cloud->sasUrl));
  }

  return true;
}

bool IoT::verifyServerFingerprint(){
  if (tlsClient.verify(_cloud->certificateFingerprint, _cloud->host)) {
    Serial.print("Certificate fingerprint verified against ");
    Serial.print(_cloud->host);
    Serial.println(" sucessfully");
  } else {
    Serial.println("Certificate fingerprint verification failed");
    ESP.restart();
  }
}

bool IoT::connectToAzure() {
  if (tlsClient.connected()) { return true;}  
  Serial.println();
  delay(500); // give network connection a moment to settle
  Serial.print(_cloud->deviceId);
  Serial.print(" connecting to ");
  Serial.println(_cloud->host);
  if (WiFi.status() != WL_CONNECTED) { return false; }
  if (!tlsClient.connect(_cloud->host, 443)) {      // Use WiFiClientSecure class to create TLS connection
    Serial.print("Host connection failed.  WiFi IP Address: ");
    Serial.println(WiFi.localIP());

    delay(2000);
  }
  else {

    // verify the server really who it says it is to prevent man in the middle attack
    
    verifyServerFingerprint();
    
    Serial.println("Host connected");
    yield(); // give firmware some time 
//    delay(250); // give network connection a moment to settle
  }

  return tlsClient.connected();
}


String IoT::send(String json){
  if (!generateSas()) {return "Sas not generated, likely cause is time not set";}
  
  connectToAzure();
  
  tlsClient.flush();
  int bytesWritten = tlsClient.print(buildHttpRequest(json)); 

  String response = "";
  String chunk = "";
  int limit = 1;
  
  do {
    if (tlsClient.connected()) { 
      yield();
      chunk = tlsClient.readStringUntil('\n');
      response += chunk;
    }
  } while (chunk.length() > 0 && ++limit < 100); 
  
  return (response.length() > 12) ? response.substring(9, 12) : "unknown";
}
