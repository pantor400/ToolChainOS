#!/usr/bin/env python3
"""TW/1 HTTP(S) bridge. Only a private QEMU Unix socket; no listening TCP.
TLS and HTTP run on Ubuntu. Numeric connections pin validated DNS addresses.
"""
import argparse
import html.parser
import http.client
import ipaddress
import json
import os
import queue
import select
import socket
import ssl
import struct
import threading
import time
import urllib.parse
from pathlib import Path

MAX_URL = 1023
MAX_FRAME = 1024
MAX_DOC = 24576
MAX_DOWNLOAD = 262144
MAX_RECORDS = 128
MAX_LINKS = 48
TIMEOUT = 10
HEADER = struct.Struct('<2sBBIHH')
DNS_SLOTS = threading.BoundedSemaphore(2)


class WebError(ValueError):
    pass


def frame(kind, request, body=b''):
    if len(body) > MAX_FRAME:
        raise WebError('Frame demasiado grande')
    head = HEADER.pack(b'TW', 1, kind, request, len(body), 0)
    checksum = (sum(head[:10]) + sum(body)) & 65535
    return head[:10] + struct.pack('<H', checksum) + body


class Decoder:
    def __init__(self):
        self.data = bytearray()

    def feed(self, data):
        # Callers read <=4096 bytes. Buffer never trusts a declared length.
        self.data.extend(data)
        result = []
        while len(self.data) >= 2:
            if self.data[:2] != b'TW':
                del self.data[0]
                continue
            if len(self.data) < 12:
                break
            _, version, kind, rid, length, checksum = HEADER.unpack(self.data[:12])
            if version != 1 or length > MAX_FRAME:
                del self.data[0]
                continue
            if len(self.data) < length + 12:
                break
            packet = self.data[:length + 12]
            if (sum(packet[:10]) + sum(packet[12:])) & 65535 != checksum:
                del self.data[0]
                continue
            result.append((kind, rid, bytes(packet[12:])))
            del self.data[:length + 12]
        return result


def normalize(url):
    url = url.strip()
    if not url or len(url.encode('utf-8')) > MAX_URL or any(ord(c) <= 32 or ord(c) == 127 for c in url):
        raise WebError('URL inválida o demasiado larga')
    if '://' not in url:
        if ':' in url.split('/')[0]:
            raise WebError('Esquema no admitido')
        url = 'https://' + url
    try:
        p = urllib.parse.urlsplit(url)
        if p.scheme not in ('http', 'https') or not p.hostname or p.username is not None or p.password is not None:
            raise ValueError()
        host = p.hostname.encode('idna').decode('ascii').lower()
        port = p.port or (443 if p.scheme == 'https' else 80)
        if port not in (80, 443) or '%' in host or '\\' in url:
            raise ValueError()
        ip_host = '[' + host + ']' if ':' in host else host
        netloc = ip_host + ((':' + str(p.port)) if p.port else '')
        path = urllib.parse.quote(p.path or '/', safe="/%:@!$&'()*+,;=-._~")
        query = urllib.parse.quote(p.query, safe="/%?:@!$&'()*+,;=-._~")
        result = urllib.parse.urlunsplit((p.scheme, netloc, path, query, ''))
        if len(result.encode()) > MAX_URL:
            raise ValueError()
        return result
    except (ValueError, UnicodeError):
        raise WebError('URL, esquema o puerto no admitido') from None


