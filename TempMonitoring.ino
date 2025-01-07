#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS_PIN A4

// Define the phone number which is verified to ask for temperature and to which the low temp alert is sent
const String PHONE_NUMBER = "+989137584081"; 

// Minimum temperature (in Celsius) for low temp alert
const float MIN_TEMP_THRESHOLD = 45.0;  

OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature tempSensor(&oneWire);

SoftwareSerial sim800Serial(3, 2); // RX, TX pins for SIM800 module

bool isLowTempAlertSent = false;  // Flag to track if low temp alert has been sent

bool gspStatusCheckingFlag = false; // Flag to track the GSM status on startup

// Variables for timing the SMS config checks
unsigned long lastSmsConfigCheck = 0;  
const unsigned long smsConfigCheckInterval = 30000;  // Check every 30 seconds

void setup()
{
  Serial.begin(9600);
  sim800Serial.begin(9600);
  tempSensor.begin();

  Serial.println("Initializing...");
  delay(1000);

  checkAndConfigureSMS();
}

void loop()
{
  // Waiting for GSM to register to the network
  if (!gspStatusCheckingFlag)
  {
    if (!isGSMReady())
    {
      delay(1000);
      return;
    }
    else
    {
      gspStatusCheckingFlag = true;
      lastSmsConfigCheck = millis();
    }
  }
  
  // Check if it's time to perform the SMS configuration check
  unsigned long currentMillis = millis();
  if (currentMillis - lastSmsConfigCheck >= smsConfigCheckInterval) {
    lastSmsConfigCheck = currentMillis;  // Update the time of the last check
    checkAndConfigureSMS();  // Check and reconfigure SMS parameters if needed
  }

  // Process incoming SMS
  String receivedSms = "";  
  bool isSmsReady = false;
  while (sim800Serial.available() > 0)
  {
    bool isSmsReceived = sim800Serial.find("+CMT: \"");
    if (isSmsReceived)
    {
      receivedSms = sim800Serial.readString();  
      receivedSms.trim();                     
      isSmsReceived = false;                    

      isSmsReady = true;                        
    }
  }
  if (isSmsReady)
  {
    if (receivedSms.startsWith(PHONE_NUMBER)) // Checking if the sender is the verified phone number
    {
      sendATCommand("AT+CMGDA=DEL ALL");
      // Checking the text message
      if (receivedSms.endsWith("Temp?")) 
      {
        tempSensor.requestTemperatures();
        sendSms(PHONE_NUMBER, "Temp: " + String(tempSensor.getTempCByIndex(0)) + " Celsius");
      }
      else if (receivedSms.endsWith("Reset"))
      {
        isLowTempAlertSent = false;
      }
    }
  }

  // Regularly check the temperature
  tempSensor.requestTemperatures();
  if (tempSensor.getTempCByIndex(0) < MIN_TEMP_THRESHOLD)
  {
    if (!isLowTempAlertSent && isGSMReady())
    {
      isLowTempAlertSent = sendLowTempAlert();
    }
  }
  
  delay(500);
}

void checkAndConfigureSMS()
{
  String response = sendATCommandWithResponse("AT+CSMP?");
  if (response.indexOf("+CSMP: 17,167,0,0") == -1) // If not properly configured
  {
    Serial.println("SMS parameters not set. Reconfiguring...");
    sendATCommand("AT+CSMP=17,167,0,0");
  }
  
  // Check if the SMS is in Text Mode (AT+CMGF=1)
  response = sendATCommandWithResponse("AT+CMGF?");
  if (response.indexOf("+CMGF: 1") == -1) // If not in Text Mode
  {
    Serial.println("Text mode not configured. Reconfiguring...");
    sendATCommand("AT+CMGF=1");
  }
  
  // Check New Message Indication (AT+CNMI)
  response = sendATCommandWithResponse("AT+CNMI?");
  if (response.indexOf("+CNMI: 1,2,0,0,0") == -1) // If not properly configured
  {
    Serial.println("New message indication not configured. Reconfiguring...");
    sendATCommand("AT+CNMI=1,2,0,0,0");
  }
}

bool isGSMReady()
{
  String response = sendATCommandWithResponse("AT+CREG?");
  Serial.println(response);
  if (response.indexOf("+CREG: 0,1") != -1 || response.indexOf("+CREG: 0,5") != -1) {
    Serial.println("GSM module is ready and registered to the network.");
    checkAndConfigureSMS();
    return true; // GSM is ready
  }
  else {
    Serial.println("GSM module is not registered or ready.");
    return false; // GSM is not ready
  }
}

bool sendLowTempAlert()
{
  // Send the low temperature alert SMS and check if it was successfully sent
  Serial.println("Sending low temp alert...");
  return sendSms(PHONE_NUMBER, "LOW TEMP ALERT.\nTemp: " + String(tempSensor.getTempCByIndex(0)) + " Celsius");
}

bool sendSms(const String& phoneNumber, const String& message)
{
  sim800Serial.println("AT+CMGS=\"" + phoneNumber + "\"");
  delay(500);
  sim800Serial.println(message);
  delay(500);
  sim800Serial.write(26);  // Send Ctrl+Z to indicate the end of the SMS
  delay(400);

  // Wait for acknowledgment that the message was sent
  unsigned long currentMillis = millis();
  unsigned long timeoutMillis = currentMillis + 30000;  // Wait 30 seconds for SMS send acknowledgment

  // Check for expected responses
  while (millis() < timeoutMillis) {
    if (sim800Serial.available()) {
      String response = sim800Serial.readString();
      if (response.indexOf("OK") != -1) {
        Serial.println("SMS sent successfully.");
        return true;  // SMS sent successfully
      }
      else if (response.indexOf("ERROR") != -1) {
        Serial.println("Error sending SMS. Retrying...");
        return false;  // Error in sending SMS
      }
    }
  }

  return false;
}

void sendATCommand(const String& command)
{
  sim800Serial.println(command);  
  delay(500);  
  while (sim800Serial.available()) 
  {
    Serial.write(sim800Serial.read());
  }
}

String sendATCommandWithResponse(const String& command)
{
  sim800Serial.println(command);  
  delay(500);  
  String response = "";
  while (sim800Serial.available()) 
  {
    response += (char)sim800Serial.read();
  }
  return response;
}
