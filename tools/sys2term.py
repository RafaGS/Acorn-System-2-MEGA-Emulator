#!/usr/bin/env python3
"""Terminal serie para el Acorn System 2 (Arduino Mega) con cassette virtual.

Sustituye a screen/picocom: el puerto serie solo puede abrirlo un programa a la
vez, y este hace de terminal (teclado y pantalla) y ademas de grabadora de
cassette: cuando el COS usa la cinta (LOAD, SAVE, CAT, FLOAD...) el Mega hace
una "sesion de cinta" por el mismo puerto (protocolo en include/SerialTape.h)
y este programa la atiende contra un fichero .tap.

  sys2term.py PORT [--baud 115200] [--tape cassette.tap] [--no-reset]
  sys2term.py info cassette.tap
  sys2term.py mktap programa.bin --name PROG --load 2000 [--exec 2000] [--tape cassette.tap] [--replace]
  sys2term.py extract cassette.tap PROG [-o programa.bin]

mktap mete un binario cualquiera en el cassette con el mismo formato que graba el
SAVE del COS (despues, en el System 2: LOAD "PROG", o LOAD "PROG" 3000 para
cargarlo en otra direccion; direcciones en hexadecimal, como en el COS). extract
hace lo contrario: saca un fichero del cassette a un binario.

Teclas propias (el resto pasa tal cual al System 2):
  Ctrl-T r   rebobinar el cassette      Ctrl-T i   mostrar el contenido
  Ctrl-T q   salir                      Ctrl-T Ctrl-T   enviar un Ctrl-T

Fichero .tap: 8 bytes "S2TAPE01" y, por cada bloque, longitud (2 bytes, little
endian) + bytes. Un bloque es lo que el COS graba entre dos tonos de guia.
Abrir el puerto reinicia el Mega (DTR); --no-reset lo evita.
"""
import argparse
import os
import re
import struct
import sys
import threading

MAGIC = b"S2TAPE01"
CHUNK = 16

PREAMBLE = b"\x1b[0m\x1b[?25h\x1b[29;1H\x1b[J"
MARKS = {b"#TAPE IN\r\n": "IN", b"#TAPE OUT\r\n": "OUT"}
ANSI_RE = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]")


# ---------------------------------------------------------------------------
# Fichero .tap
# ---------------------------------------------------------------------------
def read_tape(path):
    if not os.path.exists(path):
        return []
    data = open(path, "rb").read()
    if data[:8] != MAGIC:
        raise SystemExit("%s no es un cassette de System 2 (falta la cabecera S2TAPE01)" % path)
    blocks, i = [], 8
    while i < len(data):
        if i + 2 > len(data):
            raise SystemExit("%s esta truncado" % path)
        (n,) = struct.unpack_from("<H", data, i)
        i += 2
        if i + n > len(data):
            raise SystemExit("%s esta truncado" % path)
        blocks.append(bytes(data[i:i + n]))
        i += n
    return blocks


def write_tape(path, blocks):
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(MAGIC)
        for b in blocks:
            f.write(struct.pack("<H", len(b)))
            f.write(b)
    os.replace(tmp, path)


MAX_NAME = 16     # el SAVE del COS graba nombres de hasta 16 caracteres (comprobado en el emulador)
BLOCK_SIZE = 256


def parse_block(b):
    """Campos de un bloque grabado por el SAVE del COS (None si no tiene ese formato):
    '****' + nombre + CR + 8 bytes de cabecera + datos + suma de comprobacion.

    Cabecera (de cos.ic7, $FC1B y $FA13-$FA80): [0] banderas: bit 7 = siguen mas bloques,
    bit 6 = el bloque lleva datos, bit 5 = no es el primero del fichero (los 5 bits bajos
    son basura que el COS no mira); [1][2] numero de bloque (alto, bajo); [3] longitud-1
    de los datos; [4][5] direccion de ejecucion (alto, bajo); [6][7] direccion de carga
    del bloque (alto, bajo). La suma incluye todos los bytes desde los '****'."""
    if b[:4] != b"****" or b"\r" not in b[4:]:
        return None
    j = b.index(b"\r", 4)
    h = b[j + 1:j + 9]
    if len(h) < 8:
        return None
    flags, num_hi, num_lo, last, exec_hi, exec_lo, load_hi, load_lo = h
    n = last + 1 if flags & 0x40 else 0
    data = b[j + 9:j + 9 + n]
    ok = len(data) == n and len(b) == j + 9 + n + 1 and (sum(b[:-1]) & 0xFF) == b[-1]
    return {
        "name": b[4:j].decode("ascii", "replace"),
        "flags": flags,
        "more": bool(flags & 0x80),
        "first": not (flags & 0x20),
        "number": (num_hi << 8) | num_lo,
        "exec": (exec_hi << 8) | exec_lo,
        "load": (load_hi << 8) | load_lo,
        "data": data,
        "ok": ok,
    }