def public_addresses(host, port):
    if host.lower().rstrip('.') == 'localhost' or host.lower().rstrip('.').endswith(('.localhost', '.local', '.internal')):
        raise WebError('Destino interno bloqueado')
    # A-only DNS in v1 avoids waiting on unsupported guest/host IPv6 paths.
    # Numeric IPv6 is still checked by the same public-address policy.
    family = socket.AF_INET6 if ':' in host else socket.AF_INET
    # Bound libc DNS waits without changing WSL/Windows resolver settings.
    # At most two outstanding system resolver threads; DoH is TLS verified.
    answers = queue.Queue(maxsize=1)
    def system_dns():
        try:
            answers.put(socket.getaddrinfo(host, port, family=family, type=socket.SOCK_STREAM))
        except OSError as error:
            answers.put(error)
        finally:
            DNS_SLOTS.release()
    if DNS_SLOTS.acquire(blocking=False):
        threading.Thread(target=system_dns, daemon=True).start()
    try:
        resolved = answers.get(timeout=1)
        if isinstance(resolved, Exception):
            raise resolved
        ips = list(dict.fromkeys(answer[4][0] for answer in resolved))
    except (queue.Empty, OSError):
        # Fixed public resolver, numeric bootstrap, verified hostname/SNI.
        # API: developers.cloudflare.com/1.1.1.1/encryption/dns-over-https/
        if family == socket.AF_INET6:
            raise WebError('Sin conexión: DNS IPv6 no disponible')
        resolver = PinnedHTTP('cloudflare-dns.com', 443, '1.1.1.1', 3, True)
        try:
            resolver.request('GET', '/dns-query?name=' + urllib.parse.quote(host, safe='') + '&type=A',
                             headers={'Accept': 'application/dns-json', 'Connection': 'close'})
            response = resolver.getresponse()
            body = response.read(16385)
            if response.status != 200 or len(body) > 16384:
                raise WebError('Sin conexión: DNS sobre HTTPS rechazado')
            data = json.loads(body)
            if data.get('Status') != 0:
                raise WebError('Sin conexión: nombre DNS no encontrado')
            ips = list(dict.fromkeys(item['data'] for item in data.get('Answer', []) if item.get('type') == 1))[:32]
            print(json.dumps({'dns': 'DoH fallback TLS', 'host': host}), flush=True)
        finally:
            resolver.close()
    if not ips:
        raise WebError('Sin conexión: DNS sin respuesta')
    for value in ips:
        ip = ipaddress.ip_address(value)
        if not ip.is_global or ip.is_multicast or ip.is_reserved or getattr(ip, "is_site_local", False) or (ip.version == 6 and (ip.ipv4_mapped or ip.sixtofour or ip.teredo)):
            raise WebError('Destino interno bloqueado')
    return ips


class PinnedHTTP(http.client.HTTPConnection):
    def __init__(self, host, port, address, timeout, tls=False):
        super().__init__(host, port, timeout=timeout)
        self.address = address
        self.tls = tls

    def connect(self):
        # No second DNS lookup: the connection uses the validated numeric IP.
        self.sock = socket.create_connection((self.address, self.port), self.timeout)
        if self.tls:
            try:
                self.sock = ssl.create_default_context().wrap_socket(self.sock, server_hostname=self.host)
            except BaseException:
                self.sock.close()
                raise


def clean(text):
    return ''.join(c for c in text if c in '\n\t' or ord(c) >= 32 and ord(c) != 127)


