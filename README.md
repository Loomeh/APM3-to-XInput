# APM3 To XInput

APM3 To XInput is a project that attempts to remap arcade stick inputs from Sega's proprietary I/O boards used in the ALL.Net P-ras MULTI version 3 arcade cabinets to standard XInput to improve their compatibility with PC games and emulators.

> [!WARNING]
> This has currently not been tested on real APM3/ALLS hardware.

## Prerequisites

- Modern version of Windows 10/11
- [HidHide](https://github.com/nefarius/HidHide/releases)
- Administrator access (this is needed to create the virtual XInput pads)

## How to Download

Navigate to the [Releases](https://github.com/Loomeh/APM3-to-XInput/releases/latest) and click on `APM3-to-XInput.zip` to download the ZIP file containing the program.

---

## How to configure APM3-to-Xinput

The program is configured with an `APM-to-XInput.ini` file placed next to the executable.

Here is an example of what the configuration file should look like:

```ini
[Controller1]
Device=HID\VID_054C&PID_0CE6&MI_03\A&38240C49&0&0000
A=2
B=3
X=1
Y=4
LB=5
RB=6
LT=7
RT=8
LS=22
Up=Hat:Up
Down=Hat:Down
Left=Hat:Left
Right=Hat:Right
Start=10
Back=9

[Controller2]
Device=HID\VID_0F0D&PID_0202\9&E39F7CD&0&0000
A=2
B=3
X=1
Y=4
LB=5
RB=6
LT=7
RT=8
LS=22
Up=Hat:Up
Down=Hat:Down
Left=Hat:Left
Right=Hat:Right
Start=10
Back=9
```

_A template file with more information is available in the repository (find it at the top of this web page)._

### Finding device instance paths

You will need to provide a device instance path to each controller, which can be found by running `list.bat` or `APM-to-XInput.exe --list`. This will also tell you how many buttons and what analog inputs are available for each controller.

```text
DualSense Wireless Controller
 Device=HID\VID_054C&PID_0CE6&MI_03\A&38240C49&0&0000
 buttons 1-15, axes Ry Rx Rz Z Y X, hat

POKKEN CONTROLLER
 Device=HID\VID_0F0D&PID_0202\9&E39F7CD&0&0000
 buttons 1-14,15, axes Rz Z Y X, hat
```

### Finding button values

Button values can be found by searching "Set up USB game controllers" in the Windows start menu, clicking on a controller, and seeing what physical button presses light up which values.

<img width="400" height="455" alt="image" src="https://github.com/user-attachments/assets/9245f868-f0f6-49b9-8c4f-61eb25e08721" />

---

## How to configure HidHide

Open up the HidHide configuration client and in the Applications tab add `APM3-to-XInput.exe` to the list of applications that can see past its hiding.

<img width="730" height="329" alt="image" src="https://github.com/user-attachments/assets/16500353-4626-4102-aa5c-160c85704927" />

In the Devices tab, disable the original I/O boards and toggle "Enable device hiding".

<img width="730" height="329" alt="image" src="https://github.com/user-attachments/assets/d88b8415-273a-4112-a19e-b634eaf57812" />

---

## How to run

Provided you configured everything correctly, you should be able to click on `APM3-to-XInput.exe` and have everything work.

---

## How to report issues

Open an issue on GitHub or contact loomeh on Discord

---

## Credits

[GipSyntheticEx](https://github.com/InvoxiPlayGames/GipSyntheticEx) by InvoxiPlayGames
