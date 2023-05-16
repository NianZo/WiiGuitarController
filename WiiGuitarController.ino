//#include <HID.h>

#include <stdint.h>
#include <Arduino.h>
#include "PluggableUSB.h"

#define _USING_HID

// HID 'Driver'
// ------------
#define HID_GET_REPORT        0x01
#define HID_GET_IDLE          0x02
#define HID_GET_PROTOCOL      0x03
#define HID_SET_REPORT        0x09
#define HID_SET_IDLE          0x0A
#define HID_SET_PROTOCOL      0x0B

#define HID_HID_DESCRIPTOR_TYPE         0x21
#define HID_REPORT_DESCRIPTOR_TYPE      0x22
#define HID_PHYSICAL_DESCRIPTOR_TYPE    0x23

// HID subclass HID1.11 Page 8 4.2 Subclass
#define HID_SUBCLASS_NONE 0
#define HID_SUBCLASS_BOOT_INTERFACE 1

// HID Keyboard/Mouse bios compatible protocols HID1.11 Page 9 4.3 Protocols
#define HID_PROTOCOL_NONE 0
#define HID_PROTOCOL_KEYBOARD 1
#define HID_PROTOCOL_MOUSE 2

// Normal or bios protocol (Keyboard/Mouse) HID1.11 Page 54 7.2.5 Get_Protocol Request
// "protocol" variable is used for this purpose.
#define HID_BOOT_PROTOCOL	0
#define HID_REPORT_PROTOCOL	1

// HID Request Type HID1.11 Page 51 7.2.1 Get_Report Request
#define HID_REPORT_TYPE_INPUT   1
#define HID_REPORT_TYPE_OUTPUT  2
#define HID_REPORT_TYPE_FEATURE 3

typedef struct
{
  uint8_t len;      // 9
  uint8_t dtype;    // 0x21
  uint8_t addr;
  uint8_t versionL; // 0x101
  uint8_t versionH; // 0x101
  uint8_t country;
  uint8_t desctype; // 0x22 report
  uint8_t descLenL;
  uint8_t descLenH;
} HIDDescDescriptor;

typedef struct 
{
  InterfaceDescriptor hid;
  HIDDescDescriptor   desc;
  EndpointDescriptor  in;
} HIDDescriptor;

class HIDSubDescriptor {
public:
  HIDSubDescriptor *next = NULL;
  HIDSubDescriptor(const void *d, const uint16_t l) : data(d), length(l) { }

  const void* data;
  const uint16_t length;
};

class HID_ : public PluggableUSBModule
{
public:
  HID_(void);
  int begin(void);
  int SendReport(uint8_t id, const void* data, int len);
  void AppendDescriptor(HIDSubDescriptor* node);

protected:
  // Implementation of the PluggableUSBModule
  int getInterface(uint8_t* interfaceCount);
  int getDescriptor(USBSetup& setup);
  bool setup(USBSetup& setup);
  uint8_t getShortName(char* name);

private:
  uint8_t epType[1];

  HIDSubDescriptor* rootNode;
  uint16_t descriptorSize;

  uint8_t protocol;
  uint8_t idle;
};

// Replacement for global singleton.
// This function prevents static-initialization-order-fiasco
// https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
HID_& HID();

#define D_HIDREPORT(length) { 9, 0x21, 0x01, 0x01, 0, 1, 0x22, lowByte(length), highByte(length) }


HID_& HID()
{
	static HID_ obj;
	return obj;
}

int HID_::getInterface(uint8_t* interfaceCount)
{
	*interfaceCount += 1; // uses 1
	HIDDescriptor hidInterface = {
		D_INTERFACE(pluggedInterface, 1, USB_DEVICE_CLASS_HUMAN_INTERFACE, HID_SUBCLASS_NONE, HID_PROTOCOL_NONE),
		D_HIDREPORT(descriptorSize),
		D_ENDPOINT(USB_ENDPOINT_IN(pluggedEndpoint), USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 0x01)
	};
	return USB_SendControl(0, &hidInterface, sizeof(hidInterface));
}

