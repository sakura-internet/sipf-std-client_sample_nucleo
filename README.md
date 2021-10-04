# sipf-std-client_sample_nucleo

## Getting start

### About

This software is `sample application of Sakura's MONOPLATFORM for Nucleo`. 

We have checked the following devices.

- Nucleo F411RE


### Install CubeIDE

Download and Install [CubeIDE](https://www.st.com/ja/development-tools/stm32cubeide.html)

### Clone this repository

```
git clone https://github.com/sakura-internet/sipf-std-client_sample_nucleo.git
cd sipf-std-client_sample_nucleo
```

### Import project

1. Open CubeIDE's `Import` dialog.   
(`[File]-[Import\]` menu.)

2. Select `Existing Projects into Workspace` and push `Next>` button.

3. Select the git coloned directory and push `Finish` button.


### Select application

This project included below applications.  
You can select the application by selecting the C preprocessor macro.  

| application | MACRO | CubeIDE build configuration |
|---|---|---|
| TX(uplink) sample | `SAMPLE_TX` | `Debug-TX` or `Release-TX` |
| RX(downlink) sample | `SAMPLE_RX` | `Debug-RX` or `Release-RX` |

We have prepared MACRO defined build configuration for CubeIDE.  
Select bulid configuration from `[Project]-[Build Configurations]-[Set Active]` menu.
