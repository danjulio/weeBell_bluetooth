# weeBell Bluetooth Handsfree
This repository contains an ESP32 IDF project to build firmware running on [weeBell](https://github.com/danjulio/weeBell_hardware) that allows traditional POTS telephones to work with a cellphone via the Bluetooth Handsfree protocol.  Incoming calls ring the POTS phone(s) and can be answered on them and the phones can be used to make calls via the cellphone.

![weeBell with telephones](pictures/weeBell_with_phones.png)

## [first] A Shout Out
This project contains a significant amount of audio processing necessary to do things like generate and decode DTMF tones and dial tones, convert between different samples rates, and, very importantly, implement Line Echo Cancellation (LEC) which is has long been a necessary function of telephone systems that end in a hybrid circuit driving the two-wire POTS telephone electrical connection.  The LEC removes the audio echoed back by the hybrid so the person at the remote end doesn't hear themselves echoed back a few hundred milliseconds after they speak (the time it takes the cellular network to move the audio between two points).  This project was made possible by the discovery of David Rowe's OSLEC echo cancellation code integrated into Steve Underwood's [spandsp (github)](https://github.com/svn2github/oslec) telephony library.  IMHO this library is a thing of beauty (clearly coming from tremendous skill and experience) and made it possible for me to create this project.  A huge thank you to David and Steve, and thanks for putting this up on github so it wasn't consigned to the dustbin of history.

Although not inclusive of the history of [spandsp (Steve's website)](https://www.soft-switch.org/), you can read about David Rowe's experience building OSLEC on his website starting with [this](http://www.rowetel.com/?p=18) post.

What is fun is this code that used to have to run on [then] high-end computer processors or dedicated DSP chips can now run, unmodified, on inexpensive embedded processors like the ESP32 (an email conversation with David encouraged me to pursue using his code as he felt the ESP32 should be fully capable of running his algorithm).

## Building the project
The project was developed using Espressif IDF v4.4.4 and creates firmware to run on [gCore](https://github.com/danjulio/gCore).  The project is contained in the ```gcore_pots_bt``` directory.  These instructions assume that the IDF is installed and configured in a shell window (instructions at Espressif's [Getting Started](https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/get-started/index.html) web page).

### Configure
The project ```sdkconfig``` is preconfigured with options for the project.  These typically should not need to be changed but it is important to use these configuration items since many optimizations and specific IDF configuration changes have been made.

### Build

```idf.py build```

### Build and load
Connect gCore to your computer and turn on.  It should enumerate as a USB serial device.  You will use the serial port device file name (Linux, OS X) or COM port (Windows) in the command below (PORT).

```idf.py -p [PORT] flash```

or 

```idf.py -p [PORT] flash monitor```

to also open a serial connection after programming so you can see diagnostic messages printed by the firmware to the serial port (```idf.py -p [PORT] monitor``` to simply open a serial connection).

## Loading pre-compiled firmware
There are several easy ways to load pre-compiled firmware into gCore without having to install the IDF and compile.

1. [Easiest] Use the gCore Serial Programmer Desktop application from my [website](https://danjuliodesigns.com/products/gcore.html) to load the firmware onto gCore. See below for links for different desktop platforms.
2. Use the Espressif Windows-only programming tool to load the firmware binaries found in the ```precompiled``` directory here.
3. You can also use the IDF command line tool ```esptool.py``` to load the firmware binaries at the locations shown below if you have it installed.

| Binary File | Load Location |
| --- | --- |
| booloader.bin | 0x1000 |
| partition-table.bin | 0x8000 |
| gcore\_pots_bt.bin | 0x10000 |


### gCore Serial Programmer
Direct downloads for platform-specific gCore Serial Programmer versions may be found at the links below. 

1. [Linux download](https://danjuliodesigns.com/resources/AppSupport/gcore/gsc_0_2_0_Linux_x86_64.zip) - 64-bit x86 architecture version
2. [Mac OS download](https://danjuliodesigns.com/resources/AppSupport/gcore/gsc_0_2_0_macOS.zip) - 64-bit universal (sadly unsigned so you'll have to authorize it manually in System Preferences->Security&Privacy)
3. [Windows download](https://danjuliodesigns.com/resources/AppSupport/gcore/gsc_0_2_0_Windows64.zip) - 64-bit x86 architecture version

It downloads the current binaries from my website and is a great way to load the latest version of this program (and other gCore demos).

Connect gCore to your computer and switch on.  Open the gCore Serial Programmer and select the associated serial port form the pull-down menu.  Then select ```weeBell Bluetooth``` from the Software List and press Program.  Programming will take about 30 seconds and then gCore should reboot running the weeBell firmware.

![gCore Serial Programmer programming weeBell](pictures/gsc_programming_weeBell.png)

### Espressif Programming Tool
You can download the Espressif Programming Tool [here](https://www.espressif.com/en/support/download/other-tools).  Assuming you have downloaded the binary files in the ```precompiled``` directory to your PC, you can use this software as follows.


1. Connect gCore to your PC and turn it on.  After a few seconds Windows should recognize it as a serial device and assign a COM port to it.
2. Start the Espressif Programming Tool.
3. Configure the software with the three binary files and programming locations as shown below.
4. Select the COM port assigned to gCore.
5. Press START to initiate programming.
6. Once programming is complete, press and hold the gCore power button for five seconds to power it off.  Then press it again to power gCore back on.  You should see the weeBell firmware running.

![Espressif Programming Tool setup](pictures/esp_programming_weeBell.png)

## Operation

gCore should display the following screen after loading the firmware.

![weeBell Bluetooth main screen](pictures/weeBell_initial_screen.png)

### Pairing
weeBell Bluetooth uses the classic Bluetooth Handsfree profile to communicate with a cellphone.  Bluetooth must be enabled on the cellphone and weeBell paired with the cellphone.

1. Go to the Bluetooth settings screen on your cellphone.
2. Click the Gear icon on weeBell to show the Settings screen.
3. Click Pair on the weeBell Settings screen.  It will enter pairing mode and display a 4-digit PIN as shown below.  You have 60 seconds to complete the pairing before it automatically exits pairing mode.
4. You should see ```weeBell``` show up in your cellphone's Bluetooth screen.  Click it on the cellphone to initiate pairing.
5. Your cellphone will request a PIN code.  Enter the PIN code from weeBell's Settings Screen (below "Bluetooth").
6. Your cellphone and weeBell should complete the pairing.  Your cellphone may ask if you wish to share contacts and call lists.  You can allow or deny this (currently weeBell doesn't request either of these).

![weeBell Pairing process](pictures/weeBell_pairing_process.png)

### User Interface

#### Main Screen

![weeBell main screen controls](pictures/weeBell_main_controls.png)

| Control | Description |
| --- | --- |
| Status | Current device status. |
| Bluetooth | Displays a Bluetooth icon when connected to a cellphone. |
| Number | Displays phone numbers being dialed or answered. |
| DND | Do Not Disturb button.  Prevents the phone ringer from ringing when active. |
| Keypad | Used to dial when the phone is off hook.  May also be used to send DTMF tones during a call when using a rotary phone. |
| Backspace | Deletes the last entered digit (from the Keypad or telephone) when dialing a number. |
| Dial | Initiates a call or terminates an ongoing call. weeBell will also initiate a call 3 seconds the last digit is dialed from the telephone. |
| Settings | Display the Settings screen. |
| Mute | Mute button.  Mutes audio from the telephone handset microphone when active. |
| Power | Power status.  Displays battery level and a charge icon when charging. |


#### Settings Screen

![weeBell settings screen controls](pictures/weeBell_settings_controls.png)

| Control | Description |
| --- | --- |
| Bluetooth | Bluetooth pairing control.  Allows pairing and forgetting a pairing.  Displays the currently paired device name. |
| Backlight | Backlight brightness and auto-dim enable.  Enabling Auto Dim causes the backlight to dim after 20 seconds of inactivity.  Any user interface or call activity will return the backlight to the brightness set by the slider. |
| Locale | Sets the device location which customizes the ring, dial tone and other tones generated by weeBell. |
| Volume | Sets the telephone handset microphone and speaker volume. |
| Back | Return to the Main screen. |

### Using weeBell

### Bluetooth connection
weeBell attempts to connect with a paired cellphone once every sixty seconds.

#### Tones
weeBell generates the following dial tones when the handset if off-hook.

1. Dial tone - weeBell is connected a cellphone and capable of making a call.
2. Reorder tone - weeBell is not connected to a cellphone and not capable of making a call.
3. Off-hook tone - The handset has been off-hook for more than sixty seconds with no dialing activity.

All tones are country-specific and controlled by the Locale control.

#### Dialing
weeBell can initiate a phone call through the cellphone in the following ways when the POTs handset is taken off-hook.

1. Telephone number dialed on the POTs rotary dial or DTMF keypad.  weeBell will initiate a phone call 3 seconds after the last digit is dialed so it is important to dial all the digits at once.
2. Telephone number dialed on weeBell keypad followed by Dial button.  The weeBell keypad may also be used to send DTMF tones during a phone call (for example when responding to an auto-attendant while using a rotary telephone).
3. Voice command using cellphone "Hey Siri" or "Hey Google" features.  Dialing 0 on the telephone (or keypad followed by Dial) initiates a voice command.  The functionality must be enabled on the cellphone.  You can issue the voice command after hearing the cellphone-specific prompt in the earpiece.

#### Answering
Incoming calls will cause the POTs telephone to ring (unless Do Not Disturb as been activated).  Picking up the handset will answer the call and route audio to it.

#### Ending a call
A call is ended when the handset is placed back on-hook or the Dial button is pressed on weeBell.  During a call the Dial button turns into a red icon of an off-hook handset.

#### Auto power on
weeBell is designed to be left running constantly using an external USB-C power supply.  It will run from battery power when power fails.  It will automatically turn off when the battery voltage goes below 3.5V to protect the LiPo battery from over-discharge.  In this case it will power-on automatically when external USB power is re-applied.

weeBell can also be turned on and off using the power button on gCore.  Press the power button for about half of a second and release.  When turned off manually, weeBell will not automatically power on when external USB power is (re)applied.

In the case were weeBell firmware crashes then it is possible to turn the device off by pressing and holding the gCore power button for more than five seconds.

#### Error messages
weeBell may display a pop-up dialog box if certain internal errors are detected.  In this case normal operation is suspended and the dialog box prompts to turn weeBell off so the error may be attended to.  The mostly likely cause of this error is if the gCore POTS shield is not [correctly] connected to gCore and the firmware cannot initialize the codec chip via I2C.