int HID_::getDescriptor(USBSetup& setup)
{
	// Check if this is a HID Class Descriptor request
	if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) { return 0; }
	if (setup.wValueH != HID_REPORT_DESCRIPTOR_TYPE) { return 0; }

	// In a HID Class Descriptor wIndex contains the interface number
	if (setup.wIndex != pluggedInterface) { return 0; }

	int total = 0;
	HIDSubDescriptor* node;
	for (node = rootNode; node; node = node->next) {
		int res = USB_SendControl(TRANSFER_PGM, node->data, node->length);
		if (res == -1)
			return -1;
		total += res;
	}
	
	// Reset the protocol on reenumeration. Normally the host should not assume the state of the protocol
	// due to the USB specs, but Windows and Linux just assumes its in report mode.
	protocol = HID_REPORT_PROTOCOL;
	
	return total;
}

uint8_t HID_::getShortName(char *name)
{
	name[0] = 'H';
	name[1] = 'I';
	name[2] = 'D';
	name[3] = 'A' + (descriptorSize & 0x0F);
	name[4] = 'A' + ((descriptorSize >> 4) & 0x0F);
	return 5;
}

void HID_::AppendDescriptor(HIDSubDescriptor *node)
{
	if (!rootNode) {
		rootNode = node;
	} else {
		HIDSubDescriptor *current = rootNode;
		while (current->next) {
			current = current->next;
		}
		current->next = node;
	}
	descriptorSize += node->length;
}

int HID_::SendReport(uint8_t id, const void* data, int len)
{
	// auto ret = USB_Send(pluggedEndpoint, &id, 1);
	// if (ret < 0) return ret;
	auto ret2 = USB_Send(pluggedEndpoint | TRANSFER_RELEASE, data, len);
	if (ret2 < 0) return ret2;
	return ret2;
}

bool HID_::setup(USBSetup& setup)
{
	if (pluggedInterface != setup.wIndex) {
		return false;
	}

	uint8_t request = setup.bRequest;
	uint8_t requestType = setup.bmRequestType;

	if (requestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE)
	{
		if (request == HID_GET_REPORT) {
			// TODO: HID_GetReport();
			return true;
		}
		if (request == HID_GET_PROTOCOL) {
			// TODO: Send8(protocol);
			return true;
		}
		if (request == HID_GET_IDLE) {
			// TODO: Send8(idle);
		}
	}

	if (requestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE)
	{
		if (request == HID_SET_PROTOCOL) {
			// The USB Host tells us if we are in boot or report mode.
			// This only works with a real boot compatible device.
			protocol = setup.wValueL;
			return true;
		}
		if (request == HID_SET_IDLE) {
			idle = setup.wValueL;
			return true;
		}
		if (request == HID_SET_REPORT)
		{
			//uint8_t reportID = setup.wValueL;
			//uint16_t length = setup.wLength;
			//uint8_t data[length];
			// Make sure to not read more data than USB_EP_SIZE.
			// You can read multiple times through a loop.
			// The first byte (may!) contain the reportID on a multreport.
			//USB_RecvControl(data, length);
		}
	}

	return false;
}

HID_::HID_(void) : PluggableUSBModule(1, 1, epType),
                   rootNode(NULL), descriptorSize(0),
                   protocol(HID_REPORT_PROTOCOL), idle(1)
{
	epType[0] = EP_TYPE_INTERRUPT_IN;
	PluggableUSB().plug(this);
}

int HID_::begin(void)
{
	return 0;
}

