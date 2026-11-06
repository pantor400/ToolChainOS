#!/usr/bin/env python3
"""Host tests: real bridge policy/parser/protocol; mocks never claim Internet."""
import email.message
import socket
import ssl
import struct
import threading
import unittest
from unittest.mock import patch
import web_bridge as w


class HostTests(unittest.TestCase):
    def test_fragmentation_checksum_limits(self):
        packet = w.frame(1, 45, b'https://example.com/')
        decoder = w.Decoder()
        out = []
        for byte in b'noise' + packet:
            out.extend(decoder.feed(bytes([byte])))
        self.assertEqual(out, [(1, 45, b'https://example.com/')])
        damaged = bytearray(packet)
        damaged[-1] ^= 1
        self.assertEqual(w.Decoder().feed(damaged + packet), out)
        oversized = w.HEADER.pack(b'TW', 1, 1, 12, 65535, 0)
        self.assertEqual(w.Decoder().feed(oversized + packet), out)
        self.assertEqual(w.Decoder().feed(packet + packet), out * 2)
        with self.assertRaises(w.WebError):
            w.frame(1, 2, b'x' * 1025)
        decoder = w.Decoder()
        for _ in range(100):
            decoder.feed(b'x' * 4096)
        self.assertLess(len(decoder.data), 12)

    def test_schemes_and_credentials(self):
        for url in ['file:///etc/passwd', 'javascript:alert(1)', 'data:text/html,x', 'ftp://example.com', '/etc/passwd', 'https://u:p@example.com', 'https://example.com:11434', 'https://example.com/\r\nx', 'https://example.com\\@localhost', 'x' * 1024]:
            with self.subTest(url=url), self.assertRaises(w.WebError):
                w.normalize(url)
        self.assertEqual(w.normalize('example.com'), 'https://example.com/')
        self.assertEqual(w.normalize('https://example.com/a#fragment'), 'https://example.com/a')

    def test_internal_dns_mixed_answers(self):
        for value in ['127.0.0.1', '10.0.0.1', '172.16.0.1', '192.168.2.1', '169.254.169.254', '0.0.0.0', '224.0.0.1', '::1', 'fc00::1', 'fe80::1', '::ffff:127.0.0.1']:
            with self.subTest(value=value), patch.object(socket, 'getaddrinfo', return_value=[(2, 1, 6, '', (value, 80))]), self.assertRaises(w.WebError):
                w.public_addresses('test.example', 80)
        with patch.object(socket, 'getaddrinfo', return_value=[(2,1,6,'',('93.184.215.14',80)), (2,1,6,'',('127.0.0.1',80))]), self.assertRaises(w.WebError):
            w.public_addresses('test.example', 80)
        for host in ['localhost', 'LOCALHOST.', 'test.local', 'secret.internal']:
            with self.assertRaises(w.WebError):
                w.public_addresses(host, 80)

    def test_numeric_connection_and_tls(self):
        with patch.object(socket, 'create_connection') as connect:
            c = w.PinnedHTTP('example.com', 80, '93.184.215.14', 3)
            c.connect()
            connect.assert_called_once_with(('93.184.215.14', 80), 3)
        with patch.object(socket, 'create_connection'), patch.object(ssl, 'create_default_context') as tls:
            c = w.PinnedHTTP('example.com', 443, '93.184.215.14', 3, True)
            c.connect()
            tls.return_value.wrap_socket.assert_called_once_with(socket.create_connection.return_value, server_hostname='example.com')

    def test_html_spanish_relative_hidden(self):
        p = w.Document('https://example.com/dir/index')
        p.feed('<title>Perú &amp; año</title><h1>¡Hola!</h1><p>á é í ó ú ü ñ ¿Sí?</p><ul><li>uno</li></ul><a href="../next">Siguiente &gt;</a><script>secret()</script><style>bad</style>')
        p.flush()
        self.assertEqual(p.title, 'Perú & año')
        self.assertTrue(any('á é í ó ú ü ñ ¿Sí?' in text for _, text, _ in p.records))
        self.assertIn((2, 'Siguiente >', 'https://example.com/next'), p.records)
        self.assertNotIn('secret', repr(p.records))
        self.assertTrue(p.scripts)
        doc = w.document(200, p.url, p.title, '', p.records)
        self.assertLessEqual(len(doc), w.MAX_DOC)
        self.assertIn('año'.encode(), doc)

    def test_document_resource_caps(self):
        p = w.Document('https://example.com')
        p.feed(''.join('<p>' + 'a' * 2000 + '</p><a href="/x">link</a>' for _ in range(300)))
        p.flush()
        self.assertLessEqual(len(p.records), 128)
        self.assertLessEqual(p.links, 48)
        self.assertLessEqual(len(w.document(200, p.url, '', '', p.records)), w.MAX_DOC)

    def test_errors(self):
        for error, needle in [(ssl.SSLCertVerificationError(), 'HTTPS no válido'), (TimeoutError(), 'Tiempo de espera agotado'), (OSError('DNS'), 'Sin conexión'), (w.WebError('Contenido no compatible'), 'Contenido no compatible')]:
            self.assertIn(needle.encode(), w.error_document('https://example.com/', error))

    def test_redirect_revalidation_size_and_mime(self):
        class Response:
            status = 302
            def getheader(self, name, default=None):
                return {'Location': 'http://127.0.0.1:80/'}.get(name, default)
        class Connection:
            def __init__(self, *args): pass
            def request(self, *args, **kwargs): pass
            def getresponse(self): return Response()
            def close(self): pass
        real_addresses = w.public_addresses
        def addresses(host, port):
            if host == 'example.com': return ['93.184.215.14']
            return real_addresses(host, port)
        with patch.object(w, 'PinnedHTTP', Connection), patch.object(w, 'public_addresses', addresses), self.assertRaises(w.WebError):
            w.fetch('https://example.com/')
        Response.status = 200
        Response.headers = email.message.Message()
        Response.headers['Content-Type'] = 'application/pdf'
        with patch.object(w, 'PinnedHTTP', Connection), patch.object(w, 'public_addresses', return_value=['93.184.215.14']), self.assertRaisesRegex(w.WebError, 'Contenido no compatible'):
            w.fetch('https://example.com/')
        Response.headers.replace_header('Content-Type', 'text/html')
        Response.getheader = lambda self, name, default=None: '9999999' if name == 'Content-Length' else default
        with patch.object(w, 'PinnedHTTP', Connection), patch.object(w, 'public_addresses', return_value=['93.184.215.14']), self.assertRaisesRegex(w.WebError, 'demasiado grande'):
            w.fetch('https://example.com/')