class Document(html.parser.HTMLParser):
    def __init__(self, url):
        super().__init__(convert_charrefs=True)
        self.url = url
        self.title = ''
        self.in_title = False
        self.hidden = 0
        self.link = ''
        self.text = ''
        self.kind = 0
        self.records = []
        self.links = 0
        self.scripts = False
        self.truncated = False

    def flush(self):
        text = ' '.join(clean(self.text).split())
        self.text = ''
        if text and len(self.records) < MAX_RECORDS:
            # UTF-8 byte limits are applied again during serialization.
            self.records.append((self.kind, text[:1024], self.link))
        elif text:
            self.truncated = True

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag in ('script', 'style', 'noscript', 'svg', 'template'):
            self.hidden += 1
            self.scripts |= tag == 'script'
        if tag == 'title':
            self.in_title = True
        if self.hidden:
            return
        if tag in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'div', 'br', 'li', 'pre', 'tr', 'ul', 'ol'):
            self.flush()
            self.kind = 1 if tag.startswith('h') and len(tag) == 2 else 0
            if tag == 'li':
                self.text = '• '
        if tag == 'a':
            self.flush()
            target = urllib.parse.urljoin(self.url, attrs.get('href', ''))
            try:
                target = normalize(target)
            except WebError:
                target = ''
            if target and self.links < MAX_LINKS:
                self.link = target
                self.kind = 2
                self.links += 1

    def handle_endtag(self, tag):
        if tag == 'title':
            self.in_title = False
        if tag in ('script', 'style', 'noscript', 'svg', 'template'):
            self.hidden = max(0, self.hidden - 1)
            return
        if self.hidden:
            return
        if tag == 'a':
            self.flush()
            self.link = ''
            self.kind = 0
        if tag in ('h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'div', 'li', 'pre', 'tr'):
            self.flush()
            self.kind = 0

    def handle_data(self, text):
        if self.in_title:
            self.title += text
        elif not self.hidden:
            self.text += text
            if len(self.text) > 4096:
                self.flush()


def utf8(text, maximum):
    return clean(text).encode('utf-8')[:maximum].decode('utf-8', 'ignore').encode('utf-8')


def document(status, url, title, message, records):
    u, t, m = utf8(url, MAX_URL), utf8(title, 192), utf8(message, 240)
    parts = []
    used = 10 + len(u) + len(t) + len(m) + 64
    truncated = False
    for kind, text, target in records[:MAX_RECORDS]:
        text, target = utf8(text, 2048), utf8(target, MAX_URL)
        rec = struct.pack('<BHH', kind, len(text), len(target)) + text + target
        if used + len(rec) > MAX_DOC:
            truncated = True
            break
        parts.append(rec)
        used += len(rec)
    if truncated:
        m = utf8(message + ' Contenido recortado al límite de 24 KiB.', 240)
    return struct.pack('<HHHHH', status, len(u), len(t), len(m), len(parts)) + u + t + m + b''.join(parts)


def fetch(url, cancelled=None):
    url = normalize(url)
    deadline = time.monotonic() + TIMEOUT
    cancelled = cancelled or threading.Event()
    for redirect in range(5):
        if cancelled.is_set():
            raise WebError('Cancelado')
        p = urllib.parse.urlsplit(url)
        port = p.port or (443 if p.scheme == 'https' else 80)
        addresses = public_addresses(p.hostname, port)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError()
        connection = PinnedHTTP(p.hostname, port, addresses[0], min(3, remaining), p.scheme == 'https')
        try:
            path = urllib.parse.urlunsplit(('', '', p.path, p.query, ''))
            connection.request('GET', path, headers={'User-Agent': 'ToolchainOS-Web/1 educational', 'Accept': 'text/html,text/plain', 'Accept-Encoding': 'identity', 'Connection': 'close'})
            response = connection.getresponse()
            status = response.status
            if status in (301, 302, 303, 307, 308):
                location = response.getheader('Location')
                if not location or redirect == 4:
                    raise WebError('Demasiadas redirecciones o destino ausente')
                url = normalize(urllib.parse.urljoin(url, location))
                continue
            mime = response.headers.get_content_type()
            if mime not in ('text/html', 'text/plain'):
                raise WebError('Contenido no compatible: ' + mime)
            if response.getheader('Content-Encoding', 'identity').lower() != 'identity':
                raise WebError('Contenido comprimido no compatible')
            length = response.getheader('Content-Length')
            if length and (not length.isdigit() or int(length) > MAX_DOWNLOAD):
                raise WebError('Respuesta demasiado grande')
            data = bytearray()
            while True:
                if cancelled.is_set():
                    raise WebError('Cancelado')
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError()
                if connection.sock:
                    connection.sock.settimeout(min(3, remaining))
                chunk = response.read1(min(4096, MAX_DOWNLOAD + 1 - len(data)))
                if not chunk:
                    break
                data.extend(chunk)
                if len(data) > MAX_DOWNLOAD:
                    raise WebError('Respuesta demasiado grande')
            charset = response.headers.get_content_charset() or 'utf-8'
            try:
                text = data.decode(charset, 'replace')
            except LookupError:
                text = data.decode('utf-8', 'replace')
            if mime == 'text/plain':
                title, records, note = p.hostname, [(0, line, '') for line in text.splitlines()[:MAX_RECORDS]], ''
            else:
                parser = Document(url)
                parser.feed(text)
                parser.flush()
                title = ' '.join(parser.title.split()) or p.hostname
                records = parser.records
                note = 'HTML estático; JavaScript no se ejecuta.' if parser.scripts else 'HTML básico; sin CSS ni JavaScript.'
                if parser.truncated:
                    note += ' Página recortada.'
            if status == 404:
                note = '404: Página no encontrada. ' + note
            elif status >= 400:
                note = 'Error HTTP ' + str(status) + '. ' + note
            print(json.dumps({'url': url, 'http': status, 'download_bytes': len(data), 'title': title}, ensure_ascii=False), flush=True)
            return document(status, url, title, note, records)
        finally:
            connection.close()
    raise WebError('Demasiadas redirecciones')


def error_document(url, error):
    if isinstance(error, ssl.SSLCertVerificationError):
        message = 'HTTPS no válido: certificado rechazado'
    elif isinstance(error, (TimeoutError, socket.timeout)):
        message = 'Tiempo de espera agotado'
    elif isinstance(error, (OSError, http.client.HTTPException)):
        message = 'Sin conexión: ' + str(error)[:120]
    else:
        message = str(error)[:200]
    return document(0, url, 'Error de navegación', message, [(0, message, '')])


def serve(path, stop=None, fetcher=fetch):
    """Connect to QEMU's socket. QEMU/launcher owns creation and unlinking."""
    stop = stop or threading.Event()
    jobs, results = queue.Queue(maxsize=1), queue.Queue(maxsize=1)
    cancel = threading.Event()
    def worker():
        while not stop.is_set():
            try:
                rid, url, token = jobs.get(timeout=.1)
            except queue.Empty:
                continue
            try:
                result = fetcher(url, token)
                if not isinstance(result, bytes) or len(result) > MAX_DOC:
                    raise WebError('Respuesta demasiado grande: representación rechazada')
            except Exception as error:
                result = error_document(url, error)
            if not token.is_set():
                while not stop.is_set():
                    try:
                        results.put((rid, result, token), timeout=.1)
                        break
                    except queue.Full:
                        if token.is_set():
                            break
    threading.Thread(target=worker, daemon=True).start()
    conn = socket.socket(socket.AF_UNIX)
    conn.settimeout(1)
    end = time.monotonic() + 5
    while True:
        try:
            conn.connect(str(path))
            break
        except (FileNotFoundError, ConnectionRefusedError):
            if time.monotonic() > end:
                raise
            time.sleep(.03)
    conn.settimeout(1)
    decoder = Decoder()
    active = 0
    try:
        while not stop.is_set():
            if select.select([conn], [], [], .01)[0]:
                data = conn.recv(4096)
                if not data:
                    break
                for kind, rid, body in decoder.feed(data):
                    if kind == 3:
                        if rid == active:
                            cancel.set()
                            active = 0
                    elif kind == 1:
                        cancel.set()
                        cancel = threading.Event()
                        active = rid
                        conn.sendall(frame(0x80, rid))
                        raw_url = ''
                        try:
                            raw_url = body.decode('utf-8')
                            url = normalize(raw_url)
                            try:
                                jobs.get_nowait()
                            except queue.Empty:
                                pass
                            jobs.put_nowait((rid, url, cancel))
                        except (ValueError, UnicodeError) as error:
                            try:
                                results.get_nowait()
                            except queue.Empty:
                                pass
                            results.put_nowait((rid, error_document(raw_url, error), cancel))
            try:
                rid, result, token = results.get_nowait()
            except queue.Empty:
                continue
            if token.is_set() or rid != active:
                continue
            for offset in range(0, len(result), 512):
                if token.is_set() or stop.is_set():
                    break
                conn.sendall(frame(0x81, rid, result[offset:offset + 512]))
            conn.sendall(frame(0x82, rid))
    except (ConnectionResetError, BrokenPipeError):
        pass  # QEMU closed; normal lifecycle, no thread traceback.
    finally:
        cancel.set()
        stop.set()
        conn.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket', required=True)
    args = parser.parse_args()
    path = Path(args.socket)
    # Only connect to a same-user Unix socket inside this project's build.
    import stat
    info = path.lstat()
    build = Path(__file__).resolve().parents[1] / 'build'
    if path.parent.resolve() != build.resolve() or not stat.S_ISSOCK(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise SystemExit('Socket no privado o fuera de build/')
    try:
        serve(path)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
