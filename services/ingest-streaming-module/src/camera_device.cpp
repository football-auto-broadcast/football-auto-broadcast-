#include "camera_device.h"
#include <iostream>
#include <algorithm>
#include <vector>

CameraDevice::CameraDevice(const std::string& serial, int cameraId, const std::string& role)
    : m_serial(serial), m_cameraId(cameraId), m_role(role) {}

CameraDevice::~CameraDevice() {
    Stop();
    Close();
}

bool CameraDevice::Initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // MV_CC_Initialize() must be called once before any camera operations
    // It's a global SDK initialization (transport layer)
    static bool sdkInitialized = false;
    static std::mutex initMutex;
    std::lock_guard<std::mutex> initLock(initMutex);
    if (!sdkInitialized) {
        int initRet = MV_CC_Initialize();
        if (initRet != MV_OK) {
            std::cout << "[ERROR] Failed to initialize MVS SDK. Error: " << initRet << std::endl;
            return false;
        }
        sdkInitialized = true;
        std::cout << "[INFO] MVS SDK initialized" << std::endl;
    }

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

            m_isOnline = true;
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    // Always reset status to idle, even if handle is null
    // (handles reconnection after initialization failure)
    if (m_cameraHandle != nullptr) {
        Stop();
        MV_CC_CloseDevice(m_cameraHandle);
        MV_CC_DestroyHandle(m_cameraHandle);
        m_cameraHandle = nullptr;
        std::cout << "[INFO] Camera " << m_serial << " closed" << std::endl;
    }
    
    m_status = Status::idle;
    m_isOnline = false;
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_status;
}

uint64_t CameraDevice::GetFrameCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_frameCount;
}

bool CameraDevice::IsOnline() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_isOnline;
}

int64_t CameraDevice::GetLastFrameTimeMs() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_cameraInfo;
}

void __stdcall CameraDevice::ImageCallbackEx(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo, void* pUser) {
    if (pData == nullptr || pFrameInfo == nullptr || pUser == nullptr) {
        return;
    }
    
    CameraDevice* camera = reinterpret_cast<CameraDevice*>(pUser);
    camera->OnFrameReceived(pData, pFrameInfo);
}

// ============================================================================
// SIMPLE & CORRECT Bayer -> RGB24 conversion (2x2 block demosaic)
// ============================================================================
// 4 patterns (standard GigE Vision / CMOS sensor layouts):
//   GR: | G R |   RG: | R G |   GB: | G B |   BG: | B G |
//       | B G |       | G B |       | R G |       | G R |
//
// Algorithm: for each 2x2 block, the 4 pixels contain 1-R, 2-G, 1-B
// Assign R, G=(G1+G2)/2, B to ALL 4 output pixels -> simple & correct
// ============================================================================

enum class BayerPattern {
    GR,   // row0: G R, row1: B G
    RG,   // row0: R G, row1: G B
    GB,   // row0: G B, row1: R G
    BG    // row0: B G, row1: G R
};

// Get R, G, B positions in a 2x2 block for a given pattern
// Returns index (0,1,2,3) in row-major order of 2x2 block
static inline void GetPatternPositions(BayerPattern pattern,
                                        int& rPos, int& g1Pos, int& g2Pos, int& bPos) {
    // Block indices (row-major): 0=(0,0) 1=(0,1) 2=(1,0) 3=(1,1)
    switch (pattern) {
        case BayerPattern::GR:  // G R / B G
            rPos = 1;  g1Pos = 0;  bPos = 2;  g2Pos = 3;  break;
        case BayerPattern::RG:  // R G / G B
            rPos = 0;  g1Pos = 1;  bPos = 3;  g2Pos = 2;  break;
        case BayerPattern::GB:  // G B / R G
            rPos = 2;  g1Pos = 0;  bPos = 1;  g2Pos = 3;  break;
        case BayerPattern::BG:  // B G / G R
            rPos = 3;  g1Pos = 1;  bPos = 0;  g2Pos = 2;  break;
        default:
            rPos = 1;  g1Pos = 0;  bPos = 2;  g2Pos = 3;
    }
}

