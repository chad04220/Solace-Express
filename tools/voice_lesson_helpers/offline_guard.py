"""Fail-closed speech worker: no telemetry and no network syscalls.

Only generation and validation workers import this. Model downloads and saved
deliverables use separate processes before/after speech generation.
"""
import ctypes
import errno
import os
import socket

# ONNX Runtime Privacy.md documents that this disables all non-Windows
# telemetry before initialization, including its uploader and device ID.
os.environ['ORT_DISABLE_TELEMETRY'] = '1'

def block_network():
    sec = ctypes.CDLL('libseccomp.so.2', use_errno=True)
    sec.seccomp_init.argtypes = [ctypes.c_uint32]
    sec.seccomp_init.restype = ctypes.c_void_p
    sec.seccomp_syscall_resolve_name.argtypes = [ctypes.c_char_p]
    sec.seccomp_syscall_resolve_name.restype = ctypes.c_int
    sec.seccomp_rule_add.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_uint]
    sec.seccomp_rule_add.restype = ctypes.c_int
    sec.seccomp_attr_set.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint32]
    sec.seccomp_attr_set.restype = ctypes.c_int
    sec.seccomp_load.argtypes = [ctypes.c_void_p]
    sec.seccomp_load.restype = ctypes.c_int
    sec.seccomp_release.argtypes = [ctypes.c_void_p]
    ctx = sec.seccomp_init(0x7fff0000)  # default allow; this only removes network capability
    if not ctx:
        raise RuntimeError('Cannot initialize offline worker restriction')
    try:
        deny = 0x00050000 | errno.EPERM
        for name in ['socket', 'socketpair', 'connect', 'sendto', 'sendmsg', 'sendmmsg']:
            number = sec.seccomp_syscall_resolve_name(name.encode())
            if number < 0 or sec.seccomp_rule_add(ctx, deny, number, 0) != 0:
                raise RuntimeError('Cannot block network syscall '+name)
        # Synchronize this added restriction across any existing worker threads.
        if sec.seccomp_attr_set(ctx, 4, 1) != 0:
            raise RuntimeError('Cannot synchronize offline restriction')
        if sec.seccomp_load(ctx) != 0:
            raise RuntimeError('Cannot load offline restriction')
    finally:
        sec.seccomp_release(ctx)
    for family in [socket.AF_INET, socket.AF_INET6, socket.AF_UNIX]:
        try:
            s = socket.socket(family, socket.SOCK_STREAM)
        except PermissionError:
            continue
        else:
            s.close()
            raise RuntimeError('Offline worker network self-check failed')

block_network()