def describe_block(index, b):
    p = parse_block(b)
    if p:
        return "%3d  %-16s bloque %d  carga $%04X  ejec $%04X  datos %3d bytes  suma %s%s" % (
            index, p["name"], p["number"], p["load"], p["exec"], len(p["data"]), "ok" if p["ok"] else "MAL",
            "" if p["more"] else "  (ultimo)")
    return "%3d  (bloque sin cabecera del COS, %d bytes)" % (index, len(b))


def check_name(name):
    if not name or len(name) > MAX_NAME:
        raise SystemExit("el nombre debe tener entre 1 y %d caracteres" % MAX_NAME)
    if any(not (0x20 <= ord(c) < 0x7F) or c == '"' for c in name):
        raise SystemExit('el nombre solo puede llevar caracteres ASCII imprimibles y no comillas')
    return name


def make_blocks(name, data, load, exec_addr=None):
    """Bloques que grabaria el SAVE del COS para `data` cargado desde `load` (hasta 256 bytes por bloque)."""
    check_name(name)
    if not data:
        raise SystemExit("el fichero esta vacio")
    exec_addr = load if exec_addr is None else exec_addr
    if not (0 <= load <= 0xFFFF and 0 <= exec_addr <= 0xFFFF) or load + len(data) > 0x10000:
        raise SystemExit("direcciones fuera de rango: %d bytes desde $%04X no caben en 64 KB" % (len(data), load))
    count = (len(data) + BLOCK_SIZE - 1) // BLOCK_SIZE
    blocks = []
    for i in range(count):
        chunk = data[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE]
        addr = load + i * BLOCK_SIZE
        flags = 0x40 | (0x80 if i < count - 1 else 0) | (0x20 if i else 0)
        body = (b"****" + name.encode("ascii") + b"\r" +
                bytes([flags, i >> 8, i & 0xFF, len(chunk) - 1, exec_addr >> 8, exec_addr & 0xFF,
                       addr >> 8, addr & 0xFF]) + chunk)
        blocks.append(body + bytes([sum(body) & 0xFF]))
    return blocks


def find_file(blocks, name):
    """(indices de los bloques, datos, direccion de carga, direccion de ejecucion) del fichero `name`."""
    for start, b in enumerate(blocks):
        p = parse_block(b)
        if p and p["name"] == name and p["first"]:
            break
    else:
        raise SystemExit("no hay ningun fichero llamado %r en el cassette" % name)
    idx, data = [], bytearray()
    load, exec_addr = p["load"], p["exec"]
    i = start
    while True:
        p = parse_block(blocks[i])
        if not p or p["name"] != name or not p["ok"] or p["number"] != len(idx):
            raise SystemExit("el fichero %r esta danado en el bloque %d del cassette" % (name, i))
        idx.append(i)
        data.extend(p["data"])
        if not p["more"]:
            return idx, bytes(data), load, exec_addr
        i += 1
        if i >= len(blocks):
            raise SystemExit("el fichero %r esta incompleto (faltan bloques)" % name)


def cmd_info(path):
    blocks = read_tape(path)
    print("%s: %d bloques" % (path, len(blocks)))
    for i, b in enumerate(blocks):
        print(describe_block(i, b))


def parse_hex(text, what):
    try:
        value = int(text, 16)
    except ValueError:
        raise SystemExit("%s: %r no es una direccion en hexadecimal" % (what, text))
    if not 0 <= value <= 0xFFFF:
        raise SystemExit("%s: $%X fuera de rango" % (what, value))
    return value