// Simple 2x2 block demosaic (guaranteed correct color separation)
static inline void Bayer8ToRGB24_Block(const unsigned char* src, unsigned char* dst,
                                        unsigned int width, unsigned int height,
                                        BayerPattern pattern)
{
    int rPos, g1Pos, g2Pos, bPos;
    GetPatternPositions(pattern, rPos, g1Pos, g2Pos, bPos);

    // Process row pairs
    for (unsigned int y = 0; y < height - 1; y += 2) {
        for (unsigned int x = 0; x < width - 1; x += 2) {
            // Read 2x2 block
            unsigned char p00 = src[y * width + x];
            unsigned char p01 = src[y * width + x + 1];
            unsigned char p10 = src[(y + 1) * width + x];
            unsigned char p11 = src[(y + 1) * width + x + 1];
            const unsigned char block[4] = { p00, p01, p10, p11 };

            unsigned char rVal = block[rPos];
            unsigned char gVal = (unsigned char)(((int)block[g1Pos] + (int)block[g2Pos] + 1) / 2);
            unsigned char bVal = block[bPos];

            // Assign same R,G,B to all 4 output pixels
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    unsigned int outIdx = ((y + dy) * width + (x + dx)) * 3;
                    dst[outIdx]     = rVal;
                    dst[outIdx + 1] = gVal;
                    dst[outIdx + 2] = bVal;
                }
            }
        }
    }
    // Handle last row (if height is odd) - duplicate previous row
    if (height % 2 == 1) {
        unsigned int lastY = height - 1;
        for (unsigned int x = 0; x < width; x++) {
            unsigned int srcIdx = ((lastY - 1) * width + x) * 3;
            unsigned int dstIdx = (lastY * width + x) * 3;
            dst[dstIdx]     = dst[srcIdx];
            dst[dstIdx + 1] = dst[srcIdx + 1];
            dst[dstIdx + 2] = dst[srcIdx + 2];
        }
    }
    // Handle last column (if width is odd)
    if (width % 2 == 1) {
        unsigned int lastX = width - 1;
        for (unsigned int y = 0; y < height; y++) {
            unsigned int srcIdx = (y * width + (lastX - 1)) * 3;
            unsigned int dstIdx = (y * width + lastX) * 3;
            dst[dstIdx]     = dst[srcIdx];
            dst[dstIdx + 1] = dst[srcIdx + 1];
            dst[dstIdx + 2] = dst[srcIdx + 2];
        }
    }
}

// Auto contrast stretch: 8-bit Bayer data -> stretch to use full 0-255 range
// Preserves color relationships (R,G,B ratios), only makes image brighter/visible
static inline void AutoContrastStretch(std::vector<unsigned char>& data) {
    if (data.empty()) return;
    unsigned char vmin = 255;
    unsigned char vmax = 0;
    // Sample 1024 pixels spread evenly
    int step = std::max(1, (int)(data.size() / 1024));
    for (size_t i = 0; i < data.size(); i += step) {
        if (data[i] < vmin) vmin = data[i];
        if (data[i] > vmax) vmax = data[i];
    }
    if (vmax <= vmin) return;
    // Use 2nd percentile / 98th percentile to ignore outliers
    int hist[256] = {0};
    for (size_t i = 0; i < data.size(); i += step) hist[data[i]]++;
    int total = 0; for (int i = 0; i < 256; i++) total += hist[i];
    int loCnt = 0, hiCnt = 0, loThresh = total * 2 / 100, hiThresh = total * 98 / 100;
    int lo = 0, hi = 255;
    for (int i = 0; i < 256; i++) { loCnt += hist[i]; if (loCnt > loThresh) { lo = i; break; } }
    for (int i = 255; i >= 0; i--) { hiCnt += hist[i]; if ((total - hiCnt) < hiThresh) { hi = i; break; } }
    if (hi <= lo) return;
    // Stretch: out = (in - lo) * 255 / (hi - lo)
    int range = hi - lo;
    for (size_t i = 0; i < data.size(); i++) {
        int v = ((int)data[i] - lo) * 255 / range;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        data[i] = (unsigned char)v;
    }
}

