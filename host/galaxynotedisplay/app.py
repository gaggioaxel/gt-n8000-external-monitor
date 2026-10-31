"""
GalaxyNoteDisplay - host Ubuntu per sessioni Wayland (GNOME).

Chiede a Mutter un monitor virtuale (API org.gnome.Mutter.ScreenCast, la stessa
del desktop remoto di GNOME), ne riceve i frame via PipeWire, li codifica in
H.264 a bassa latenza con GStreamer e li invia al tablet via USB. Niente Xorg,
niente sudo, niente schermo fittizio. Solo video: il tablet non controlla il desktop. Orientamento, fps e keepalive
li sceglie il pannello del tablet, che li manda qui.

  GalaxyNoteDisplay
  GalaxyNoteDisplay --stats --encoder va
  GalaxyNoteDisplay --test          # senza tablet: solo cattura e codifica
"""
import argparse
import collections
import os
import struct
import threading
import time

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gio, GLib, Gst

from .usblink import T_CONFIG, T_FRAME, T_INFO, T_KEY, Link, input_loop, log, open_accessory

SC_BUS = "org.gnome.Mutter.ScreenCast"
SC_PATH = "/org/gnome/Mutter/ScreenCast"
CURSOR_EMBEDDED = 1


# ---------------------------------------------------------------- monitor virtuale

class VirtualMonitor:
    """Sessione ScreenCast di Mutter con un monitor virtuale; chiama on_node(id) quando il nodo PipeWire è pronto."""

    def __init__(self, on_node):
        self.stopping = False
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        sc = self._proxy(SC_PATH, "org.gnome.Mutter.ScreenCast")
        session_path = sc.call_sync("CreateSession", GLib.Variant("(a{sv})", ({},)),
                                    Gio.DBusCallFlags.NONE, -1).unpack()[0]
        self.session = self._proxy(session_path, "org.gnome.Mutter.ScreenCast.Session")
        props = {"cursor-mode": GLib.Variant("u", CURSOR_EMBEDDED),
                 "is-platform": GLib.Variant("b", True)}
        stream_path = self.session.call_sync("RecordVirtual", GLib.Variant("(a{sv})", (props,)),
                                             Gio.DBusCallFlags.NONE, -1).unpack()[0]
        self.stream = self._proxy(stream_path, "org.gnome.Mutter.ScreenCast.Stream")
        self.stream.connect("g-signal", lambda p, sender, sig, params:
                            sig == "PipeWireStreamAdded" and on_node(params.unpack()[0]))
        self.session.connect("g-signal", lambda p, sender, sig, params:
                             sig == "Closed" and GLib.idle_add(self._closed))
        self.session.call_sync("Start", None, Gio.DBusCallFlags.NONE, -1)

    def _proxy(self, path, iface):
        return Gio.DBusProxy.new_sync(self.bus, Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES,
                                      None, SC_BUS, path, iface, None)

    def _closed(self):
        if self.stopping:
            return
        log("Mutter ha chiuso il monitor virtuale")
        os._exit(1)

    def stop(self):
        self.stopping = True
        try:
            self.session.call_sync("Stop", None, Gio.DBusCallFlags.NONE, 1000)
        except GLib.Error:
            pass