def cmd_mktap(argv):
    ap = argparse.ArgumentParser(prog="sys2term.py mktap", description="Anade un binario al cassette con el formato del COS")
    ap.add_argument("file", help="fichero binario")
    ap.add_argument("--name", help="nombre en el cassette (por defecto, el del fichero en mayusculas)")
    ap.add_argument("--load", required=True, help="direccion de carga, en hexadecimal (ej. 2000)")
    ap.add_argument("--exec", dest="exec_addr", help="direccion de ejecucion, en hexadecimal (por defecto, la de carga)")
    ap.add_argument("--tape", default="cassette.tap")
    ap.add_argument("--replace", action="store_true", help="sustituir el fichero si ya hay uno con ese nombre")
    a = ap.parse_args(argv)
    name = a.name or os.path.splitext(os.path.basename(a.file))[0].upper()[:MAX_NAME]
    data = open(a.file, "rb").read()
    new = make_blocks(name, data, parse_hex(a.load, "--load"),
                      parse_hex(a.exec_addr, "--exec") if a.exec_addr else None)
    blocks = read_tape(a.tape)
    try:
        idx = find_file(blocks, name)[0]
    except SystemExit:
        idx = []
    if idx:
        if not a.replace:
            raise SystemExit("%s ya tiene un fichero %r (usa --replace para sustituirlo)" % (a.tape, name))
        blocks = [b for i, b in enumerate(blocks) if i not in idx]
    write_tape(a.tape, blocks + new)
    print("%s: %r, %d bytes desde $%04X en %d bloques; el cassette tiene ahora %d bloques" % (
        a.tape, name, len(data), parse_hex(a.load, "--load"), len(new), len(blocks) + len(new)))


def cmd_extract(argv):
    ap = argparse.ArgumentParser(prog="sys2term.py extract", description="Saca un fichero del cassette a un binario")
    ap.add_argument("tape")
    ap.add_argument("name")
    ap.add_argument("-o", "--output", help="fichero de salida (por defecto NOMBRE.bin)")
    a = ap.parse_args(argv)
    _, data, load, exec_addr = find_file(read_tape(a.tape), a.name)
    out = a.output or (a.name + ".bin")
    open(out, "wb").write(data)
    print("%s: %d bytes (carga $%04X, ejecucion $%04X)" % (out, len(data), load, exec_addr))


# ---------------------------------------------------------------------------
# Cassette
# ---------------------------------------------------------------------------
def d_line(chunk):
    return b"D" + chunk.hex().upper().encode() + b"%02X" % (sum(chunk) & 0xFF) + b"\n"