// Bayer12 (16-bit padded) -> 8-bit Bayer, auto-stretch, then demosaic
//
// RAW DATA DIAGNOSTIC: srcRaw bytes = 02 80 02 7F 03 80 02 7F ...
//   srcRaw[i*2]   = 02, 02, 03, 02, 03, 04, 03, 03 (high nibble/byte)
//   srcRaw[i*2+1] = 80, 7F, 80, 7F, 7F, 80, 7F, 80 (low byte)
//
// GigE Vision Bayer12 IS big-endian on wire. MVS returns raw wire bytes.
// On little-endian host:
//   *src16 = srcRaw[i*2+1] * 256 + srcRaw[i*2] = 0x8002 = 32770 (WRONG endian)
//   correct 12-bit value (big-endian) = srcRaw[i*2] * 256 + srcRaw[i*2+1] = 0x0280 = 640
//   BUT 640 exceeds 4095 only if > 4095, so 640 is fine as 12-bit value
//   8-bit scaled = v12 * 255 / 4095
//
// HOWEVER: there are TWO common MVS 12-bit memory layouts:
//   Layout A (left-aligned, big-endian wire): bits[15:4]=data, bits[3:0]=0
//     → 12-bit value = (big-endian u16) >> 4
//     → 0x0280 >> 4 = 0x0028 = 40 → 8-bit = 2.5 (too dark)
//   Layout B (right-aligned, big-endian wire): bits[11:0]=data
//     → 12-bit value = big-endian u16 & 0xFFF
//     → 0x0280 & 0xFFF = 0x280 = 640 → 8-bit = 640*255/4095 = 39.8 (reasonable)
//
// Also need to consider: maybe MVS already swapped to host byte order for us,
// meaning the first byte IS the high byte of value already in little-endian:
//   src16[i] on little-endian host = srcRaw[i*2] + srcRaw[i*2+1]*256 = 0x8002
//   If MVS returned host-order with 12-bit value = 0x0280: we should bswap16 first
static inline void Bayer12ToRGB24_Block(const unsigned char* srcRaw, unsigned char* dst,
                                         unsigned int width, unsigned int height,
                                         BayerPattern pattern)
{
    size_t n = (size_t)width * height;
    std::vector<unsigned char> bayer8(n);
    const unsigned short* src16 = reinterpret_cast<const unsigned short*>(srcRaw);

    // Full diagnostic: print raw bytes + multiple extraction methods
    static bool diagPrinted = false;
    if (!diagPrinted) {
        diagPrinted = true;
        fprintf(stderr, "\n=== BAYER12 DIAGNOSTIC ===\n");
        fprintf(stderr, "  first 16 bytes: ");
        for (int i = 0; i < 16; i++) fprintf(stderr, "%02X ", srcRaw[i]);
        fprintf(stderr, "\n");
        fprintf(stderr, "  pixel values (first 8) with different extraction methods:\n");
        for (int i = 0; i < 8 && (size_t)i < n; i++) {
            unsigned char b0 = srcRaw[i*2];
            unsigned char b1 = srcRaw[i*2+1];
            unsigned short u_le = b0 | ((unsigned short)b1 << 8); // little-endian: low-byte-first
            unsigned short u_be = (unsigned short)((b0 << 8) | b1);  // big-endian: first-byte-is-high
            // Various methods to get 8-bit value:
            int m0 = b0;                                          // raw first byte
            int m1 = b1;                                          // raw second byte
            int m2 = (u_le >> 8) & 0xFF;                          // high byte of little-endian u16 (=b1)
            int m3 = u_le & 0xFF;                                 // low byte of little-endian u16 (=b0)
            int m4 = (u_be >> 8) & 0xFF;                          // high byte of big-endian u16 (=b0)
            int m5 = u_be & 0xFF;                                 // low byte of big-endian u16 (=b1)
            int m6 = ((u_be & 0xFFF) * 255 + 2047) / 4095;        // big-endian low 12-bit scaled
            int m7 = ((u_le & 0xFFF) * 255 + 2047) / 4095;        // little-endian low 12-bit scaled
            int m8 = (u_le * 255 + 32767) / 65535;                // little-endian 16-bit full scaled
            int m9 = (u_be * 255 + 32767) / 65535;                // big-endian 16-bit full scaled
            fprintf(stderr, "  pix[%d] bytes=%02X%02X u_le=%04X(%5d) u_be=%04X(%5d) | m0=b0=%d m1=b1=%d m2=hiLE=%d m3=loLE=%d m4=hiBE=%d m5=loBE=%d m6=be12=%d m7=le12=%d m8=le16=%d m9=be16=%d\n",
                    i, b0, b1, u_le, u_le, u_be, u_be, m0, m1, m2, m3, m4, m5, m6, m7, m8, m9);
        }
        // Print min/max statistics for a larger sample to determine which method gives reasonable 0-255 range
        int minVals[10] = {255,255,255,255,255,255,255,255,255,255};
        int maxVals[10] = {0,0,0,0,0,0,0,0,0,0};
        int sampleCount = std::min(512, (int)n);
        for (int i = 0; i < sampleCount; i++) {
            unsigned char b0 = srcRaw[i*2];
            unsigned char b1 = srcRaw[i*2+1];
            unsigned short u_le = b0 | ((unsigned short)b1 << 8);
            unsigned short u_be = (unsigned short)((b0 << 8) | b1);
            int vs[10] = {b0, b1, (u_le>>8)&0xFF, u_le&0xFF, (u_be>>8)&0xFF, u_be&0xFF,
                          ((u_be&0xFFF)*255+2047)/4095, ((u_le&0xFFF)*255+2047)/4095,
                          (u_le*255+32767)/65535, (u_be*255+32767)/65535};
            for (int m = 0; m < 10; m++) {
                if (vs[m] < minVals[m]) minVals[m] = vs[m];
                if (vs[m] > maxVals[m]) maxVals[m] = vs[m];
            }
        }
        fprintf(stderr, "  stats (512 sample): m0=[%3d-%3d] m1=[%3d-%3d] m2=[%3d-%3d] m3=[%3d-%3d] m4=[%3d-%3d] m5=[%3d-%3d] m6=[%3d-%3d] m7=[%3d-%3d] m8=[%3d-%3d] m9=[%3d-%3d]\n",
                minVals[0],maxVals[0], minVals[1],maxVals[1], minVals[2],maxVals[2],
                minVals[3],maxVals[3], minVals[4],maxVals[4], minVals[5],maxVals[5],
                minVals[6],maxVals[6], minVals[7],maxVals[7], minVals[8],maxVals[8], minVals[9],maxVals[9]);
        fprintf(stderr, "  Good methods have range ~20-230 (avoid flat 0-0 or values always <20)\n");
        fprintf(stderr, "=========================================\n");
        fflush(stderr);
    }

    // Use big-endian 12-bit scaled (method m6). If wrong, we'll revise based on diagnostic output.
    for (size_t i = 0; i < n; i++) {
        unsigned char b0 = srcRaw[i*2];
        unsigned char b1 = srcRaw[i*2+1];
        unsigned short u_be = (unsigned short)((b0 << 8) | b1);
        int v8 = ((u_be & 0xFFF) * 255 + 2047) / 4095;
        if (v8 > 255) v8 = 255;
        bayer8[i] = (unsigned char)v8;
    }

    // Auto-contrast stretch: handles underexposed sensors
    AutoContrastStretch(bayer8);
    Bayer8ToRGB24_Block(bayer8.data(), dst, width, height, pattern);
}