def main_logical_size():
    """Dimensione logica (pixel diviso scala) del monitor principale di GNOME."""
    bus = Gio.bus_get_sync(Gio.BusType.SESSION)
    _, monitors, logical, _ = bus.call_sync(
        "org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
        "org.gnome.Mutter.DisplayConfig", "GetCurrentState", None, None,
        Gio.DBusCallFlags.NONE, -1, None).unpack()
    _, _, scale, _, primary, specs, _ = next(lm for lm in logical if lm[4])
    for spec, modes, _ in monitors:
        if spec == specs[0]:
            mode = next(m for m in modes if m[6].get("is-current"))
            # Pari, per la sottocampionatura 4:2:0 dell'encoder
            return (round(mode[1] / scale) // 2 * 2, round(mode[2] / scale) // 2 * 2)
    raise RuntimeError("monitor principale non trovato")


# ---------------------------------------------------------------- pipeline

def source_desc(a):
    """Caps chiesti a Mutter e conversione fino ai frame grezzi da dare all'encoder."""
    vw, vh = a.virtual
    w, h = a.size
    # Con il limite uguale ai propri 60 Hz Mutter salta dei refresh e si ferma a circa 40 fps:
    # gli chiedo il doppio e il limite vero lo applica on_captured
    rate = f"max-framerate={a.max_fps}/1"
    if a.encoder == "va":
        return (f"video/x-raw,width={vw},height={vh},{rate} ! "
                f"vapostproc ! video/x-raw(memory:VAMemory),format=NV12,width={w},height={h}")
    if a.virtual != a.size and Gst.ElementFactory.find("vaapipostproc"):
        # Frame grandi: Mutter li passa come DMA-BUF e la GPU Intel li rimpicciolisce,
        # copiarli in RAM a 3072x1920 dimezzerebbe gli fps
        return (f"video/x-raw(memory:DMABuf),width={vw},height={vh},{rate} ! "
                f"vaapipostproc width={w} height={h} scale-method=hq ! "
                f"video/x-raw,format=NV12,width={w},height={h}")
    return (f"video/x-raw,width={vw},height={vh},{rate} ! "
            f"videoconvertscale n-threads=4 ! video/x-raw,format=I420,width={w},height={h}")


def encoder_desc(a):
    gop = a.fps  # un keyframe al secondo
    if a.encoder == "x264":
        return (f"x264enc tune=zerolatency speed-preset=ultrafast bframes=0 key-int-max={gop} "
                f"pass=quant quantizer={a.crf} rc-lookahead=0 sync-lookahead=0 byte-stream=true")
    if a.encoder == "va":
        return (f"vah264lpenc rate-control=cqp qpi={a.crf} qpp={a.crf} b-frames=0 "
                f"key-int-max={gop} ref-frames=1 target-usage=7 ! video/x-h264,profile=main")
    raise ValueError(a.encoder)


def pipeline_desc(a, node):
    # I caps chiesti a pipewiresrc decidono la risoluzione e la frequenza del monitor virtuale.
    # Mutter manda frame solo quando lo schermo cambia e on_captured può scartare l'ultimo di una
    # raffica: keepalive-time ripete l'ultimo frame (in H.264 costa poche decine di byte) così
    # l'immagine finale arriva comunque, e un tablet appena collegato riceve subito un keyframe.
    return (f"pipewiresrc name=src path={node} do-timestamp=true always-copy=false "
            f"keepalive-time={a.keepalive} ! "
            f"{source_desc(a)} ! {encoder_desc(a)} ! "
            "h264parse config-interval=-1 ! video/x-h264,stream-format=byte-stream,alignment=au ! "
            "appsink name=sink emit-signals=true sync=false max-buffers=2 drop=false")


def split_nals(au):
    """Divide un'access unit Annex-B in NAL (con il loro start code a 4 byte)."""
    out, i, n = [], 0, len(au)
    starts = []
    while True:
        j = au.find(b"\0\0\1", i)
        if j < 0:
            break
        starts.append(j)
        i = j + 3
    for k, s in enumerate(starts):
        e = starts[k + 1] if k + 1 < len(starts) else n
        if k + 1 < len(starts) and au[e - 1] == 0:
            e -= 1
        out.append(au[s + 3:e])
    return out


# ---------------------------------------------------------------- invio

class Sender:
    """Riceve le AU dall'appsink e le manda al tablet collegato, se c'è."""

    def __init__(self, a):
        self.a = a
        self.link = None
        self.lock = threading.Lock()
        self.sps = self.pps = None
        self.need_config = True
        self.frames, self.nbytes, self.t0 = 0, 0, time.time()
        self.latency_ms = 0.0
        # Istanti in cui i frame escono da pipewiresrc; l'encoder (senza B-frame) ne restituisce uno per frame
        self.captured = collections.deque(maxlen=32)
        self.pipeline = None
        self.next_in = 0.0
        self.on_settings = None

    def on_captured(self, pad, info):
        # Limite di fps prima dell'encoder: in media non più di a.fps, scelto dal pannello del
        # tablet, con un quarto di frame di tolleranza sul jitter. Senza limite Mutter arriva a
        # max_fps, il decoder non regge e i frame in coda diventano ritardo visibile
        now = time.monotonic()
        period = 1 / self.a.fps
        if now < self.next_in - period / 4:
            return Gst.PadProbeReturn.DROP
        self.next_in = max(self.next_in, now - period / 2) + period
        self.captured.append(now)
        return Gst.PadProbeReturn.OK

    def attach(self, link):
        with self.lock:
            self.link = link
            self.need_config = True
        self.request_keyframe()

    def detach(self):
        with self.lock:
            self.link = None

    def request_keyframe(self):
        if self.pipeline:
            ev = Gst.Event.new_custom(Gst.EventType.CUSTOM_UPSTREAM, Gst.Structure.new_from_string(
                "GstForceKeyUnit, all-headers=(boolean)true"))
            self.pipeline.get_by_name("sink").send_event(ev)

    def on_sample(self, sink):
        sample = sink.emit("pull-sample")
        buf = sample.get_buffer()
        ok, info = buf.map(Gst.MapFlags.READ)
        if not ok:
            return Gst.FlowReturn.OK
        au = bytes(info.data)
        buf.unmap(info)
        key = not buf.has_flags(Gst.BufferFlags.DELTA_UNIT)

        # Tempo da quando pipewiresrc ha consegnato il frame all'AU codificata
        if self.captured:
            t_in = self.captured.popleft()
            self.latency_ms = 0.9 * self.latency_ms + 0.1 * (time.monotonic() - t_in) * 1e3

        nals = split_nals(au)
        rest = []
        for nal in nals:
            t = nal[0] & 0x1F if nal else 0
            if t == 7:
                if nal != self.sps:
                    self.sps, self.need_config = nal, True
            elif t == 8:
                if nal != self.pps:
                    self.pps, self.need_config = nal, True
            elif t != 9:  # scarto gli access unit delimiter
                rest.append(nal)
        payload = b"".join(b"\0\0\0\1" + n for n in rest)

        with self.lock:
            link = self.link
            if link is not None and self.sps and self.pps:
                try:
                    if self.need_config:
                        if not key:
                            self.request_keyframe()
                            return Gst.FlowReturn.OK
                        sps_b, pps_b = b"\0\0\0\1" + self.sps, b"\0\0\0\1" + self.pps
                        w, h = self.a.size
                        link.send(T_CONFIG, struct.pack(">HHH", w, h, len(sps_b)) + sps_b + pps_b)
                        self.need_config = False
                        log(f"Stream {w}x{h} @ {self.a.fps} fps, encoder {self.a.encoder}")
                    link.send(T_KEY if key else T_FRAME, payload)
                except Exception as e:
                    log("Errore USB:", e)
                    link.alive = False
                    self.link = None

        self.frames += 1
        self.nbytes += len(payload)
        dt = time.time() - self.t0
        if dt >= 2:
            fps = self.frames / dt
            if self.a.stats:
                log(f"{fps:5.1f} fps  {self.nbytes * 8 / dt / 1e6:6.1f} Mbit/s  "
                    f"cattura→codifica {self.latency_ms:4.1f} ms")
            self.frames, self.nbytes, self.t0 = 0, 0, time.time()
            self.send_info(fps)
        return Gst.FlowReturn.OK

    def send_info(self, fps):
        """Encoder, fps inviati e tempo di codifica per il pannello del tablet."""
        with self.lock:
            if self.link is not None:
                try:
                    self.link.send(T_INFO, f"{self.a.encoder} {fps:.1f} {self.latency_ms:.1f}".encode())
                except Exception as e:
                    log("Errore USB:", e)


def usb_loop(sender, a):
    while True:
        dev, ep_out, ep_in = open_accessory()
        link = Link(dev, ep_out, ep_in)
        sender.attach(link)
        input_loop(link, sender.on_settings)  # torna quando il cavo si stacca
        sender.detach()
        link.close()
        log("Tablet scollegato")
        if a.once:
            os._exit(0)  # il monitor virtuale sparisce con il processo
        time.sleep(1)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(prog="GalaxyNoteDisplay", description="Usa il Galaxy Note 10.1 come monitor virtuale via USB (Wayland/GNOME)")
    ap.add_argument("--size", default="1280x800", help="risoluzione dello stream verso il tablet (default 1280x800)")
    ap.add_argument("--match-main", action="store_true",
                    help="monitor virtuale grande quanto quello principale in pixel logici, così il "
                         "cursore passa da uno all'altro senza salti; lo stream viene rimpicciolito a --size")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--keepalive", type=int, default=50, metavar="MS",
                    help="ripete l'ultimo frame dopo MS millisecondi senza cambiamenti (default 50)")
    ap.add_argument("--crf", type=int, default=22, help="quantizzatore: più basso = migliore (default 22)")
    ap.add_argument("--encoder", choices=["x264", "va"], default="x264")
    ap.add_argument("--stats", action="store_true", help="mostra fps, bitrate e latenza di codifica")
    ap.add_argument("--test", action="store_true", help="non usa il tablet: solo cattura e codifica")
    ap.add_argument("--once", action="store_true",
                    help="esce quando il tablet si scollega (per il servizio avviato da udev)")
    a = ap.parse_args()
    a.size = tuple(int(v) for v in a.size.lower().split("x"))
    a.virtual = main_logical_size() if a.match_main else a.size
    a.max_fps = a.fps * 2  # resta fisso: a.fps cambia dal pannello del tablet
    if a.match_main and a.encoder == "va":
        log("Con --match-main uso x264: vah264lpenc non riceve i frame DMA-BUF e a quella risoluzione rallenta")
        a.encoder = "x264"
    if a.test:
        a.stats = True

    Gst.init(None)
    loop = GLib.MainLoop()
    sender = Sender(a)
    vm = None
    usb_started = False

    def apply_settings(portrait, fps, keepalive):
        """Impostazioni scelte sul tablet, nel main loop. Il collegamento USB resta aperto."""
        nonlocal vm
        log(f"Dal tablet: {'verticale' if portrait else 'orizzontale'}, {fps} fps, keepalive {keepalive} ms")
        a.fps = fps
        a.keepalive = keepalive
        if sender.pipeline:
            sender.pipeline.get_by_name("src").set_property("keepalive-time", keepalive)
        if portrait != (a.size[1] > a.size[0]):
            pipeline, sender.pipeline = sender.pipeline, None
            if pipeline:
                pipeline.get_bus().remove_signal_watch()
                pipeline.set_state(Gst.State.NULL)
            vm.stop()
            a.size, a.virtual = a.size[::-1], a.virtual[::-1]
            with sender.lock:
                sender.need_config = True
            vm = VirtualMonitor(on_node)
        return False

    sender.on_settings = lambda *s: GLib.idle_add(apply_settings, *s)

    def on_node(node):
        nonlocal usb_started
        log(f"Monitor virtuale pronto (nodo PipeWire {node})")
        desc = pipeline_desc(a, node)
        sender.pipeline = Gst.parse_launch(desc)
        sender.pipeline.get_by_name("sink").connect("new-sample", sender.on_sample)
        sender.pipeline.get_by_name("src").get_static_pad("src").add_probe(
            Gst.PadProbeType.BUFFER, sender.on_captured)
        bus = sender.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message::error", lambda b, m: (log("Errore GStreamer:", m.parse_error()[0].message),
                                                    loop.quit()))
        sender.pipeline.set_state(Gst.State.PLAYING)
        if not a.test and not usb_started:
            usb_started = True
            threading.Thread(target=usb_loop, args=(sender, a), daemon=True).start()

    vm = VirtualMonitor(on_node)
    log(f"Monitor virtuale {a.virtual[0]}x{a.virtual[1]} richiesto a GNOME, stream {a.size[0]}x{a.size[1]}; "
        "posizionalo in Impostazioni → Schermi")
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
    finally:
        if sender.pipeline:
            sender.pipeline.set_state(Gst.State.NULL)
        vm.stop()
        os._exit(0)  # i thread USB e GStreamer non devono girare durante lo spegnimento dell'interprete