static const uint8_t _hidReportDescriptor[137] PROGMEM =
{
  0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
0x09, 0x05,        // Usage (Game Pad)
0xA1, 0x01,        // Collection (Application)
0x15, 0x00,        //   Logical Minimum (0)
0x25, 0x01,        //   Logical Maximum (1)
0x35, 0x00,        //   Physical Minimum (0)
0x45, 0x01,        //   Physical Maximum (1)
0x75, 0x01,        //   Report Size (1)
0x95, 0x0D,        //   Report Count (13)
0x05, 0x09,        //   Usage Page (Button)
0x19, 0x01,        //   Usage Minimum (0x01)
0x29, 0x0D,        //   Usage Maximum (0x0D)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x95, 0x03,        //   Report Count (3)
0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
0x25, 0x07,        //   Logical Maximum (7)
0x46, 0x3B, 0x01,  //   Physical Maximum (315)
0x75, 0x04,        //   Report Size (4)
0x95, 0x01,        //   Report Count (1)
0x65, 0x14,        //   Unit (System: English Rotation, Length: Centimeter)
0x09, 0x39,        //   Usage (Hat switch)
0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
0x65, 0x00,        //   Unit (None)
0x95, 0x01,        //   Report Count (1)
0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x26, 0xFF, 0x00,  //   Logical Maximum (255)
0x46, 0xFF, 0x00,  //   Physical Maximum (255)
0x09, 0x30,        //   Usage (X)
0x09, 0x31,        //   Usage (Y)
0x09, 0x32,        //   Usage (Z)
0x09, 0x35,        //   Usage (Rz)
0x75, 0x08,        //   Report Size (8)
0x95, 0x04,        //   Report Count (4)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
0x09, 0x20,        //   Usage (0x20)
0x09, 0x21,        //   Usage (0x21)
0x09, 0x22,        //   Usage (0x22)
0x09, 0x23,        //   Usage (0x23)
0x09, 0x24,        //   Usage (0x24)
0x09, 0x25,        //   Usage (0x25)
0x09, 0x26,        //   Usage (0x26)
0x09, 0x27,        //   Usage (0x27)
0x09, 0x28,        //   Usage (0x28)
0x09, 0x29,        //   Usage (0x29)
0x09, 0x2A,        //   Usage (0x2A)
0x09, 0x2B,        //   Usage (0x2B)
0x95, 0x0C,        //   Report Count (12)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0x0A, 0x21, 0x26,  //   Usage (0x2621)
0x95, 0x08,        //   Report Count (8)
0xB1, 0x02,        //   Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x0A, 0x21, 0x26,  //   Usage (0x2621)
0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
0x46, 0xFF, 0x03,  //   Physical Maximum (1023)
0x09, 0x2C,        //   Usage (0x2C)
0x09, 0x2D,        //   Usage (0x2D)
0x09, 0x2E,        //   Usage (0x2E)
0x09, 0x2F,        //   Usage (0x2F)
0x75, 0x10,        //   Report Size (16)
0x95, 0x04,        //   Report Count (4)
0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
0xC0,              // End Collection
// 137 bytes
};



typedef struct
{
  uint16_t buttons; // 2
  uint8_t hatAndConstant; // 1
  uint8_t axis[4]; // 4
  uint8_t reserved1[12]; // 12
  uint64_t finalConstant; // 8
} InstrumentButtonState;

class Instrument
{
public:
  Instrument(void);
  void SendReport2(InstrumentButtonState* buttons);
};

Instrument::Instrument(void)
{
  static HIDSubDescriptor node(_hidReportDescriptor, sizeof(_hidReportDescriptor));
  HID().AppendDescriptor(&node);  
  //AppendDescriptor(&node);
}

void Instrument::SendReport2(InstrumentButtonState* buttons)
{
  HID().SendReport(0, (uint8_t*)buttons, 27);//sizeof(InstrumentButtonState)); // Ummmmm... the struct size is 27, and wireshark shows the URB size as 27 when I put 26 here
  //USB_Send(pluggedInterface | TRANSFER_RELEASE, buttons, 27);
  //USB_Send(1 | TRANSFER_RELEASE, buttons, 2);
  //controller.write((uint8_t*)buttons, 2);
}