class Deck:
    """Maquina de estados del cassette; independiente del puerto (recibe lineas, devuelve bytes)."""

    def __init__(self, path):
        self.path = path
        self.blocks = read_tape(path)
        self.pos = 0            # siguiente bloque que se entregara en un #SYNC
        self.mode = None        # None, "IN" o "OUT"
        self.cur = None         # bloque que se esta leyendo
        self.off = 0
        self.last_reply = b""
        self.resend = False
        self.rec = None         # bloque que se esta grabando
        self.rec_blocks = []
        self.bad_lines = 0
        self.notices = []       # avisos para el usuario; Link los entrega cuando el Mega termina de dibujar

    # -- sesion --------------------------------------------------------------
    def start(self, mode):
        self.mode = mode
        self.cur, self.off, self.resend, self.last_reply = None, 0, False, b""
        self.rec, self.rec_blocks, self.bad_lines = None, [], 0
        self.say("cassette: %s" % ("leyendo" if mode == "IN" else "grabando"))

    def say(self, msg):
        self.notices.append(msg)

    def end(self):
        if self.mode == "OUT":
            self._close_rec()
            if self.rec_blocks:
                self.blocks.extend(self.rec_blocks)
                write_tape(self.path, self.blocks)
                self.pos = len(self.blocks)
            self.say("cassette: grabados %d bloques en %s%s" % (
                len(self.rec_blocks), self.path, " (%d lineas con error)" % self.bad_lines if self.bad_lines else ""))
        elif self.mode == "IN":
            self.say("cassette: bloque %d/%d" % (self.pos, len(self.blocks)))
        self.mode = None

    def rewind(self):
        self.pos, self.cur, self.off = 0, None, 0
        self.say("cassette: rebobinado (%d bloques)" % len(self.blocks))

    def _close_rec(self):
        if self.rec:
            self.rec_blocks.append(bytes(self.rec))
        self.rec = None

    # -- lineas del Mega -------------------------------------------------------
    def on_line(self, line):
        """Procesa una linea del Mega durante una sesion. Devuelve bytes a enviar (o b"")."""
        if self.mode == "IN":
            if line == "#ERR":
                self.resend = True
                return b""
            if line in ("#SYNC", "#NEED"):
                if self.resend and self.last_reply:
                    self.resend = False
                    return self.last_reply
                reply = self._sync() if line == "#SYNC" else self._need()
                if reply:
                    self.last_reply = reply
                return reply
            if line == "#EOF":
                self.say("cassette: el COS quiere mas datos y la cinta se acabo (Ctrl-\\ para reiniciar)")
        elif self.mode == "OUT":
            if line == "L":
                self._close_rec()
                self.rec = bytearray()
            elif line.startswith("D"):
                self._record(line[1:])
        return b""

    def _sync(self):
        if self.pos >= len(self.blocks):
            self.cur = None
            if self.blocks:
                self.say("cassette: fin de cinta (Ctrl-T r para rebobinar)")
            else:
                self.say("cassette vacio: no hay nada que leer")
            return b"E\n"
        self.cur = self.blocks[self.pos]
        self.pos += 1
        self.off = min(CHUNK, len(self.cur))
        return b"L\n" + d_line(self.cur[:self.off])

    def _need(self):
        if self.cur is None or self.off >= len(self.cur):
            return b""   # el bloque se acabo; el COS se queda esperando, como con una cinta real sin datos
        chunk = self.cur[self.off:self.off + CHUNK]
        self.off += len(chunk)
        return d_line(chunk)

    def _record(self, hexpart):
        try:
            raw = bytes.fromhex(hexpart)
        except ValueError:
            self.bad_lines += 1
            return
        if len(raw) < 1 or (sum(raw[:-1]) & 0xFF) != raw[-1]:
            self.bad_lines += 1
            return
        if self.rec is None:
            self.rec = bytearray()
        self.rec.extend(raw[:-1])

    def status(self):
        return "cassette %s: %d bloques, posicion %d" % (os.path.basename(self.path), len(self.blocks), self.pos)


# ---------------------------------------------------------------------------
# Decodificador del flujo del Mega: separa pantalla (se muestra) de sesiones de cinta (se atienden)
# ---------------------------------------------------------------------------
class Link:
    def __init__(self, deck, write_mega, write_screen, notify=None):
        self.deck = deck
        self.write_mega = write_mega
        self.write_screen = write_screen
        self.notify = notify or (lambda msg: None)
        self.buf = b""
        self.in_session = False
        self.candidates = [PREAMBLE + m for m in MARKS] + list(MARKS)

    def idle(self):
        """Sin datos nuevos: lo que se retuvo por si era el principio de una marca se muestra, y se
        entregan los avisos pendientes (despues del redibujado que hace el Mega al acabar la sesion,
        que borra la pantalla entera)."""
        if not self.in_session and self.buf:
            self.write_screen(self.buf)
            self.buf = b""
        while self.deck.notices:
            self.notify(self.deck.notices.pop(0))

    def feed(self, data):
        self.buf += data
        while self.buf:
            if self.in_session:
                m = re.search(rb"[\r\n]", self.buf)
                if not m:
                    return
                line = ANSI_RE.sub(b"", self.buf[:m.start()]).decode("ascii", "replace").strip()
                self.buf = self.buf[m.end():]
                if not line:
                    continue
                if line == "#END":
                    self.in_session = False
                    self.deck.end()
                    continue
                reply = self.deck.on_line(line)
                if reply:
                    self.write_mega(reply)
            else:
                best = None
                for mark, mode in MARKS.items():
                    i = self.buf.find(mark)
                    if i >= 0 and (best is None or i < best[0]):
                        best = (i, mark, mode)
                if best:
                    i, mark, mode = best
                    before = self.buf[:i]
                    if before.endswith(PREAMBLE):
                        before = before[:-len(PREAMBLE)]
                    if before:
                        self.write_screen(before)
                    self.buf = self.buf[i + len(mark):]
                    self.in_session = True
                    self.deck.start(mode)
                    continue
                # nada que atender: se muestra todo salvo un posible principio de marca
                keep = 0
                for cand in self.candidates:
                    for n in range(min(len(cand) - 1, len(self.buf)), 0, -1):
                        if self.buf.endswith(cand[:n]):
                            keep = max(keep, n)
                            break
                out, self.buf = self.buf[:len(self.buf) - keep], self.buf[len(self.buf) - keep:]
                if out:
                    self.write_screen(out)
                return


