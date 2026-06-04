
"""Qt tool for previewing and capturing from two Hikvision/Hikrobot MVS cameras.

Usage:
  python scripts/hik_dual_camera_qt.py
  python scripts/hik_dual_camera_qt.py --output raw_data/hik_dual_capture
  python scripts/hik_dual_camera_qt.py --sdk-path "C:/Program Files (x86)/MVS/Development/Samples/Python/MvImport"

Dependencies:
  pip install numpy PySide6
  # or: pip install numpy PyQt5

The Hikrobot MVS runtime must be installed. If the SDK Python wrapper cannot be
found automatically, pass --sdk-path to the MvImport directory or set
MVSDK_PYTHON_PATH to that directory.
"""
from __future__ import annotations

import argparse
import ctypes as C
import importlib
import os
import sys
import time
from ctypes import POINTER, byref, c_ubyte, cast, memmove, memset, sizeof
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
from threading import RLock
from typing import Any

try:
    import numpy as np
except ImportError as exc:  # pragma: no cover
    raise SystemExit("NumPy is required: pip install numpy") from exc

try:
    import cv2
except ImportError:  # pragma: no cover
    cv2 = None

try:  # Prefer PySide6, but support PyQt5 because many MVS examples use it.
    from PySide6 import QtCore, QtGui, QtWidgets

    Signal = QtCore.Signal
except ImportError:  # pragma: no cover
    try:
        from PyQt5 import QtCore, QtGui, QtWidgets

        Signal = QtCore.pyqtSignal
    except ImportError as exc:
        raise SystemExit("Qt for Python is required: pip install PySide6  (or pip install PyQt5)") from exc


MVS: Any = None
MV_MAX_DEVICE_NUM = 256
PARAMETERS = (
    ("ExposureTime", "曝光", "us"),
    ("Gain", "增益", "dB"),
    ("AcquisitionFrameRate", "帧率", "fps"),
)
PIXEL_TYPE_MONO8 = 0x01080001
PIXEL_TYPE_BAYER_GR8 = 0x01080008
PIXEL_TYPE_BAYER_RG8 = 0x01080009
PIXEL_TYPE_BAYER_GB8 = 0x0108000A
PIXEL_TYPE_BAYER_BG8 = 0x0108000B
PIXEL_TYPE_RGB8_PACKED = 0x02180014
PIXEL_TYPE_BGR8_PACKED = 0x02180015
PIXEL_TYPE_YUV422_PACKED = 0x0210001F
PIXEL_TYPE_YUV422_YUYV_PACKED = 0x02100032
COLOR_MODE_MONO = "mono"
COLOR_MODE_COLOR = "color"
VIDEO_FPS = 12.0
VIDEO_QUALITY_PRESETS = (
    ("原始", None),
    ("2K (2048x1536)", (2048, 1536)),
    ("1080p (1440x1080)", (1440, 1080)),
    ("720p (960x720)", (960, 720)),
)
COLOR_PIXEL_FORMATS = (
    PIXEL_TYPE_BAYER_BG8,
    PIXEL_TYPE_BAYER_RG8,
    PIXEL_TYPE_BAYER_GB8,
    PIXEL_TYPE_BAYER_GR8,
    PIXEL_TYPE_RGB8_PACKED,
    PIXEL_TYPE_BGR8_PACKED,
    PIXEL_TYPE_YUV422_YUYV_PACKED,
    PIXEL_TYPE_YUV422_PACKED,
)
BAYER_CONVERSIONS = {
    PIXEL_TYPE_BAYER_BG8: getattr(cv2, "COLOR_BAYER_BG2RGB", None) if cv2 else None,
    PIXEL_TYPE_BAYER_RG8: getattr(cv2, "COLOR_BAYER_RG2RGB", None) if cv2 else None,
    PIXEL_TYPE_BAYER_GB8: getattr(cv2, "COLOR_BAYER_GB2RGB", None) if cv2 else None,
    PIXEL_TYPE_BAYER_GR8: getattr(cv2, "COLOR_BAYER_GR2RGB", None) if cv2 else None,
}


def fmt_ret(ret: int) -> str:
    return f"0x{ret & 0xFFFFFFFF:08x}"


def sdk_path_candidates(extra_path: str | None) -> list[Path]:
    candidates: list[Path] = []
    if extra_path:
        candidates.append(Path(extra_path))

    for env_name in ("MVSDK_PYTHON_PATH", "MVS_PYTHON_PATH", "HIKROBOT_MVS_PYTHON_PATH"):
        env_value = os.environ.get(env_name)
        if env_value:
            candidates.append(Path(env_value))

    script_dir = Path(__file__).resolve().parent
    candidates.extend(
        [
            script_dir / "MvImport",
            Path.cwd() / "MvImport",
            Path("C:/Program Files (x86)/MVS/Development/Samples/Python/MvImport"),
            Path("C:/Program Files/MVS/Development/Samples/Python/MvImport"),
            Path("C:/Program Files/HIKROBOT/MVS/Development/Samples/Python/MvImport"),
            Path("C:/Program Files (x86)/HIKROBOT/MVS/Development/Samples/Python/MvImport"),
        ]
    )

    for root in (
        Path("C:/Program Files (x86)/MVS"),
        Path("C:/Program Files/MVS"),
        Path("C:/Program Files/HIKROBOT/MVS"),
        Path("C:/Program Files (x86)/HIKROBOT/MVS"),
    ):
        if root.exists():
            candidates.extend(path.parent for path in root.rglob("MvCameraControl_class.py"))

    unique: list[Path] = []
    seen: set[str] = set()
    for path in candidates:
        key = str(path.resolve()) if path.exists() else str(path)
        if key not in seen:
            seen.add(key)
            unique.append(path)
    return unique


def mvs_runtime_candidates() -> list[Path]:
    return [
        Path("C:/Program Files (x86)/Common Files/MVS/Runtime/Win64_x64"),
        Path("C:/Program Files/Common Files/MVS/Runtime/Win64_x64"),
        Path("C:/Program Files (x86)/MVS/Runtime/Win64_x64"),
        Path("C:/Program Files/MVS/Runtime/Win64_x64"),
    ]