// Bayer8: optional auto-contrast stretch, then demosaic
static inline void Bayer8ToRGB24_BlockWithStretch(const unsigned char* src, unsigned char* dst,
                                                   unsigned int width, unsigned int height,
                                                   BayerPattern pattern)
{
    size_t n = (size_t)width * height;
    std::vector<unsigned char> buf(n);
    for (size_t i = 0; i < n; i++) buf[i] = src[i];

    // Diagnostic: print Bayer8 statistics (comparison with Bayer12)
    static bool diag8Printed = false;
    if (!diag8Printed) {
        diag8Printed = true;
        fprintf(stderr, "=== BAYER8 DIAGNOSTIC ===\n");
        fprintf(stderr, "  first 16 bytes: ");
        for (int i = 0; i < 16; i++) fprintf(stderr, "%02X ", src[i]);
        fprintf(stderr, "\n");
        fprintf(stderr, "  first 8 pixel values: ");
        for (int i = 0; i < 8; i++) fprintf(stderr, "%d ", (int)src[i]);
        int mn = 255, mx = 0;
        int s = std::min(512, (int)n);
        for (int i = 0; i < s; i++) {
            if (src[i] < mn) mn = src[i];
            if (src[i] > mx) mx = src[i];
        }
        fprintf(stderr, "\n  stats (512 sample): [%d-%d]\n", mn, mx);
        fprintf(stderr, "  (For comparison: Bayer12 should have similar pixel distribution)\n");
        fprintf(stderr, "=========================\n");
        fflush(stderr);
    }

    AutoContrastStretch(buf);
    Bayer8ToRGB24_Block(buf.data(), dst, width, height, pattern);
}

// Pattern selector per camera
static inline BayerPattern DetectBayerPattern(unsigned int pixelType, const std::string& serial) {
    // Try GR first (most common for Hikvision CMOS sensors)
    // Camera-specific patterns
    // F92514845 (Bayer8): GR (verified - produces normal color image)
    // D91363830 (Bayer12): try BG first (most sensors use GR/BG based on
    //                       how the first read-out pixel is interpreted)
    if (serial == "F92514845") return BayerPattern::GR;
    if (serial == "D91363830") return BayerPattern::BG;
    return BayerPattern::GR;
}