# ---------------------------------------------------------------------------
# Terminal
# ---------------------------------------------------------------------------
def run_terminal(args):
    try:
        import serial
    except ImportError:
        raise SystemExit("Falta pyserial: pip install pyserial")

    deck = Deck(args.tape)
    lock = threading.Lock()
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = args.port, args.baud, 0.05
    if args.no_reset:
        ser.dtr = False
    ser.open()

    def write_mega(data):
        with lock:
            ser.write(data)
            ser.flush()

    def write_screen(data):
        os.write(1, data)

    def log(msg):
        text = msg.encode("ascii", "replace")
        # titulo de la ventana + linea 28 (debajo de la pantalla y de la linea de estado del System 2)
        os.write(1, b"\x1b]0;System 2 | " + text + b"\x07")
        os.write(1, b"\x1b7\x1b[28;1H\x1b[2K\x1b[7m " + text[:77] + b" \x1b[0m\x1b8")

    link = Link(deck, write_mega, write_screen, notify=log)
    log(deck.status())
    stop = threading.Event()

    def reader():
        while not stop.is_set():
            try:
                data = ser.read(ser.in_waiting or 1)
            except Exception as e:  # puerto desconectado
                os.write(2, ("\r\nPuerto serie: %s\r\n" % e).encode())
                stop.set()
                return
            if data:
                link.feed(data)
            else:
                link.idle()

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    if args.no_reset:
        write_mega(b"\x1d")  # Ctrl-]: redibujar

    restore = None
    if os.name == "posix" and sys.stdin.isatty():
        import termios
        import tty
        fd = sys.stdin.fileno()
        saved = termios.tcgetattr(fd)
        tty.setraw(fd)
        restore = lambda: termios.tcsetattr(fd, termios.TCSADRAIN, saved)

    prefix = False
    try:
        for byte in user_bytes():
            if stop.is_set():
                break
            if prefix:
                prefix = False
                c = byte.lower()
                if c == b"q":
                    break
                if c == b"r":
                    deck.rewind()
                elif c == b"i":
                    names = [b[4:b.index(b"\r", 4)].decode("ascii", "replace") if b[:4] == b"****" and b"\r" in b[4:]
                             else "?" for b in deck.blocks]
                    log("cassette: " + (", ".join(names) if names else "vacio") + " | posicion %d" % deck.pos)
                elif byte == b"\x14":
                    write_mega(byte)
                continue
            if byte == b"\x14":
                prefix = True
                continue
            write_mega(byte)
    finally:
        stop.set()
        if restore:
            restore()
        ser.close()
        os.write(1, b"\r\n")


def user_bytes():
    """Genera los bytes que teclea el usuario, de uno en uno."""
    if os.name == "nt":
        import msvcrt
        keys = {b"H": b"\x1b[A", b"P": b"\x1b[B", b"M": b"\x1b[C", b"K": b"\x1b[D"}
        while True:
            ch = msvcrt.getch()
            if ch in (b"\x00", b"\xe0"):
                seq = keys.get(msvcrt.getch(), b"")
                for b in seq:
                    yield bytes([b])
            else:
                yield ch
    else:
        fd = sys.stdin.fileno()
        while True:
            data = os.read(fd, 256)
            if not data:
                return
            for b in data:
                yield bytes([b])


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "info":
        return cmd_info(sys.argv[2])
    if len(sys.argv) >= 2 and sys.argv[1] == "mktap":
        return cmd_mktap(sys.argv[2:])
    if len(sys.argv) >= 2 and sys.argv[1] == "extract":
        return cmd_extract(sys.argv[2:])
    ap = argparse.ArgumentParser(description="Terminal serie del Acorn System 2 con cassette virtual")
    ap.add_argument("port", help="puerto serie del Mega (/dev/ttyACM0, COM3...)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--tape", default="cassette.tap", help="fichero del cassette (se crea al grabar)")
    ap.add_argument("--no-reset", action="store_true", help="no reiniciar el Mega al abrir el puerto")
    run_terminal(ap.parse_args())


if __name__ == "__main__":
    main()