def import_mvs_direct_dll() -> Any:
    for dll_dir in mvs_runtime_candidates():
        if dll_dir.exists():
            os.add_dll_directory(str(dll_dir))
            break
    else:
        raise ImportError("MvCameraControl.dll runtime directory was not found.")

    dll = C.WinDLL("MvCameraControl.dll")

    class MV_GIGE_DEVICE_INFO(C.Structure):
        _fields_ = [
            ("nIpCfgOption", C.c_uint),
            ("nIpCfgCurrent", C.c_uint),
            ("nCurrentIp", C.c_uint),
            ("nCurrentSubNetMask", C.c_uint),
            ("nDefultGateWay", C.c_uint),
            ("chManufacturerName", C.c_ubyte * 32),
            ("chModelName", C.c_ubyte * 32),
            ("chDeviceVersion", C.c_ubyte * 32),
            ("chManufacturerSpecificInfo", C.c_ubyte * 48),
            ("chSerialNumber", C.c_ubyte * 16),
            ("chUserDefinedName", C.c_ubyte * 16),
            ("nNetExport", C.c_uint),
            ("nReserved", C.c_uint * 4),
        ]

    class MV_USB3_DEVICE_INFO(C.Structure):
        _fields_ = [
            ("CrtlInEndPoint", C.c_ubyte),
            ("CrtlOutEndPoint", C.c_ubyte),
            ("StreamEndPoint", C.c_ubyte),
            ("EventEndPoint", C.c_ubyte),
            ("idVendor", C.c_ushort),
            ("idProduct", C.c_ushort),
            ("nDeviceNumber", C.c_uint),
            ("chDeviceGUID", C.c_ubyte * 64),
            ("chVendorName", C.c_ubyte * 64),
            ("chModelName", C.c_ubyte * 64),
            ("chFamilyName", C.c_ubyte * 64),
            ("chDeviceVersion", C.c_ubyte * 64),
            ("chManufacturerName", C.c_ubyte * 64),
            ("chSerialNumber", C.c_ubyte * 64),
            ("chUserDefinedName", C.c_ubyte * 64),
            ("nbcdUSB", C.c_uint),
            ("nDeviceAddress", C.c_uint),
            ("nReserved", C.c_uint * 2),
        ]

    class MV_SPECIAL_INFO(C.Union):
        _fields_ = [
            ("stGigEInfo", MV_GIGE_DEVICE_INFO),
            ("stUsb3VInfo", MV_USB3_DEVICE_INFO),
            ("reserved", C.c_ubyte * 540),
        ]

    class MV_CC_DEVICE_INFO(C.Structure):
        _fields_ = [
            ("nMajorVer", C.c_ushort),
            ("nMinorVer", C.c_ushort),
            ("nMacAddrHigh", C.c_uint),
            ("nMacAddrLow", C.c_uint),
            ("nTLayerType", C.c_uint),
            ("nReserved", C.c_uint * 4),
            ("SpecialInfo", MV_SPECIAL_INFO),
        ]

    class MV_CC_DEVICE_INFO_LIST(C.Structure):
        _fields_ = [
            ("nDeviceNum", C.c_uint),
            ("pDeviceInfo", C.POINTER(MV_CC_DEVICE_INFO) * MV_MAX_DEVICE_NUM),
        ]

    class MVCC_INTVALUE_EX(C.Structure):
        _fields_ = [
            ("nCurValue", C.c_longlong),
            ("nMax", C.c_longlong),
            ("nMin", C.c_longlong),
            ("nInc", C.c_longlong),
            ("nReserved", C.c_uint * 16),
        ]

    class MVCC_FLOATVALUE(C.Structure):
        _fields_ = [
            ("fCurValue", C.c_float),
            ("fMax", C.c_float),
            ("fMin", C.c_float),
            ("nReserved", C.c_uint * 4),
        ]

    class MV_FRAME_OUT_INFO_EX(C.Structure):
        _fields_ = [
            ("nWidth", C.c_ushort),
            ("nHeight", C.c_ushort),
            ("enPixelType", C.c_uint),
            ("nFrameNum", C.c_uint),
            ("nDevTimeStampHigh", C.c_uint),
            ("nDevTimeStampLow", C.c_uint),
            ("nReserved0", C.c_uint),
            ("nHostTimeStamp", C.c_longlong),
            ("nFrameLen", C.c_uint),
            ("nSecondCount", C.c_uint),
            ("nCycleCount", C.c_uint),
            ("nCycleOffset", C.c_uint),
            ("fGain", C.c_float),
            ("fExposureTime", C.c_float),
            ("nAverageBrightness", C.c_uint),
            ("nRed", C.c_uint),
            ("nGreen", C.c_uint),
            ("nBlue", C.c_uint),
            ("nFrameCounter", C.c_uint),
            ("nTriggerIndex", C.c_uint),
            ("nInput", C.c_uint),
            ("nOutput", C.c_uint),
            ("nOffsetX", C.c_ushort),
            ("nOffsetY", C.c_ushort),
            ("nChunkWidth", C.c_ushort),
            ("nChunkHeight", C.c_ushort),
            ("nLostPacket", C.c_uint),
            ("nUnparsedChunkNum", C.c_uint),
            ("UnparsedChunkList", C.c_void_p),
            ("nReserved", C.c_uint * 36),
        ]

    class MV_FRAME_OUT(C.Structure):
        _fields_ = [
            ("pBufAddr", C.POINTER(C.c_ubyte)),
            ("stFrameInfo", MV_FRAME_OUT_INFO_EX),
            ("nRes", C.c_uint * 16),
        ]

    class MV_CC_PIXEL_CONVERT_PARAM(C.Structure):
        _fields_ = [
            ("nWidth", C.c_ushort),
            ("nHeight", C.c_ushort),
            ("enSrcPixelType", C.c_uint),
            ("pSrcData", C.POINTER(C.c_ubyte)),
            ("nSrcDataLen", C.c_uint),
            ("pDstBuffer", C.POINTER(C.c_ubyte)),
            ("nDstBufferSize", C.c_uint),
            ("enDstPixelType", C.c_uint),
            ("nReserved", C.c_uint * 4),
        ]

    dll.MV_CC_EnumDevices.argtypes = [C.c_uint, C.POINTER(MV_CC_DEVICE_INFO_LIST)]
    dll.MV_CC_EnumDevices.restype = C.c_int
    dll.MV_CC_CreateHandle.argtypes = [C.POINTER(C.c_void_p), C.POINTER(MV_CC_DEVICE_INFO)]
    dll.MV_CC_CreateHandle.restype = C.c_int
    dll.MV_CC_DestroyHandle.argtypes = [C.c_void_p]
    dll.MV_CC_DestroyHandle.restype = C.c_int
    dll.MV_CC_OpenDevice.argtypes = [C.c_void_p, C.c_uint16, C.c_uint16]
    dll.MV_CC_OpenDevice.restype = C.c_int
    dll.MV_CC_CloseDevice.argtypes = [C.c_void_p]
    dll.MV_CC_CloseDevice.restype = C.c_int
    dll.MV_CC_StartGrabbing.argtypes = [C.c_void_p]
    dll.MV_CC_StartGrabbing.restype = C.c_int
    dll.MV_CC_StopGrabbing.argtypes = [C.c_void_p]
    dll.MV_CC_StopGrabbing.restype = C.c_int
    dll.MV_CC_GetOneFrameTimeout.argtypes = [
        C.c_void_p,
        C.POINTER(C.c_ubyte),
        C.c_uint,
        C.POINTER(MV_FRAME_OUT_INFO_EX),
        C.c_uint,
    ]
    dll.MV_CC_GetOneFrameTimeout.restype = C.c_int
    dll.MV_CC_GetIntValueEx.argtypes = [C.c_void_p, C.c_char_p, C.POINTER(MVCC_INTVALUE_EX)]
    dll.MV_CC_GetIntValueEx.restype = C.c_int
    dll.MV_CC_GetFloatValue.argtypes = [C.c_void_p, C.c_char_p, C.POINTER(MVCC_FLOATVALUE)]
    dll.MV_CC_GetFloatValue.restype = C.c_int
    dll.MV_CC_SetFloatValue.argtypes = [C.c_void_p, C.c_char_p, C.c_float]
    dll.MV_CC_SetFloatValue.restype = C.c_int
    dll.MV_CC_SetEnumValue.argtypes = [C.c_void_p, C.c_char_p, C.c_uint]
    dll.MV_CC_SetEnumValue.restype = C.c_int
    dll.MV_CC_SetBoolValue.argtypes = [C.c_void_p, C.c_char_p, C.c_bool]
    dll.MV_CC_SetBoolValue.restype = C.c_int
    dll.MV_CC_ConvertPixelType.argtypes = [
        C.c_void_p,
        C.POINTER(MV_CC_PIXEL_CONVERT_PARAM),
    ]
    dll.MV_CC_ConvertPixelType.restype = C.c_int
    if hasattr(dll, "MV_CC_GetOptimalPacketSize"):
        dll.MV_CC_GetOptimalPacketSize.argtypes = [C.c_void_p]
        dll.MV_CC_GetOptimalPacketSize.restype = C.c_int
    if hasattr(dll, "MV_CC_SetIntValueEx"):
        dll.MV_CC_SetIntValueEx.argtypes = [C.c_void_p, C.c_char_p, C.c_longlong]
        dll.MV_CC_SetIntValueEx.restype = C.c_int

    class MvCamera:
        @staticmethod
        def MV_CC_EnumDevices(mask: int, device_list: MV_CC_DEVICE_INFO_LIST) -> int:
            return dll.MV_CC_EnumDevices(mask, byref(device_list))

        def __init__(self) -> None:
            self.handle = C.c_void_p()
            self._last_frame_buffer: Any = None
            self._payload_size = 0

        def MV_CC_CreateHandle(self, device_info: MV_CC_DEVICE_INFO) -> int:
            return dll.MV_CC_CreateHandle(byref(self.handle), byref(device_info))

        def MV_CC_DestroyHandle(self) -> int:
            ret = dll.MV_CC_DestroyHandle(self.handle)
            self.handle = C.c_void_p()
            return ret

        def MV_CC_OpenDevice(self, access_mode: int, switchover_key: int) -> int:
            return dll.MV_CC_OpenDevice(self.handle, access_mode, switchover_key)

        def MV_CC_CloseDevice(self) -> int:
            return dll.MV_CC_CloseDevice(self.handle)

        def MV_CC_StartGrabbing(self) -> int:
            return dll.MV_CC_StartGrabbing(self.handle)

        def MV_CC_StopGrabbing(self) -> int:
            return dll.MV_CC_StopGrabbing(self.handle)

        def MV_CC_GetOptimalPacketSize(self) -> int:
            if hasattr(dll, "MV_CC_GetOptimalPacketSize"):
                return dll.MV_CC_GetOptimalPacketSize(self.handle)
            return 0

        def MV_CC_SetIntValue(self, name: str, value: int) -> int:
            if not hasattr(dll, "MV_CC_SetIntValueEx"):
                return 0
            return dll.MV_CC_SetIntValueEx(self.handle, name.encode("ascii"), int(value))

        def MV_CC_SetEnumValue(self, name: str, value: int) -> int:
            ret = dll.MV_CC_SetEnumValue(self.handle, name.encode("ascii"), int(value))
            if ret == 0 and name == "PixelFormat":
                self._payload_size = 0
            return ret

        def MV_CC_SetBoolValue(self, name: str, value: bool) -> int:
            return dll.MV_CC_SetBoolValue(self.handle, name.encode("ascii"), bool(value))

        def MV_CC_GetFloatValue(self, name: str, value: MVCC_FLOATVALUE) -> int:
            return dll.MV_CC_GetFloatValue(self.handle, name.encode("ascii"), byref(value))

        def MV_CC_SetFloatValue(self, name: str, value: float) -> int:
            return dll.MV_CC_SetFloatValue(self.handle, name.encode("ascii"), C.c_float(value))

        def _get_payload_size(self) -> int:
            if self._payload_size > 0:
                return self._payload_size
            value = MVCC_INTVALUE_EX()
            ret = dll.MV_CC_GetIntValueEx(self.handle, b"PayloadSize", byref(value))
            if ret != 0 or value.nCurValue <= 0:
                return 16 * 1024 * 1024
            self._payload_size = int(value.nCurValue)
            return self._payload_size

        def MV_CC_GetImageBuffer(self, out_frame: MV_FRAME_OUT, timeout_ms: int) -> int:
            payload_size = self._get_payload_size()
            buffer = (C.c_ubyte * payload_size)()
            frame_info = MV_FRAME_OUT_INFO_EX()
            ret = dll.MV_CC_GetOneFrameTimeout(
                self.handle,
                buffer,
                payload_size,
                byref(frame_info),
                int(timeout_ms),
            )
            if ret != 0:
                return ret
            self._last_frame_buffer = buffer
            out_frame.pBufAddr = cast(buffer, C.POINTER(C.c_ubyte))
            out_frame.stFrameInfo = frame_info
            return 0

        def MV_CC_FreeImageBuffer(self, out_frame: MV_FRAME_OUT) -> int:
            self._last_frame_buffer = None
            out_frame.pBufAddr = cast(0, C.POINTER(C.c_ubyte))
            return 0

        def MV_CC_ConvertPixelType(self, convert_param: MV_CC_PIXEL_CONVERT_PARAM) -> int:
            return dll.MV_CC_ConvertPixelType(self.handle, byref(convert_param))

    return SimpleNamespace(
        USING_DIRECT_DLL=True,
        MvCamera=MvCamera,
        MV_CC_DEVICE_INFO_LIST=MV_CC_DEVICE_INFO_LIST,
        MV_CC_DEVICE_INFO=MV_CC_DEVICE_INFO,
        MVCC_FLOATVALUE=MVCC_FLOATVALUE,
        MV_FRAME_OUT=MV_FRAME_OUT,
        MV_CC_PIXEL_CONVERT_PARAM=MV_CC_PIXEL_CONVERT_PARAM,
        MV_GIGE_DEVICE=0x00000001,
        MV_USB_DEVICE=0x00000004,
        MV_GENTL_GIGE_DEVICE=0,
        MV_GENTL_CAMERALINK_DEVICE=0,
        MV_GENTL_CXP_DEVICE=0,
        MV_GENTL_XOF_DEVICE=0,
        MV_ACCESS_Exclusive=1,
        MV_TRIGGER_MODE_OFF=0,
        PixelType_Gvsp_RGB8_Packed=PIXEL_TYPE_RGB8_PACKED,
    )


