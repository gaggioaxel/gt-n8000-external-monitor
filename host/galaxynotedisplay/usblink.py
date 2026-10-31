"""Collegamento USB con il tablet: dispositivo accessorio Android, due endpoint bulk."""
import struct
import time

import usb.core
import usb.util

MAGIC = 0xA5
T_NOOP, T_CONFIG, T_FRAME, T_KEY, T_INFO, T_SETTINGS = 0, 1, 2, 3, 4, 0x13
GOOGLE = 0x18D1
ACCESSORY = 0x2D00  # il tablet si presenta già così: nessuno scambio AOA


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def open_accessory():
    """Attende il tablet e restituisce il dispositivo con i suoi endpoint."""
    waited = False
    while True:
        dev = usb.core.find(idVendor=GOOGLE, idProduct=ACCESSORY)
        if dev is not None:
            break
        if not waited:
            log("In attesa del tablet via USB…")
            waited = True
        time.sleep(1)

    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except (usb.core.USBError, NotImplementedError):
        pass
    intf = dev.get_active_configuration()[(0, 0)]
    usb.util.claim_interface(dev, intf)
    ep_out = usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
    ep_in = usb.util.find_descriptor(intf, custom_match=lambda e:
        usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
    log("Tablet collegato")
    return dev, ep_out, ep_in


class Link:
    def __init__(self, dev, ep_out, ep_in):
        self.dev, self.out, self.inp = dev, ep_out, ep_in
        self.mps = ep_out.wMaxPacketSize or 512
        self.alive = True

    def send(self, ptype, payload):
        pkt = struct.pack(">BBI", MAGIC, ptype, len(payload)) + payload
        # Se la lunghezza è un multiplo del pacchetto USB il tablet aspetterebbe
        # altri dati: aggiungo un noop così il trasferimento finisce "corto".
        if len(pkt) % self.mps == 0:
            pkt += struct.pack(">BBI", MAGIC, T_NOOP, 0)
        self.out.write(pkt, timeout=3000)

    def close(self):
        self.alive = False
        try:
            usb.util.dispose_resources(self.dev)
        except Exception:
            pass


def input_loop(link, on_settings):
    """Legge i pacchetti del tablet; torna quando il cavo si stacca."""
    buf = bytearray()
    while link.alive:
        try:
            buf += bytes(link.inp.read(16384, timeout=500))
        except usb.core.USBTimeoutError:
            continue
        except usb.core.USBError:
            break
        while len(buf) >= 6:
            if buf[0] != MAGIC:
                del buf[0]
                continue
            ptype, ln = buf[1], struct.unpack(">I", buf[2:6])[0]
            if ln > 4096:
                del buf[0]
                continue
            if len(buf) < 6 + ln:
                break
            payload = bytes(buf[6:6 + ln])
            del buf[:6 + ln]
            if ptype == T_SETTINGS and ln == 4:
                on_settings(*struct.unpack(">BBH", payload))
