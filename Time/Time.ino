/*
  THIS WATCH CODE IS BUILT ON TOP OF THE CODE FROM https://github.com/olikraus/u8g2/, 
  which is licensed as Simplified BSD License! See the License File! 
  See SelectionList.ido for orignal code that this was built on top of
*/
#include <Arduino.h>
#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
void setup(void) {  
  // MKR Zero Test Board
  u8g2.begin(/*Select=*/ 8, /*Right/Next=*/ 1, /*Left/Prev=*/ 2, /*Up=*/ 6, /*Down=*/ 7, /*Home/Cancel=*/ A6); 
  u8g2.setFont(u8g2_font_6x12_tr);
}
const char *string_list = 
  "New Task\n"
  "Pomogrado Timer\n"
  "To-Do List\n"
  "Find iPhone\n"
  "Cirrus\n"
  "Cumulonimbus\n"
  "Cumulus\n"
  "Nimbostratus\n"
  "Stratocumulus\n"
  "Stratus";
uint8_t current_selection = 1;
void loop(void) {
  current_selection = u8g2.userInterfaceSelectionList(
    "05:03:03",
    current_selection, 
    string_list);
  if ( current_selection == 0 ) {
    u8g2.userInterfaceMessage(
	"Nothing selected.", 
	"",
	"",
	" ok ");
  } else {
    u8g2.userInterfaceMessage(
	"Selection:", 
	u8x8_GetStringLineStart(current_selection-1, string_list ),
	"",
	" ok \n cancel ");
  }
}