def import_mvs_sdk(extra_path: str | None) -> Any:
    global MVS

    for path in sdk_path_candidates(extra_path):
        if path.exists() and str(path) not in sys.path:
            sys.path.insert(0, str(path))

    try:
        MVS = importlib.import_module("MvCameraControl_class")
    except ImportError as exc:
        try:
            MVS = import_mvs_direct_dll()
        except Exception as dll_exc:
            searched = "\n  ".join(str(path) for path in sdk_path_candidates(extra_path))
            raise SystemExit(
                "Failed to import Hikrobot MVS Python SDK module MvCameraControl_class.py "
                "and failed to load MvCameraControl.dll directly.\n"
                "Pass --sdk-path to the MvImport directory or set MVSDK_PYTHON_PATH.\n"
                f"Searched:\n  {searched}\nDLL fallback error: {dll_exc}"
            ) from exc
    return MVS


def decode_c_string(value: Any) -> str:
    if isinstance(value, str):
        return value
    try:
        raw = bytes(value)
    except TypeError:
        return ""
    raw = raw.split(b"\x00", 1)[0]
    for encoding in ("utf-8", "gbk", "latin1"):
        try:
            return raw.decode(encoding).strip()
        except UnicodeDecodeError:
            continue
    return ""


def yuv_to_rgb(y: np.ndarray, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    y_f = y.astype(np.float32)
    u_f = u.astype(np.float32) - 128.0
    v_f = v.astype(np.float32) - 128.0
    r = y_f + 1.402 * v_f
    g = y_f - 0.344136 * u_f - 0.714136 * v_f
    b = y_f + 1.772 * u_f
    return np.clip(np.stack((r, g, b), axis=2), 0, 255).astype(np.uint8)


def yuv422_to_rgb(buffer: Any, width: int, height: int, pixel_type: int) -> np.ndarray:
    data = np.frombuffer(buffer, dtype=np.uint8, count=width * height * 2)
    packed = data.reshape((height, width // 2, 4))

    if pixel_type == PIXEL_TYPE_YUV422_YUYV_PACKED:
        y0 = packed[:, :, 0]
        u = packed[:, :, 1]
        y1 = packed[:, :, 2]
        v = packed[:, :, 3]
    else:
        u = packed[:, :, 0]
        y0 = packed[:, :, 1]
        v = packed[:, :, 2]
        y1 = packed[:, :, 3]

    y = np.empty((height, width), dtype=np.uint8)
    u_full = np.empty((height, width), dtype=np.uint8)
    v_full = np.empty((height, width), dtype=np.uint8)
    y[:, 0::2] = y0
    y[:, 1::2] = y1
    u_full[:, 0::2] = u
    u_full[:, 1::2] = u
    v_full[:, 0::2] = v
    v_full[:, 1::2] = v
    return yuv_to_rgb(y, u_full, v_full)


def bayer_to_rgb(buffer: Any, width: int, height: int, pixel_type: int) -> np.ndarray:
    bayer = np.frombuffer(buffer, dtype=np.uint8, count=width * height).reshape((height, width))
    conversion = BAYER_CONVERSIONS.get(pixel_type)
    if cv2 is not None and conversion is not None:
        return cv2.cvtColor(bayer, conversion)
    return np.repeat(bayer[:, :, None], 3, axis=2)


def rgb_to_mono_rgb(frame: np.ndarray) -> np.ndarray:
    if frame.ndim == 2:
        mono = frame
    else:
        mono = (
            0.299 * frame[:, :, 0].astype(np.float32)
            + 0.587 * frame[:, :, 1].astype(np.float32)
            + 0.114 * frame[:, :, 2].astype(np.float32)
        ).astype(np.uint8)
    return np.repeat(mono[:, :, None], 3, axis=2)


def resize_for_preview(frame: np.ndarray, max_width: int = 1280) -> np.ndarray:
    height, width = frame.shape[:2]
    if width <= max_width or cv2 is None:
        return frame
    preview_height = max(1, round(height * max_width / width))
    return cv2.resize(frame, (max_width, preview_height), interpolation=cv2.INTER_AREA)


def resize_for_video(frame: np.ndarray, target_size: tuple[int, int] | None) -> np.ndarray:
    if target_size is None:
        height, width = frame.shape[:2]
        even_width = width - (width % 2)
        even_height = height - (height % 2)
        if even_width == width and even_height == height:
            return frame
        return frame[:even_height, :even_width]
    if cv2 is None:
        return frame
    return cv2.resize(frame, target_size, interpolation=cv2.INTER_AREA)


@dataclass
class DeviceEntry:
    index: int
    info: Any
    name: str


def device_type_mask() -> int:
    names = (
        "MV_GIGE_DEVICE",
        "MV_USB_DEVICE",
        "MV_GENTL_GIGE_DEVICE",
        "MV_GENTL_CAMERALINK_DEVICE",
        "MV_GENTL_CXP_DEVICE",
        "MV_GENTL_XOF_DEVICE",
    )
    mask = 0
    for name in names:
        mask |= int(getattr(MVS, name, 0))
    return mask or 0xFFFFFFFF


def get_device_name(index: int, info: Any) -> str:
    try:
        if info.nTLayerType == getattr(MVS, "MV_GIGE_DEVICE", -1):
            gige = info.SpecialInfo.stGigEInfo
            model = decode_c_string(gige.chModelName)
            serial = decode_c_string(gige.chSerialNumber)
            return f"cam_{index + 1:02d} | GigE | {model or 'Hik camera'} | {serial}".strip()
        if info.nTLayerType == getattr(MVS, "MV_USB_DEVICE", -1):
            usb = info.SpecialInfo.stUsb3VInfo
            model = decode_c_string(usb.chModelName)
            serial = decode_c_string(usb.chSerialNumber)
            return f"cam_{index + 1:02d} | USB | {model or 'Hik camera'} | {serial}".strip()
    except Exception:
        pass
    return f"cam_{index + 1:02d} | Hik camera"


def enumerate_devices() -> list[DeviceEntry]:
    device_list = MVS.MV_CC_DEVICE_INFO_LIST()
    memset(byref(device_list), 0, sizeof(device_list))
    ret = MVS.MvCamera.MV_CC_EnumDevices(device_type_mask(), device_list)
    if ret != 0:
        raise RuntimeError(f"MV_CC_EnumDevices failed: {fmt_ret(ret)}")

    devices: list[DeviceEntry] = []
    for index in range(int(device_list.nDeviceNum)):
        info = cast(device_list.pDeviceInfo[index], POINTER(MVS.MV_CC_DEVICE_INFO)).contents
        devices.append(DeviceEntry(index=index, info=info, name=get_device_name(index, info)))
    return devices


class HikCamera:
    def __init__(self, camera_id: str, device: DeviceEntry) -> None:
        self.camera_id = camera_id
        self.device = device
        self._cam = MVS.MvCamera()
        self._lock = RLock()
        self._opened = False
        self._grabbing = False
        self.color_mode = COLOR_MODE_COLOR

    @property
    def title(self) -> str:
        return f"{self.camera_id} - {self.device.name}"

    def _check(self, ret: int, action: str) -> None:
        if ret != 0:
            raise RuntimeError(f"{self.camera_id}: {action} failed: {fmt_ret(ret)}")

    def open(self) -> None:
        with self._lock:
            if self._opened:
                return
            self._check(self._cam.MV_CC_CreateHandle(self.device.info), "CreateHandle")
            self._check(
                self._cam.MV_CC_OpenDevice(getattr(MVS, "MV_ACCESS_Exclusive", 1), 0),
                "OpenDevice",
            )
            self._opened = True

            if self.device.info.nTLayerType == getattr(MVS, "MV_GIGE_DEVICE", -1):
                packet_size = self._cam.MV_CC_GetOptimalPacketSize()
                if packet_size and packet_size > 0:
                    self._cam.MV_CC_SetIntValue("GevSCPSPacketSize", packet_size)

            self._cam.MV_CC_SetEnumValue("TriggerMode", getattr(MVS, "MV_TRIGGER_MODE_OFF", 0))
            self._cam.MV_CC_SetEnumValue("ExposureAuto", 0)
            self._cam.MV_CC_SetEnumValue("GainAuto", 0)
            self._cam.MV_CC_SetBoolValue("AcquisitionFrameRateEnable", True)
            self.set_color_mode(self.color_mode)

    def start_grabbing(self) -> None:
        with self._lock:
            if not self._opened:
                self.open()
            if self._grabbing:
                return
            self._check(self._cam.MV_CC_StartGrabbing(), "StartGrabbing")
            self._grabbing = True

    def stop_grabbing(self) -> None:
        with self._lock:
            if self._grabbing:
                self._cam.MV_CC_StopGrabbing()
                self._grabbing = False

    def close(self) -> None:
        with self._lock:
            self.stop_grabbing()
            if self._opened:
                self._cam.MV_CC_CloseDevice()
                self._cam.MV_CC_DestroyHandle()
                self._opened = False

    def get_float_info(self, name: str) -> dict[str, float] | None:
        with self._lock:
            value = MVS.MVCC_FLOATVALUE()
            memset(byref(value), 0, sizeof(value))
            ret = self._cam.MV_CC_GetFloatValue(name, value)
            if ret != 0:
                return None
            return {
                "min": float(value.fMin),
                "max": float(value.fMax),
                "current": float(value.fCurValue),
            }

    def set_float_value(self, name: str, value: float) -> None:
        with self._lock:
            ret = self._cam.MV_CC_SetFloatValue(name, float(value))
            self._check(ret, f"SetFloatValue({name})")

    def set_color_mode(self, mode: str) -> None:
        with self._lock:
            self.color_mode = mode
            if not self._opened:
                return

            was_grabbing = self._grabbing
            if was_grabbing:
                self._cam.MV_CC_StopGrabbing()
                self._grabbing = False

            try:
                if mode == COLOR_MODE_MONO:
                    self._check(
                        self._cam.MV_CC_SetEnumValue("PixelFormat", PIXEL_TYPE_MONO8),
                        "SetEnumValue(PixelFormat=Mono8)",
                    )
                    return

                last_ret = 0
                for pixel_format in COLOR_PIXEL_FORMATS:
                    last_ret = self._cam.MV_CC_SetEnumValue("PixelFormat", pixel_format)
                    if last_ret == 0:
                        return
                self._check(last_ret, "SetEnumValue(PixelFormat=color)")
            finally:
                if was_grabbing:
                    self._check(self._cam.MV_CC_StartGrabbing(), "StartGrabbing")
                    self._grabbing = True

    def _apply_color_mode(self, frame: np.ndarray) -> np.ndarray:
        if self.color_mode == COLOR_MODE_MONO:
            return rgb_to_mono_rgb(frame)
        return frame

    def read_frame_rgb(self, timeout_ms: int = 1000) -> np.ndarray | None:
        with self._lock:
            if not self._grabbing:
                return None

            out_frame = MVS.MV_FRAME_OUT()
            memset(byref(out_frame), 0, sizeof(out_frame))
            ret = self._cam.MV_CC_GetImageBuffer(out_frame, timeout_ms)
            if ret != 0:
                return None

            try:
                frame_info = out_frame.stFrameInfo
                width = int(frame_info.nWidth)
                height = int(frame_info.nHeight)
                src_len = int(frame_info.nFrameLen)
                src_buffer = (c_ubyte * src_len)()
                memmove(src_buffer, out_frame.pBufAddr, src_len)

                if int(frame_info.enPixelType) == PIXEL_TYPE_MONO8:
                    mono = np.frombuffer(src_buffer, dtype=np.uint8).reshape((height, width))
                    return self._apply_color_mode(np.repeat(mono[:, :, None], 3, axis=2)).copy()

                if int(frame_info.enPixelType) in BAYER_CONVERSIONS:
                    return self._apply_color_mode(
                        bayer_to_rgb(src_buffer, width, height, int(frame_info.enPixelType))
                    ).copy()

                if int(frame_info.enPixelType) == PIXEL_TYPE_RGB8_PACKED and src_len >= width * height * 3:
                    frame = np.frombuffer(src_buffer, dtype=np.uint8, count=width * height * 3)
                    return self._apply_color_mode(frame.reshape((height, width, 3))).copy()

                if int(frame_info.enPixelType) == PIXEL_TYPE_BGR8_PACKED and src_len >= width * height * 3:
                    frame = np.frombuffer(src_buffer, dtype=np.uint8, count=width * height * 3)
                    return self._apply_color_mode(frame.reshape((height, width, 3))[:, :, ::-1]).copy()

                if int(frame_info.enPixelType) in (
                    PIXEL_TYPE_YUV422_PACKED,
                    PIXEL_TYPE_YUV422_YUYV_PACKED,
                ):
                    return self._apply_color_mode(
                        yuv422_to_rgb(src_buffer, width, height, int(frame_info.enPixelType))
                    ).copy()

                dst_len = width * height * 3
                dst_buffer = (c_ubyte * dst_len)()
                convert_param = MVS.MV_CC_PIXEL_CONVERT_PARAM()
                memset(byref(convert_param), 0, sizeof(convert_param))
                convert_param.nWidth = width
                convert_param.nHeight = height
                convert_param.pSrcData = src_buffer
                convert_param.nSrcDataLen = src_len
                convert_param.enSrcPixelType = frame_info.enPixelType
                convert_param.enDstPixelType = getattr(MVS, "PixelType_Gvsp_RGB8_Packed")
                convert_param.pDstBuffer = dst_buffer
                convert_param.nDstBufferSize = dst_len

                ret = self._cam.MV_CC_ConvertPixelType(convert_param)
                if ret != 0:
                    return None
                frame = np.frombuffer(dst_buffer, dtype=np.uint8).reshape((height, width, 3))
                return self._apply_color_mode(frame).copy()
            finally:
                self._cam.MV_CC_FreeImageBuffer(out_frame)


class CameraStreamThread(QtCore.QThread):
    frame_ready = Signal(int, object)
    error = Signal(int, str)

    def __init__(
        self,
        index: int,
        camera: HikCamera,
        max_preview_width: int = 1280,
        max_fps: float = 12.0,
    ) -> None:
        super().__init__()
        self.index = index
        self.camera = camera
        self.max_preview_width = max_preview_width
        self.frame_interval = 1.0 / max(max_fps, 1.0)

    def run(self) -> None:  # pragma: no cover - requires live cameras
        while not self.isInterruptionRequested():
            started_at = time.perf_counter()
            try:
                frame = self.camera.read_frame_rgb(timeout_ms=500)
                if frame is not None:
                    preview = resize_for_preview(frame, self.max_preview_width)
                    self.frame_ready.emit(self.index, (frame, preview))
            except Exception as exc:
                self.error.emit(self.index, str(exc))
                self.msleep(500)
            elapsed = time.perf_counter() - started_at
            sleep_ms = max(1, round((self.frame_interval - elapsed) * 1000))
            self.msleep(sleep_ms)


class VideoLabel(QtWidgets.QLabel):
    def __init__(self, text: str) -> None:
        super().__init__(text)
        self._image: QtGui.QImage | None = None
        self._aspect_ratio = 4.0 / 3.0
        self.setAlignment(QtCore.Qt.AlignCenter)
        self.setMinimumSize(420, 315)
        self.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding)
        self.setStyleSheet("background: #15181c; color: #d6dde6; border: 1px solid #333942;")

    def hasHeightForWidth(self) -> bool:
        return True

    def heightForWidth(self, width: int) -> int:
        return max(1, round(width / self._aspect_ratio))

    def set_frame(self, frame: np.ndarray) -> None:
        frame = np.ascontiguousarray(frame)
        height, width, channels = frame.shape
        if height > 0:
            self._aspect_ratio = width / height
        bytes_per_line = channels * width
        self._image = QtGui.QImage(
            frame.data,
            width,
            height,
            bytes_per_line,
            QtGui.QImage.Format_RGB888,
        ).copy()
        self._refresh_pixmap()

    def resizeEvent(self, event: QtGui.QResizeEvent) -> None:
        super().resizeEvent(event)
        self._refresh_pixmap()

    def _refresh_pixmap(self) -> None:
        if self._image is None:
            return
        pixmap = QtGui.QPixmap.fromImage(self._image)
        self.setPixmap(
            pixmap.scaled(self.size(), QtCore.Qt.KeepAspectRatio, QtCore.Qt.SmoothTransformation)
        )


class ParamControl(QtWidgets.QWidget):
    value_changed = Signal(float)

    def __init__(self, label: str, unit: str, minimum: float, maximum: float, current: float) -> None:
        super().__init__()
        self.minimum = minimum
        self.maximum = maximum

        name_label = QtWidgets.QLabel(f"{label} ({unit})")
        self.value_box = QtWidgets.QDoubleSpinBox()
        self.value_box.setRange(minimum, maximum)
        self.value_box.setDecimals(3 if maximum - minimum < 100 else 1)
        self.value_box.setSingleStep(max((maximum - minimum) / 200.0, 0.001))
        self.value_box.setValue(current)

        self.slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        self.slider.setRange(0, 1000)
        self.slider.setValue(self._value_to_slider(current))

        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(0, 6, 0, 6)
        layout.addWidget(name_label)
        layout.addWidget(self.value_box)
        layout.addWidget(self.slider)

        self.slider.valueChanged.connect(self._on_slider_changed)
        self.value_box.valueChanged.connect(self._on_box_changed)

    def _value_to_slider(self, value: float) -> int:
        if self.maximum <= self.minimum:
            return 0
        ratio = (value - self.minimum) / (self.maximum - self.minimum)
        return int(max(0, min(1000, round(ratio * 1000))))

    def _slider_to_value(self, value: int) -> float:
        ratio = value / 1000.0
        return self.minimum + ratio * (self.maximum - self.minimum)

    def _on_slider_changed(self, value: int) -> None:
        real_value = self._slider_to_value(value)
        self.value_box.blockSignals(True)
        self.value_box.setValue(real_value)
        self.value_box.blockSignals(False)
        self.value_changed.emit(real_value)

    def _on_box_changed(self, value: float) -> None:
        self.slider.blockSignals(True)
        self.slider.setValue(self._value_to_slider(value))
        self.slider.blockSignals(False)
        self.value_changed.emit(value)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self, cameras: list[HikCamera], output_dir: Path) -> None:
        super().__init__()
        self.cameras = cameras
        self.output_dir = output_dir
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.last_frames: list[np.ndarray | None] = [None for _ in cameras]
        self.streams: list[CameraStreamThread] = []
        self.capture_started_at: list[float | None] = [None, None, None]
        self.capture_active = [False, False, False]
        self.capture_saved_counts = [0, 0, 0]
        self.capture_session_stamps: list[str | None] = [None, None, None]
        self.capture_targets = [[0], [1], [0, 1]]
        self.capture_buttons: list[QtWidgets.QPushButton] = []
        self.capture_timer_labels: list[QtWidgets.QLabel] = []
        self.recording_writers: dict[tuple[int, int], Any] = {}
        self.recording_paths: dict[tuple[int, int], Path] = {}
        self.video_quality_size: tuple[int, int] | None = None
        self.capture_timer = QtCore.QTimer(self)
        self.capture_timer.setInterval(250)
        self.capture_timer.timeout.connect(self.update_capture_timers)

        self.setWindowTitle("Hikvision dual camera capture")
        self.resize(1320, 760)
        self._build_ui()
        self._open_cameras()
        self._start_streams()
        self.capture_timer.start()

    def _build_ui(self) -> None:
        central = QtWidgets.QWidget()
        central.setObjectName("central")
        root = QtWidgets.QHBoxLayout(central)
        root.setContentsMargins(14, 14, 14, 14)
        root.setSpacing(14)
        central.setStyleSheet(
            """
            QWidget#central {
                background: #f4f6f8;
            }
            QGroupBox {
                background: #ffffff;
                border: 1px solid #d9dee7;
                border-radius: 8px;
                margin-top: 10px;
                padding: 10px;
                font-weight: 700;
                color: #111827;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 4px;
            }
            QLabel {
                color: #1f2937;
            }
            QPushButton#secondaryButton {
                background: #ffffff;
                border: 1px solid #cfd6e2;
                border-radius: 6px;
                padding: 7px 10px;
                color: #111827;
                font-weight: 600;
            }
            QPushButton#secondaryButton:hover {
                background: #f1f5f9;
            }
            QScrollArea {
                background: transparent;
            }
            """
        )

        preview_layout = QtWidgets.QGridLayout()
        preview_layout.setHorizontalSpacing(10)
        preview_layout.setVerticalSpacing(8)
        self.video_labels: list[VideoLabel] = []
        for index, camera in enumerate(self.cameras):
            title = QtWidgets.QLabel(camera.title)
            title.setStyleSheet("font-weight: 600;")
            label = VideoLabel("等待相机画面...")
            self.video_labels.append(label)
            preview_layout.addWidget(title, index * 2, 0)
            preview_layout.addWidget(label, index * 2 + 1, 0)
            preview_layout.setRowStretch(index * 2 + 1, 1)

        root.addLayout(preview_layout, 1)

        sidebar = QtWidgets.QWidget()
        sidebar.setObjectName("sidebar")
        sidebar.setMinimumWidth(360)
        sidebar.setMaximumWidth(430)
        sidebar_layout = QtWidgets.QVBoxLayout(sidebar)
        sidebar_layout.setContentsMargins(4, 0, 0, 0)
        sidebar_layout.setSpacing(10)

        self.output_label = QtWidgets.QLabel(str(self.output_dir))
        self.output_label.setWordWrap(True)
        self.output_label.setStyleSheet("color: #4b5563;")
        choose_button = QtWidgets.QPushButton("选择输出文件夹")
        choose_button.setObjectName("secondaryButton")
        choose_button.clicked.connect(self.choose_output_dir)
        sidebar_layout.addWidget(QtWidgets.QLabel("输出文件夹"))
        sidebar_layout.addWidget(self.output_label)
        sidebar_layout.addWidget(choose_button)

        capture_one = self.create_capture_button("1", "录制相机 1")
        capture_two = self.create_capture_button("2", "录制相机 2")
        capture_all = self.create_capture_button("全", "同时录制全部")
        self.capture_buttons = [capture_one, capture_two, capture_all]
        capture_one.clicked.connect(lambda: self.toggle_capture(0))
        capture_two.clicked.connect(lambda: self.toggle_capture(1))
        capture_all.clicked.connect(lambda: self.toggle_capture(2))
        capture_group = QtWidgets.QGroupBox("视频录制")
        capture_layout = QtWidgets.QVBoxLayout(capture_group)
        capture_layout.setSpacing(10)
        quality_row = QtWidgets.QHBoxLayout()
        quality_row.addWidget(QtWidgets.QLabel("视频画质"))
        self.video_quality_combo = QtWidgets.QComboBox()
        for label, size in VIDEO_QUALITY_PRESETS:
            self.video_quality_combo.addItem(label, size)
        self.video_quality_combo.currentIndexChanged.connect(self.set_video_quality)
        quality_row.addWidget(self.video_quality_combo, 1)
        capture_layout.addLayout(quality_row)
        for button, text in (
            (capture_one, "相机 1 未录制"),
            (capture_two, "相机 2 未录制"),
            (capture_all, "全部未录制"),
        ):
            timer_label = QtWidgets.QLabel(text)
            timer_label.setMinimumWidth(150)
            timer_label.setStyleSheet(
                "color: #374151; font-weight: 700; background: #f8fafc; "
                "border: 1px solid #e5e7eb; border-radius: 6px; padding: 7px 9px;"
            )
            self.capture_timer_labels.append(timer_label)
            row = QtWidgets.QHBoxLayout()
            row.setSpacing(12)
            row.addWidget(button)
            row.addWidget(timer_label, 1)
            capture_layout.addLayout(row)
        sidebar_layout.addSpacing(8)
        sidebar_layout.addWidget(capture_group)

        self.param_container = QtWidgets.QWidget()
        self.param_layout = QtWidgets.QVBoxLayout(self.param_container)
        self.param_layout.setContentsMargins(0, 0, 0, 0)
        self.param_layout.addStretch(1)

        self.param_scroll = QtWidgets.QScrollArea()
        self.param_scroll.setWidgetResizable(True)
        self.param_scroll.setFrameShape(QtWidgets.QFrame.NoFrame)
        self.param_scroll.setWidget(self.param_container)
        sidebar_layout.addSpacing(8)
        sidebar_layout.addWidget(self.param_scroll, 1)

        root.addWidget(sidebar)
        self.setCentralWidget(central)
        self.statusBar().showMessage("正在初始化相机...")

    def create_capture_button(self, text: str, tooltip: str) -> QtWidgets.QPushButton:
        button = QtWidgets.QPushButton(text)
        button.setToolTip(tooltip)
        button.setFixedSize(60, 60)
        button.setCursor(QtCore.Qt.PointingHandCursor)
        button.setProperty("recording", False)
        button.setStyleSheet(
            """
            QPushButton {
                background-color: #dc2626;
                border: 2px solid #991b1b;
                border-radius: 30px;
                color: white;
                font-size: 18px;
                font-weight: 700;
            }
            QPushButton:hover {
                background-color: #ef4444;
            }
            QPushButton:pressed {
                background-color: #991b1b;
                padding-top: 2px;
            }
            QPushButton[recording="true"] {
                background-color: #7f1d1d;
                border-color: #450a0a;
                font-size: 16px;
            }
            """
        )
        return button

    def set_capture_button_recording(self, timer_index: int, recording: bool) -> None:
        if timer_index >= len(self.capture_buttons):
            return
        button = self.capture_buttons[timer_index]
        button.setProperty("recording", recording)
        button.setText("停" if recording else ("全" if timer_index == 2 else str(timer_index + 1)))
        button.style().unpolish(button)
        button.style().polish(button)
        button.update()

    def _open_cameras(self) -> None:
        for camera in self.cameras:
            camera.open()
            camera.start_grabbing()
        self._build_parameter_controls()
        self.statusBar().showMessage("相机已连接")

    def _build_parameter_controls(self) -> None:
        for camera_index, camera in enumerate(self.cameras):
            group = QtWidgets.QGroupBox(f"相机 {camera_index + 1} 参数")
            group_layout = QtWidgets.QVBoxLayout(group)

            color_row = QtWidgets.QHBoxLayout()
            color_row.addWidget(QtWidgets.QLabel("画面色彩"))
            color_combo = QtWidgets.QComboBox()
            color_combo.addItem("彩色", COLOR_MODE_COLOR)
            color_combo.addItem("黑白", COLOR_MODE_MONO)
            color_combo.currentIndexChanged.connect(
                lambda _index, combo=color_combo, cam=camera: self.set_camera_color_mode(
                    cam,
                    combo.currentData(),
                )
            )
            color_row.addWidget(color_combo, 1)
            group_layout.addLayout(color_row)

            for sdk_name, label, unit in PARAMETERS:
                info = camera.get_float_info(sdk_name)
                if not info:
                    continue
                control = ParamControl(label, unit, info["min"], info["max"], info["current"])
                control.value_changed.connect(
                    lambda value, cam=camera, name=sdk_name: self.set_camera_param(cam, name, value)
                )
                group_layout.addWidget(control)
            self.param_layout.insertWidget(self.param_layout.count() - 1, group)

    def _start_streams(self) -> None:
        for index, camera in enumerate(self.cameras):
            stream = CameraStreamThread(index, camera)
            stream.frame_ready.connect(self.update_frame)
            stream.error.connect(self.show_stream_error)
            stream.start()
            self.streams.append(stream)

    def set_camera_param(self, camera: HikCamera, name: str, value: float) -> None:
        try:
            camera.set_float_value(name, value)
            self.statusBar().showMessage(f"{camera.camera_id} {name} = {value:.3f}", 2500)
        except Exception as exc:
            self.statusBar().showMessage(str(exc), 5000)

    def set_camera_color_mode(self, camera: HikCamera, mode: str) -> None:
        try:
            camera.set_color_mode(mode)
            label = "黑白" if mode == COLOR_MODE_MONO else "彩色"
            self.statusBar().showMessage(f"{camera.camera_id} 画面色彩 = {label}", 2500)
        except Exception as exc:
            self.statusBar().showMessage(str(exc), 5000)

    def update_frame(self, index: int, payload: object) -> None:
        if isinstance(payload, tuple):
            frame, preview = payload
        else:
            frame = payload
            preview = payload
        self.last_frames[index] = frame
        self.video_labels[index].set_frame(preview)
        self.write_video_frame(index, frame)

    def show_stream_error(self, index: int, message: str) -> None:
        self.statusBar().showMessage(f"相机 {index + 1}: {message}", 5000)

    def choose_output_dir(self) -> None:
        selected = QtWidgets.QFileDialog.getExistingDirectory(
            self,
            "选择输出文件夹",
            str(self.output_dir),
        )
        if selected:
            self.output_dir = Path(selected)
            self.output_dir.mkdir(parents=True, exist_ok=True)
            self.output_label.setText(str(self.output_dir))

    def set_video_quality(self) -> None:
        if any(self.capture_active):
            self.statusBar().showMessage("录制中不能切换视频画质，停止后再调整", 3000)
            return
        self.video_quality_size = self.video_quality_combo.currentData()
        self.statusBar().showMessage(f"视频画质 = {self.video_quality_combo.currentText()}", 2500)

    def update_recording_controls(self) -> None:
        self.video_quality_combo.setEnabled(not any(self.capture_active))

    def toggle_capture(self, timer_index: int) -> None:
        if timer_index >= len(self.capture_active):
            return
        if self.capture_active[timer_index]:
            self.stop_capture(timer_index)
        else:
            self.start_capture(timer_index)

    def start_capture(self, timer_index: int) -> None:
        if cv2 is None:
            self.statusBar().showMessage("录制视频需要 OpenCV: pip install opencv-python", 5000)
            return
        self.capture_active[timer_index] = True
        self.capture_started_at[timer_index] = time.perf_counter()
        self.capture_session_stamps[timer_index] = time.strftime("%Y%m%d_%H%M%S")
        self.capture_saved_counts[timer_index] = 0
        self.set_capture_button_recording(timer_index, True)
        self.update_recording_controls()
        self.set_capture_label(timer_index, "录制中 0.0s | 等待画面")
        for camera_index in self.capture_targets[timer_index]:
            frame = self.last_frames[camera_index]
            if frame is not None:
                self.write_video_frame(camera_index, frame)
        self.statusBar().showMessage(self.capture_status_prefix(timer_index) + "开始录制视频", 3000)

    def stop_capture(self, timer_index: int) -> None:
        elapsed = self.capture_elapsed(timer_index)
        count = self.capture_saved_counts[timer_index]
        self.capture_active[timer_index] = False
        self.capture_started_at[timer_index] = None
        self.release_recording_writers(timer_index)
        self.set_capture_button_recording(timer_index, False)
        self.update_recording_controls()
        self.set_capture_label(timer_index, f"已停止 {elapsed:.1f}s | {count} 帧")
        self.statusBar().showMessage(self.capture_status_prefix(timer_index) + "停止录制视频", 3000)

    def capture_elapsed(self, timer_index: int) -> float:
        started_at = self.capture_started_at[timer_index]
        if started_at is None:
            return 0.0
        return max(0.0, time.perf_counter() - started_at)

    def set_capture_label(self, timer_index: int, text: str) -> None:
        if timer_index < len(self.capture_timer_labels):
            self.capture_timer_labels[timer_index].setText(text)

    def update_capture_timers(self) -> None:
        for index, active in enumerate(self.capture_active):
            if not active:
                continue
            elapsed = self.capture_elapsed(index)
            frame_count = self.capture_saved_counts[index]
            if frame_count:
                self.set_capture_label(index, f"录制中 {elapsed:.1f}s | {frame_count} 帧")
            else:
                self.set_capture_label(index, f"录制中 {elapsed:.1f}s | 等待画面")

    def capture_status_prefix(self, timer_index: int) -> str:
        if timer_index == 2:
            return "全部相机 "
        return f"相机 {timer_index + 1} "

    def active_recording_sessions_for_camera(self, camera_index: int) -> list[int]:
        sessions: list[int] = []
        if camera_index < len(self.capture_active) and self.capture_active[camera_index]:
            sessions.append(camera_index)
        if self.capture_active[2] and camera_index in self.capture_targets[2]:
            sessions.append(2)
        return sessions

    def video_quality_suffix(self) -> str:
        text = self.video_quality_combo.currentText()
        return text.split(" ", 1)[0].replace("原始", "original").replace("(", "").replace(")", "")

    def ensure_video_writer(self, timer_index: int, camera_index: int, frame: np.ndarray) -> Any | None:
        key = (timer_index, camera_index)
        writer = self.recording_writers.get(key)
        if writer is not None:
            return writer

        video_frame = resize_for_video(frame, self.video_quality_size)
        height, width = video_frame.shape[:2]
        camera_dir = self.output_dir / self.cameras[camera_index].camera_id
        camera_dir.mkdir(parents=True, exist_ok=True)
        stamp = self.capture_session_stamps[timer_index] or time.strftime("%Y%m%d_%H%M%S")
        mode = "all" if timer_index == 2 else self.cameras[camera_index].camera_id
        video_path = camera_dir / (
            f"{self.cameras[camera_index].camera_id}_{stamp}_{mode}_{self.video_quality_suffix()}.mp4"
        )
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        writer = cv2.VideoWriter(str(video_path), fourcc, VIDEO_FPS, (width, height))
        if not writer.isOpened():
            self.statusBar().showMessage(f"视频写入器打开失败: {video_path}", 5000)
            writer.release()
            return None
        self.recording_writers[key] = writer
        self.recording_paths[key] = video_path
        return writer

    def write_video_frame(self, camera_index: int, frame: np.ndarray) -> None:
        sessions = self.active_recording_sessions_for_camera(camera_index)
        if not sessions or cv2 is None:
            return
        for timer_index in sessions:
            writer = self.ensure_video_writer(timer_index, camera_index, frame)
            if writer is None:
                continue
            video_frame = resize_for_video(frame, self.video_quality_size)
            bgr_frame = cv2.cvtColor(np.ascontiguousarray(video_frame), cv2.COLOR_RGB2BGR)
            writer.write(bgr_frame)
            self.capture_saved_counts[timer_index] += 1

    def release_recording_writers(self, timer_index: int) -> None:
        for key in list(self.recording_writers):
            if key[0] != timer_index:
                continue
            writer = self.recording_writers.pop(key)
            writer.release()
            self.recording_paths.pop(key, None)

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        self.capture_timer.stop()
        for index, active in enumerate(self.capture_active):
            if active:
                self.capture_active[index] = False
                self.release_recording_writers(index)
                self.set_capture_button_recording(index, False)
        for stream in self.streams:
            stream.requestInterruption()
        for stream in self.streams:
            stream.wait(1500)
        for camera in self.cameras:
            camera.close()
        super().closeEvent(event)


def save_rgb_frame(frame: np.ndarray, image_path: Path) -> None:
    frame = np.ascontiguousarray(frame)
    image = QtGui.QImage(
        frame.data,
        frame.shape[1],
        frame.shape[0],
        frame.shape[1] * 3,
        QtGui.QImage.Format_RGB888,
    ).copy()
    if not image.save(str(image_path), "JPG", 95):
        raise RuntimeError(f"failed to save image: {image_path}")


def run_smoke_test(cameras: list[HikCamera], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"detected cameras: {len(cameras)}")
    saved: list[Path] = []
    try:
        for camera in cameras:
            print(f"opening {camera.title}")
            camera.open()
            camera.start_grabbing()

        stamp = time.strftime("%Y%m%d_%H%M%S") + f"_{time.time_ns() % 1_000_000_000:09d}"
        for index, camera in enumerate(cameras):
            frame = None
            for _ in range(10):
                frame = camera.read_frame_rgb(timeout_ms=1000)
                if frame is not None:
                    break
            if frame is None:
                raise RuntimeError(f"{camera.camera_id}: failed to grab frame")
            camera_dir = output_dir / camera.camera_id
            camera_dir.mkdir(parents=True, exist_ok=True)
            image_path = camera_dir / f"{camera.camera_id}_{stamp}_smoke.jpg"
            save_rgb_frame(frame, image_path)
            saved.append(image_path)
            print(f"{camera.camera_id}: {frame.shape[1]}x{frame.shape[0]} saved to {image_path}")
    finally:
        for camera in cameras:
            camera.close()

    if len(saved) != len(cameras):
        raise RuntimeError(f"saved {len(saved)} images, expected {len(cameras)}")
    print("smoke test passed")


def parse_args() -> argparse.Namespace:
    training_dir = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Preview and capture from two Hikrobot MVS cameras.")
    parser.add_argument(
        "--output",
        type=Path,
        default=training_dir / "raw_data" / "hik_dual_capture",
        help="Default output folder for captured JPG files.",
    )
    parser.add_argument(
        "--sdk-path",
        default=None,
        help="Path to Hikrobot MVS MvImport directory containing MvCameraControl_class.py.",
    )
    parser.add_argument(
        "--smoke-test",
        action="store_true",
        help="Open two cameras, grab one frame from each, save JPGs, then exit without showing UI.",
    )
    parser.add_argument(
        "--auto-close-ms",
        type=int,
        default=0,
        help="Testing helper: close the Qt window automatically after this many milliseconds.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    import_mvs_sdk(args.sdk_path)
    devices = enumerate_devices()
    if len(devices) < 2:
        raise SystemExit(f"Need at least 2 Hikrobot cameras, found {len(devices)}.")

    cameras = [HikCamera(f"cam_{index + 1:02d}", device) for index, device in enumerate(devices[:2])]
    if args.smoke_test:
        run_smoke_test(cameras, args.output.resolve())
        return

    app = QtWidgets.QApplication(sys.argv)
    window = MainWindow(cameras, args.output.resolve())
    window.show()
    if args.auto_close_ms > 0:
        QtCore.QTimer.singleShot(args.auto_close_ms, window.close)
    exec_app = app.exec if hasattr(app, "exec") else app.exec_
    sys.exit(exec_app())


if __name__ == "__main__":
    main()