int TESTBUTTON = 2;
int BIT0 = 2;
int BIT1 = 3;
int BIT2 = 4;
int BIT3 = 5;
int BIT4 = 6;
int BIT5 = 7;
int BIT6 = 8;
int BIT7 = 9;
int BIT8 = 10;
int BIT9 = 11;
//int wammyPin = 12;
int STRUM_DOWN = 0;
int STRUM_UP = 1;
int TILT_SWITCH = 2;
int BUTTON_GREEN = 3;
int BUTTON_RED = 4;
int BUTTON_YELLOW = 5;
int BUTTON_BLUE = 6;
int BUTTON_ORANGE = 7;
int SOLO_BUTTON_GREEN = 8;
int SOLO_BUTTON_RED = 9;
int SOLO_BUTTON_YELLOW = 10;
int SOLO_BUTTON_BLUE = 11;
int SOLO_BUTTON_ORANGE = 12;
int BUTTON_PLUS = 13;
int BUTTON_MINUS = 18;
int ANALOG_WAMMY = 19;
int BUTTON_UP = 20;
int BUTTON_RIGHT = 21;
int BUTTON_DOWN = 22;
int BUTTON_LEFT = 23;

uint16_t GREEN_BIT = 0x0002;
uint16_t RED_BIT = 0x0004;
uint16_t YELLOW_BIT = 0x0008;
uint16_t BLUE_BIT = 0x0001;
uint16_t ORANGE_BIT = 0x0010;
uint16_t SOLO_BIT = 0x0040;
uint16_t OVERDRIVE_BIT = 0x0020;
uint16_t PLUS_BIT = 0x0200;
uint16_t MINUS_BIT = 0x0100;

int OBLED = 13;
Instrument instrument;
InstrumentButtonState buttonState;

void setButtonBits(uint16_t bits, bool state)
{
  if (state)
  {
    buttonState.buttons |= bits;
  }  
  else
  {
    buttonState.buttons &= ~bits;
  }
}

void setOtherBits(uint8_t bits, bool state)
{
  if (state)
  {
    buttonState.hatAndConstant |= bits;
  }  
  else
  {
    buttonState.hatAndConstant &= ~bits;
  }
}

void setup() {
  // put your setup code here, to run once:
  //pinMode(TESTBUTTON, INPUT_PULLUP);
  pinMode(STRUM_DOWN, INPUT_PULLUP);
  pinMode(STRUM_UP, INPUT_PULLUP);
  pinMode(TILT_SWITCH, INPUT_PULLUP);
  pinMode(BUTTON_GREEN, INPUT_PULLUP);
  pinMode(BUTTON_RED, INPUT_PULLUP);
  pinMode(BUTTON_YELLOW, INPUT_PULLUP);
  pinMode(BUTTON_BLUE, INPUT_PULLUP);
  pinMode(BUTTON_ORANGE, INPUT_PULLUP);
  pinMode(SOLO_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(SOLO_BUTTON_RED, INPUT_PULLUP);
  pinMode(SOLO_BUTTON_YELLOW, INPUT_PULLUP);
  pinMode(SOLO_BUTTON_BLUE, INPUT_PULLUP);
  pinMode(SOLO_BUTTON_ORANGE, INPUT_PULLUP);
  pinMode(BUTTON_PLUS, INPUT_PULLUP);
  pinMode(BUTTON_MINUS, INPUT_PULLUP);
  pinMode(ANALOG_WAMMY, INPUT);
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_LEFT, INPUT_PULLUP);
  //pinMode(OBLED, OUTPUT);
  buttonState.buttons = 0x0000;
  buttonState.hatAndConstant = 0x08;
  //buttonState.instrumentIdentifier = 0x80808080;//0x7f807f80;// guitar identifier //0x80808080; // Drum identifier
  buttonState.axis[0] = 0;
  buttonState.axis[1] = 0;
  buttonState.axis[2] = 0;
  buttonState.axis[3] = 0;
  for (int i = 0; i < 12; i++)
  {
    buttonState.reserved1[i] = 0x0;
  }
  buttonState.finalConstant = 0x0200020002000200;
}

