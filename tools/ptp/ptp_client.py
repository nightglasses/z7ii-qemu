#!/usr/bin/env python3
"""
Nikon PTP / USB Direct Client for Physical Hardware Exploration
Used to collect verified physical-camera evidence for nightglass-qemu.
"""

import ctypes
import struct
import sys
import time

LIBUSB_PATH = "/opt/homebrew/lib/libusb-1.0.dylib"

class DeviceDescriptor(ctypes.Structure):
    _fields_ = [
        ("bLength", ctypes.c_uint8),
        ("bDescriptorType", ctypes.c_uint8),
        ("bcdUSB", ctypes.c_uint16),
        ("bDeviceClass", ctypes.c_uint8),
        ("bDeviceSubClass", ctypes.c_uint8),
        ("bDeviceProtocol", ctypes.c_uint8),
        ("bMaxPacketSize0", ctypes.c_uint8),
        ("idVendor", ctypes.c_uint16),
        ("idProduct", ctypes.c_uint16),
        ("bcdDevice", ctypes.c_uint16),
        ("iManufacturer", ctypes.c_uint8),
        ("iProduct", ctypes.c_uint8),
        ("iSerialNumber", ctypes.c_uint8),
        ("bNumConfigurations", ctypes.c_uint8),
    ]

class PTPClient:
    VID_NIKON = 0x04B0
    PID_Z7_2 = 0x044B

    # Container types
    TYPE_COMMAND = 1
    TYPE_DATA = 2
    TYPE_RESPONSE = 3
    TYPE_EVENT = 4

    # Endpoints for Z 7II
    EP_OUT = 0x02
    EP_IN = 0x81
    EP_INT = 0x83

    def __init__(self, lib_path=LIBUSB_PATH):
        self.lib = ctypes.CDLL(lib_path)
        self._setup_ctypes()
        self.ctx = None
        self.handle = None
        self.tx_id = 1
        self.session_open = False

    def _setup_ctypes(self):
        self.lib.libusb_init.argtypes = [ctypes.c_void_p]
        self.lib.libusb_init.restype = ctypes.c_int

        self.lib.libusb_exit.argtypes = [ctypes.c_void_p]
        self.lib.libusb_exit.restype = None

        self.lib.libusb_get_device_list.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))
        ]
        self.lib.libusb_get_device_list.restype = ctypes.c_ssize_t

        self.lib.libusb_free_device_list.argtypes = [
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.c_int
        ]
        self.lib.libusb_free_device_list.restype = None

        self.lib.libusb_get_device_descriptor.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(DeviceDescriptor)
        ]
        self.lib.libusb_get_device_descriptor.restype = ctypes.c_int

        self.lib.libusb_open.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p)
        ]
        self.lib.libusb_open.restype = ctypes.c_int

        self.lib.libusb_close.argtypes = [ctypes.c_void_p]
        self.lib.libusb_close.restype = None

        self.lib.libusb_claim_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.libusb_claim_interface.restype = ctypes.c_int

        self.lib.libusb_release_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.lib.libusb_release_interface.restype = ctypes.c_int

        self.lib.libusb_bulk_transfer.argtypes = [
            ctypes.c_void_p, ctypes.c_uint8, ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_uint
        ]
        self.lib.libusb_bulk_transfer.restype = ctypes.c_int

    def open(self):
        res = self.lib.libusb_init(None)
        if res != 0:
            raise RuntimeError(f"libusb_init failed with error {res}")

        devs = ctypes.POINTER(ctypes.c_void_p)()
        cnt = self.lib.libusb_get_device_list(None, ctypes.byref(devs))
        if cnt < 0:
            raise RuntimeError(f"libusb_get_device_list failed: {cnt}")

        target_dev = None
        for i in range(cnt):
            dev = devs[i]
            desc = DeviceDescriptor()
            if self.lib.libusb_get_device_descriptor(dev, ctypes.byref(desc)) == 0:
                if desc.idVendor == self.VID_NIKON and desc.idProduct == self.PID_Z7_2:
                    target_dev = dev
                    break

        if not target_dev:
            self.lib.libusb_free_device_list(devs, 1)
            raise RuntimeError("Nikon Z 7_2 camera not found on USB bus")

        h = ctypes.c_void_p()
        res = self.lib.libusb_open(target_dev, ctypes.byref(h))
        self.lib.libusb_free_device_list(devs, 1)
        if res != 0 or not h:
            raise RuntimeError(f"libusb_open failed with error {res}")

        self.handle = h

        # Claim interface 0
        res = self.lib.libusb_claim_interface(self.handle, 0)
        if res != 0:
            self.lib.libusb_close(self.handle)
            self.handle = None
            raise RuntimeError(f"libusb_claim_interface(0) failed with error {res}")

        return True

    def close(self):
        if self.handle:
            if self.session_open:
                try:
                    self.close_session()
                except Exception:
                    pass
            self.lib.libusb_release_interface(self.handle, 0)
            self.lib.libusb_close(self.handle)
            self.handle = None
        self.lib.libusb_exit(None)

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def next_tx_id(self):
        t = self.tx_id
        self.tx_id += 1
        return t

    def send_cmd(self, opcode, params=[]):
        tx = self.next_tx_id()
        length = 12 + 4 * len(params)
        pkt = struct.pack("<IHHI", length, self.TYPE_COMMAND, opcode, tx)
        for p in params:
            pkt += struct.pack("<I", p)

        buf = (ctypes.c_uint8 * len(pkt)).from_buffer_copy(pkt)
        transferred = ctypes.c_int()
        ret = self.lib.libusb_bulk_transfer(
            self.handle, self.EP_OUT, buf, len(pkt), ctypes.byref(transferred), 3000
        )
        if ret != 0:
            raise RuntimeError(f"send_cmd bulk transfer OUT failed: {ret}")
        return tx

    def read_packet(self, timeout=3000):
        raw_buf = (ctypes.c_uint8 * 131072)()
        transferred = ctypes.c_int()
        ret = self.lib.libusb_bulk_transfer(
            self.handle, self.EP_IN, raw_buf, 131072, ctypes.byref(transferred), timeout
        )
        if ret != 0:
            return None
        if transferred.value < 12:
            return None

        data = bytes(raw_buf[:transferred.value])
        length, ptype, code, tx_id = struct.unpack("<IHHI", data[:12])
        payload = data[12:]
        expected = length - 12
        while len(payload) < expected:
            ret = self.lib.libusb_bulk_transfer(
                self.handle, self.EP_IN, raw_buf, 131072, ctypes.byref(transferred), timeout
            )
            if ret != 0 or transferred.value == 0:
                break
            payload += bytes(raw_buf[:transferred.value])

        params = []
        if ptype == self.TYPE_RESPONSE and len(payload) >= 4:
            params = [
                struct.unpack("<I", payload[i:i+4])[0]
                for i in range(0, len(payload), 4)
            ]

        return {
            "length": length,
            "type": ptype,
            "code": code,
            "tx_id": tx_id,
            "payload": payload,
            "params": params,
        }

    def execute_transaction(self, opcode, params=[], timeout=3000):
        tx = self.send_cmd(opcode, params)
        pkt1 = self.read_packet(timeout=timeout)
        if not pkt1:
            return None, None
        if pkt1["type"] == self.TYPE_DATA:
            pkt2 = self.read_packet(timeout=timeout)
            return pkt1, pkt2
        return None, pkt1

    def open_session(self, session_id=1):
        data, resp = self.execute_transaction(0x1002, [session_id])
        if resp and (resp["code"] == 0x2001 or resp["code"] == 0x201E):
            self.session_open = True
            return resp["code"]
        raise RuntimeError(f"OpenSession failed: {resp['code'] if resp else 'No response'}")

    def close_session(self):
        if not self.session_open:
            return
        data, resp = self.execute_transaction(0x1003)
        self.session_open = False
        return resp["code"] if resp else None

    def get_device_info(self):
        data, resp = self.execute_transaction(0x1001)
        if not data:
            return None
        return data["payload"]

    def get_storage_ids(self):
        data, resp = self.execute_transaction(0x1004)
        if not data or len(data["payload"]) < 4:
            return []
        cnt = struct.unpack("<I", data["payload"][:4])[0]
        ids = [
            struct.unpack("<I", data["payload"][4+4*i:8+4*i])[0]
            for i in range(cnt)
        ]
        return ids

    def get_device_prop_desc(self, prop_code):
        data, resp = self.execute_transaction(0x1014, [prop_code])
        return data["payload"] if data else None

    def get_device_prop_value(self, prop_code):
        data, resp = self.execute_transaction(0x1015, [prop_code])
        return data["payload"] if data else None

    def start_live_view(self):
        data, resp = self.execute_transaction(0x9201, timeout=5000)
        return resp and resp["code"] == 0x2001

    def end_live_view(self):
        data, resp = self.execute_transaction(0x9202, timeout=5000)
        return resp and resp["code"] == 0x2001

    def get_live_view_frame(self):
        data, resp = self.execute_transaction(0x9203, timeout=5000)
        if data:
            return data["payload"]
        return None

    def check_device_ready(self):
        data, resp = self.execute_transaction(0x90C8, timeout=2000)
        return resp and resp["code"] == 0x2001
