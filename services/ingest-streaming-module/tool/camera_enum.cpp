#include <iostream>
#include <cstring>
#include "MvCameraControl.h"

void PrintError(unsigned int errCode, const char* msg)
{
    std::cout << "[ERROR] " << msg << " | Error Code: 0x" << std::hex << errCode << std::dec << std::endl;
}

int main()
{
    std::cout << "===== MVS 5.0 Camera Enumeration Tool =====" << std::endl;
    std::cout << "Enumerating GigE/USB cameras..." << std::endl << std::endl;

    MV_CC_DEVICE_INFO_LIST devList;
    memset(&devList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));
    unsigned int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devList);

    if (ret != MV_OK)
    {
        PrintError(ret, "Failed to enumerate cameras. Please check MVS driver installation.");
        system("pause");
        return -1;
    }

    if (devList.nDeviceNum == 0)
    {
        std::cout << "[INFO] No cameras detected. Please check:" << std::endl;
        std::cout << "1. PoE switch power supply" << std::endl;
        std::cout << "2. Camera and PC are on the same network segment (default 192.168.10.x)" << std::endl;
        std::cout << "3. Network cable connection" << std::endl;
    }
    else
    {
        std::cout << "[SUCCESS] Detected " << devList.nDeviceNum << " cameras:" << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        for (unsigned int i = 0; i < devList.nDeviceNum; i++)
        {
            MV_CC_DEVICE_INFO* dev = devList.pDeviceInfo[i];
            if (!dev) continue;

            std::cout << "Camera Index: " << i + 1 << std::endl;
            std::cout << "Model: " << dev->SpecialInfo.stGigEInfo.chModelName << std::endl;
            std::cout << "Serial Number: " << dev->SpecialInfo.stGigEInfo.chSerialNumber << std::endl;
            std::cout << "IP Address: " << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 24) & 0xFF) << "." 
                      << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 16) & 0xFF) << "." 
                      << ((dev->SpecialInfo.stGigEInfo.nCurrentIp >> 8) & 0xFF) << "." 
                      << (dev->SpecialInfo.stGigEInfo.nCurrentIp & 0xFF) << std::endl;
            std::cout << "Subnet Mask: " << ((dev->SpecialInfo.stGigEInfo.nCurrentSubNetMask >> 24) & 0xFF) << "." 
                      << ((dev->SpecialInfo.stGigEInfo.nCurrentSubNetMask >> 16) & 0xFF) << "." 
                      << ((dev->SpecialInfo.stGigEInfo.nCurrentSubNetMask >> 8) & 0xFF) << "." 
                      << (dev->SpecialInfo.stGigEInfo.nCurrentSubNetMask & 0xFF) << std::endl;
            std::cout << "Gateway: " << ((dev->SpecialInfo.stGigEInfo.nDefultGateWay >> 24) & 0xFF) << "." 
                      << ((dev->SpecialInfo.stGigEInfo.nDefultGateWay >> 16) & 0xFF) << "." 
                      << ((dev->SpecialInfo.stGigEInfo.nDefultGateWay >> 8) & 0xFF) << "." 
                      << (dev->SpecialInfo.stGigEInfo.nDefultGateWay & 0xFF) << std::endl;
            std::cout << "----------------------------------------" << std::endl;
        }
    }

    std::cout << std::endl << "Enumeration completed. Press any key to exit..." << std::endl;
    system("pause");
    return 0;
}