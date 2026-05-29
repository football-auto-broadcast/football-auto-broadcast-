#include "camera_device.h"
#include <iostream>
#include <algorithm>

CameraDevice::CameraDevice(const std::string& serial, int cameraId, const std::string& role)
    : m_serial(serial), m_cameraId(cameraId), m_role(role) {}

CameraDevice::~CameraDevice() {
    Stop();
    Close();
}

bool CameraDevice::Initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status != Status::idle) {
        std::cout << "[WARN] Camera " << m_serial << " is not in idle state" << std::endl;
        return false;
    }
    
    m_status = Status::initializing;
    
    MV_CC_DEVICE_INFO_LIST devList = { 0 };
    int ret = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devList);
    if (ret != MV_OK) {
        std::cout << "[ERROR] Camera " << m_serial << " failed to enumerate devices. Error: " << ret << std::endl;
        m_status = Status::failed;
        return false;
    }
    
    for (unsigned int i = 0; i < devList.nDeviceNum; i++) {
        MV_CC_DEVICE_INFO* devInfo = devList.pDeviceInfo[i];
        std::string devSerial = (char*)devInfo->SpecialInfo.stGigEInfo.chSerialNumber;
        
        if (devSerial == m_serial) {
            ret = MV_CC_CreateHandle(&m_cameraHandle, devInfo);
            if (ret != MV_OK) {
                std::cout << "[ERROR] Camera " << m_serial << " failed to create handle. Error: " << ret << std::endl;
                m_status = Status::failed;
                return false;
            }
            
            ret = MV_CC_OpenDevice(m_cameraHandle);
            if (ret != MV_OK) {
                std::cout << "[ERROR] Camera " << m_serial << " failed to open device. Error: " << ret << std::endl;
                MV_CC_DestroyHandle(m_cameraHandle);
                m_cameraHandle = nullptr;
                m_status = Status::failed;
                return false;
            }
            
            if (!GetCameraInfoFromSDK()) {
                std::cout << "[WARN] Camera " << m_serial << " failed to get camera info" << std::endl;
            }
            
            m_status = Status::stopped;
            std::cout << "[SUCCESS] Camera " << m_serial << " initialized" << std::endl;
            return true;
        }
    }
    
    std::cout << "[WARN] Camera " << m_serial << " not found" << std::endl;
    m_status = Status::failed;
    return false;
}

bool CameraDevice::Open() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_cameraHandle == nullptr) {
        if (!Initialize()) {
            return false;
        }
    }
    
    if (m_status == Status::running) {
        std::cout << "[WARN] Camera " << m_serial << " is already running" << std::endl;
        return true;
    }
    
    int ret = MV_CC_OpenDevice(m_cameraHandle);
    if (ret != MV_OK) {
        std::cout << "[ERROR] Camera " << m_serial << " failed to open device. Error: " << ret << std::endl;
        m_status = Status::failed;
        return false;
    }
    
    if (!GetCameraInfoFromSDK()) {
        std::cout << "[WARN] Camera " << m_serial << " failed to get camera info" << std::endl;
    }
    
    m_status = Status::stopped;
    std::cout << "[INFO] Camera " << m_serial << " opened" << std::endl;
    return true;
}

bool CameraDevice::StartGrabbing() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_cameraHandle == nullptr) {
        std::cout << "[ERROR] Camera " << m_serial << " handle is null" << std::endl;
        return false;
    }
    
    if (m_status == Status::running) {
        std::cout << "[WARN] Camera " << m_serial << " is already grabbing" << std::endl;
        return true;
    }
    
    int ret = MV_CC_RegisterImageCallBackEx(m_cameraHandle, ImageCallbackEx, this);
    if (ret != MV_OK) {
        std::cout << "[ERROR] Camera " << m_serial << " failed to register callback. Error: " << ret << std::endl;
        return false;
    }
    
    ret = MV_CC_StartGrabbing(m_cameraHandle);
    if (ret != MV_OK) {
        std::cout << "[ERROR] Camera " << m_serial << " failed to start grabbing. Error: " << ret << std::endl;
        return false;
    }
    
    m_frameCount = 0;
    m_isOnline = true;
    m_lastFrameTime = std::chrono::steady_clock::now();
    m_status = Status::running;
    std::cout << "[SUCCESS] Camera " << m_serial << " started grabbing" << std::endl;
    return true;
}