class RedirectTests(unittest.TestCase):
    def test_allowed_public_redirect(self):
        """Mock HTTP responses only; verifies allowed redirect and final URL."""
        class Response:
            def __init__(self, code, body=b'', location=None):
                self.status, self.body, self.location = code, body, location
                self.headers = email.message.Message()
                self.headers['Content-Type'] = 'text/html; charset=utf-8'
            def getheader(self, name, default=None):
                return self.location if name == 'Location' else default
            def read1(self, count):
                piece, self.body = self.body[:count], self.body[count:]
                return piece
        responses = [Response(302, location='/next'),
                     Response(200, '<title>Redirección permitida</title><p>Perú</p>'.encode())]
        paths = []
        class Connection:
            sock = None
            def __init__(self, *args): pass
            def request(self, method, path, **kwargs):
                self.path = path
                paths.append((method, path))
            def getresponse(self):
                return responses.pop(0)
            def close(self): pass
        with patch.object(w, 'PinnedHTTP', Connection), patch.object(w, 'public_addresses', return_value=['93.184.215.14']) as validate:
            result = w.fetch('https://public.example/start')
        self.assertEqual(paths, [('GET', '/start'), ('GET', '/next')])
        self.assertEqual(validate.call_count, 2)
        status, url_size, _, _, _ = struct.unpack('<HHHHH', result[:10])
        self.assertEqual(status, 200)
        self.assertEqual(result[10:10+url_size], b'https://public.example/next')
        self.assertIn('Redirección permitida'.encode(), result)


if __name__ == '__main__':
    unittest.main(verbosity=2)