void CameraDevice::OnFrameReceived(unsigned char* pData, MV_FRAME_OUT_INFO_EX* pFrameInfo) {
    // Immediate debug output to stderr (unbuffered) - this ALWAYS prints
    static std::atomic<uint64_t> debugFrameCount {0};
    uint64_t cnt = debugFrameCount.fetch_add(1) + 1;
    if (cnt <= 5) {
        fprintf(stderr, "[DEBUG-CALLBACK] Camera %s got frame #%llu width=%u height=%u frameLen=%u pixelType=%d\n",
                m_serial.c_str(), (unsigned long long)cnt,
                pFrameInfo ? pFrameInfo->nWidth : 0,
                pFrameInfo ? pFrameInfo->nHeight : 0,
                pFrameInfo ? pFrameInfo->nFrameLen : 0,
                pFrameInfo ? (int)pFrameInfo->enPixelType : -1);
        fflush(stderr);
    }

    try {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_frameCount++;
        m_isOnline = true;
        m_lastFrameTime = std::chrono::steady_clock::now();

        unsigned int width = pFrameInfo->nWidth;
        unsigned int height = pFrameInfo->nHeight;
        unsigned int pixelType = static_cast<unsigned int>(pFrameInfo->enPixelType);
        size_t expectedRgbSize = static_cast<size_t>(width) * height * 3;

        unsigned char* rgbData = new unsigned char[expectedRgbSize];
        size_t rgbSize = expectedRgbSize;
        bool useSoftware = true;

        // Try SDK conversion first
        {
            MV_CC_PIXEL_CONVERT_PARAM stConvertParam = { 0 };
            stConvertParam.nWidth = static_cast<unsigned short>(width);
            stConvertParam.nHeight = static_cast<unsigned short>(height);
            stConvertParam.pSrcData = pData;
            stConvertParam.nSrcDataLen = pFrameInfo->nFrameLen;
            stConvertParam.enSrcPixelType = static_cast<enum MvGvspPixelType>(pixelType);
            stConvertParam.enDstPixelType = PixelType_Gvsp_RGB8_Packed;
            stConvertParam.pDstBuffer = rgbData;
            stConvertParam.nDstBufferSize = static_cast<unsigned int>(expectedRgbSize);

            int ret = MV_CC_ConvertPixelType(m_cameraHandle, &stConvertParam);
            if (ret == MV_OK) {
                rgbSize = stConvertParam.nDstLen;
                useSoftware = false;
            }
        }

        // Fallback: software Bayer demosaic to RGB24
        if (useSoftware) {
            BayerPattern pattern = DetectBayerPattern(pixelType, m_serial);
            bool isBayer8 = (pFrameInfo->nFrameLen == static_cast<size_t>(width) * height);

            if (isBayer8) {
                Bayer8ToRGB24_BlockWithStretch(pData, rgbData, width, height, pattern);
            } else {
                Bayer12ToRGB24_Block(pData, rgbData, width, height, pattern);
            }
            rgbSize = expectedRgbSize;

            if (m_frameCount <= 3) {
                const char* patternName =
                    (pattern == BayerPattern::GR) ? "GR" :
                    (pattern == BayerPattern::RG) ? "RG" :
                    (pattern == BayerPattern::GB) ? "GB" : "BG";
                fprintf(stderr, "[INFO] Camera %s pattern=%s format=%s auto-contrast\n",
                        m_serial.c_str(), patternName, isBayer8 ? "Bayer8" : "Bayer12");
                fflush(stderr);
            }
        }

        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);

            if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
                m_frameQueue.pop();
            }

            MV_FRAME_OUT_INFO_EX rgbInfo = *pFrameInfo;
            rgbInfo.nFrameLen = static_cast<unsigned int>(rgbSize);
            rgbInfo.enPixelType = PixelType_Gvsp_RGB8_Packed;

            m_frameQueue.push(std::make_unique<FrameData>(rgbData, rgbInfo));
        }

        m_cv.notify_one();

        if (m_frameCount <= 3) {
            std::cout << "[INFO] Camera " << m_serial
                      << " frame #" << m_frameCount
                      << " " << width << "x" << height
                      << " srcType=" << pixelType
                      << " srcSize=" << pFrameInfo->nFrameLen
                      << " -> RGB24 size=" << rgbSize
                      << (useSoftware ? " (software)" : " (SDK)") << std::endl;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[ERROR-CALLBACK] Camera %s exception: %s\n", m_serial.c_str(), e.what());
        fflush(stderr);
    } catch (...) {
        fprintf(stderr, "[ERROR-CALLBACK] Camera %s unknown exception\n", m_serial.c_str());
        fflush(stderr);
    }
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