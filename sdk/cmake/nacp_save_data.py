#!/usr/bin/env python3
"""Sets the account save data fields of a NACP made by nacptool, which has no options for them.

An NRO is not installed and never gets save data from these fields, but forwarders built from the
NRO (Sphaira and similar tools copy its NACP) do: with them the installed title asks for a user at
launch and the system knows the size of its save data.

usage: nacp_save_data.py <file.nacp> <save size bytes> <journal size bytes>
"""
import struct
import sys

STARTUP_USER_ACCOUNT = 0x3025      # u8: 1 = a user must be selected at launch
USER_SAVE_DATA_SIZE = 0x3080       # s64
USER_SAVE_DATA_JOURNAL = 0x3088    # s64

path, size, journal = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
data = bytearray(open(path, 'rb').read())
if len(data) != 0x4000:
    sys.exit(f'{path}: not a NACP (0x{len(data):X} bytes)')
data[STARTUP_USER_ACCOUNT] = 1
struct.pack_into('<qq', data, USER_SAVE_DATA_SIZE, size, journal)
open(path, 'wb').write(data)