void loop() {
  // put your main code here, to run repeatedly:
  buttonState.buttons = 0;
  buttonState.hatAndConstant = 0;
  //setButtonBits(0x0082, !digitalRead(TESTBUTTON));
  // setButtonBits(0x0080, !digitalRead(BIT0));
  // setButtonBits(0x0040, !digitalRead(BIT1));
  // setButtonBits(0x0020, !digitalRead(BIT2));
  // setButtonBits(0x0010, !digitalRead(BIT3));
  // setButtonBits(0x0008, !digitalRead(BIT4));
  // setButtonBits(0x0004, !digitalRead(BIT5));
  // setButtonBits(0x0002, !digitalRead(BIT6));
  // setButtonBits(0x0001, !digitalRead(BIT7));
  // setButtonBits(0x8000, !digitalRead(BIT8));
  // setButtonBits(0x4000, !digitalRead(BIT9));
  setButtonBits(GREEN_BIT, !digitalRead(BUTTON_GREEN) || !digitalRead(SOLO_BUTTON_GREEN));  
  setButtonBits(RED_BIT, !digitalRead(BUTTON_RED) || !digitalRead(SOLO_BUTTON_RED));
  setButtonBits(YELLOW_BIT, !digitalRead(BUTTON_YELLOW) || !digitalRead(SOLO_BUTTON_YELLOW));
  setButtonBits(BLUE_BIT, !digitalRead(BUTTON_BLUE) || !digitalRead(SOLO_BUTTON_BLUE));
  setButtonBits(ORANGE_BIT, !digitalRead(BUTTON_ORANGE) || !digitalRead(SOLO_BUTTON_ORANGE));
  setButtonBits(SOLO_BIT, !digitalRead(SOLO_BUTTON_GREEN) || !digitalRead(SOLO_BUTTON_RED) || !digitalRead(SOLO_BUTTON_YELLOW) || !digitalRead(SOLO_BUTTON_BLUE) || !digitalRead(SOLO_BUTTON_ORANGE));
  setButtonBits(OVERDRIVE_BIT, !digitalRead(TILT_SWITCH));
  setButtonBits(PLUS_BIT, !digitalRead(BUTTON_PLUS));
  setButtonBits(MINUS_BIT, !digitalRead(BUTTON_MINUS));
  if (!digitalRead(BUTTON_UP) || !digitalRead(STRUM_UP))
  {
    buttonState.hatAndConstant = 0x00;
  } else if (!digitalRead(BUTTON_RIGHT))
  {
    buttonState.hatAndConstant = 0x02;
  } else if (!digitalRead(BUTTON_DOWN) || !digitalRead(STRUM_DOWN))
  {
    buttonState.hatAndConstant = 0x04;
  } else if (!digitalRead(BUTTON_LEFT))
  {
    buttonState.hatAndConstant = 0x06;
  } else
  {
    buttonState.hatAndConstant = 0x08;
  }

  //uint16_t wammy = (analogRead(A11) / 100) - 1;
  //buttonState.hatAndConstant |= wammy << 4; 
  uint8_t wammy = analogRead(ANALOG_WAMMY) / 4;
  buttonState.axis[2] = wammy;
  //setOtherBits(0x08, !digitalRead(A0));
  //setOtherBits(0x04, !digitalRead(A1));
  //setOtherBits(0x02, !digitalRead(A2));
  //setOtherBits(0x01, !digitalRead(A3));
  //setButtonBits(0x0200, !digitalRead(A4));
  //setButtonBits(0x0100, !digitalRead(A5));
  // if (!digitalRead(TESTBUTTON))
  // {
  //   buttonState.buttons |= 0x0082;
  // }
  // else
  // {
  //   buttonState.buttons &= ~0x0082;
  // }
  //digitalWrite(OBLED, LOW);
  //delay(200);
  //digitalWrite(OBLED, HIGH);
  instrument.SendReport2(&buttonState);
  delay(10);
}