void CameraDevice::Stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_status != Status::running) {
        return;
    }
    
    if (m_cameraHandle != nullptr) {
        MV_CC_StopGrabbing(m_cameraHandle);
    }
    
    m_isOnline = false;
    m_status = Status::stopped;
    ClearFrameQueue();
    std::cout << "[INFO] Camera " << m_serial << " stopped grabbing" << std::endl;
}

void CameraDevice::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_cameraHandle == nullptr) {
        return;
    }
    
    Stop();
    
    MV_CC_CloseDevice(m_cameraHandle);
    MV_CC_DestroyHandle(m_cameraHandle);
    m_cameraHandle = nullptr;
    m_status = Status::idle;
    std::cout << "[INFO] Camera " << m_serial << " closed" << std::endl;
}

bool CameraDevice::Reconnect() {
    std::cout << "[INFO] Camera " << m_serial << " reconnecting..." << std::endl;
    
    Close();
    
    if (!Initialize()) {
        std::cout << "[ERROR] Camera " << m_serial << " reconnect failed" << std::endl;
        return false;
    }
    
    if (!StartGrabbing()) {
        std::cout << "[ERROR] Camera " << m_serial << " failed to start grabbing after reconnect" << std::endl;
        return false;
    }
    
    std::cout << "[SUCCESS] Camera " << m_serial << " reconnected" << std::endl;
    return true;
}

CameraDevice::Status CameraDevice::GetStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

uint64_t CameraDevice::GetFrameCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_frameCount;
}

bool CameraDevice::IsOnline() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_isOnline;
}

int64_t CameraDevice::GetLastFrameTimeMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFrameTime).count();
}

bool CameraDevice::GetFrame(FrameData& frame, int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    
    if (m_frameQueue.empty()) {
        if (timeoutMs <= 0) {
            return false;
        }
        
        if (m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !m_frameQueue.empty(); })) {
            if (m_frameQueue.empty()) {
                return false;
            }
        } else {
            return false;
        }
    }
    
    auto framePtr = std::move(m_frameQueue.front());
    m_frameQueue.pop();
    
    frame = std::move(*framePtr);
    return true;
}

void CameraDevice::ClearFrameQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_frameQueue.empty()) {
        m_frameQueue.pop();
    }
}

CameraDevice::CameraInfo CameraDevice::GetCameraInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cameraInfo;
}

void __stdcall CameraDevice::ImageCallbackEx(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser) {
    if (pData == nullptr || pFrameInfo == nullptr || pUser == nullptr) {
        return;
    }
    
    CameraDevice* camera = reinterpret_cast<CameraDevice*>(pUser);
    camera->OnFrameReceived(pData, pFrameInfo);
}

void CameraDevice::OnFrameReceived(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameCount++;
    m_isOnline = true;
    m_lastFrameTime = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> queueLock(m_queueMutex);
        
        if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
            m_frameQueue.pop();
        }
        
        size_t dataSize = pFrameInfo->nFrameLen;
        unsigned char* copyData = new unsigned char[dataSize];
        memcpy(copyData, pData, dataSize);
        
        m_frameQueue.push(std::make_unique<FrameData>(copyData, *pFrameInfo));
    }
    
    m_cv.notify_one();
}

bool CameraDevice::GetCameraInfoFromSDK() {
    if (m_cameraHandle == nullptr) {
        return false;
    }
    
    MVCC_INTVALUE value = { 0 };
    
    int ret = MV_CC_GetIntValue(m_cameraHandle, "Width", &value);
    if (ret == MV_OK) {
        m_cameraInfo.width = value.nCurValue;
    }
    
    ret = MV_CC_GetIntValue(m_cameraHandle, "Height", &value);
    if (ret == MV_OK) {
        m_cameraInfo.height = value.nCurValue;
    }
    
    MVCC_FLOATVALUE floatValue = { 0 };
    ret = MV_CC_GetFloatValue(m_cameraHandle, "AcquisitionFrameRate", &floatValue);
    if (ret == MV_OK) {
        m_cameraInfo.fps = floatValue.fCurValue;
    }
    
    return true;
}