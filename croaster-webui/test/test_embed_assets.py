"""Host tests for croaster-webui/embed_assets.py. Run: python3 -m unittest discover croaster-webui/test"""

import gzip
import importlib.util
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("embed_assets", os.path.join(HERE, "..", "embed_assets.py"))
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)
WEB = os.path.join(HERE, "..", "web")


class EmbedAssetsTest(unittest.TestCase):
    def test_output_is_deterministic(self):
        files = embed.sources(WEB)
        digest = embed.source_hash(files)
        self.assertEqual(embed.render(files, digest), embed.render(files, digest))

    def test_gzip_has_no_timestamp_and_round_trips(self):
        body = embed.gz(b"<html></html>")
        self.assertEqual(body[4:8], b"\0\0\0\0")  # MTIME field
        self.assertEqual(gzip.decompress(body), b"<html></html>")

    def test_hash_follows_content_and_names(self):
        a = embed.source_hash([("a.js", b"1")])
        self.assertNotEqual(a, embed.source_hash([("a.js", b"2")]))
        self.assertNotEqual(a, embed.source_hash([("b.js", b"1")]))

    def test_crlf_checkout_gives_the_same_output(self):
        lf = embed.sources(WEB)
        with tempfile.TemporaryDirectory() as tmp:
            for name, data in lf:
                with open(os.path.join(tmp, name), "wb") as f:
                    f.write(data.replace(b"\n", b"\r\n"))
            crlf = embed.sources(tmp)
        self.assertEqual(crlf, lf)
        self.assertEqual(embed.source_hash(crlf), embed.source_hash(lf))

    def test_committed_header_is_current(self):
        self.assertEqual(embed.run(check=True), 0, "run python3 croaster-webui/embed_assets.py")

    def test_page_fits_the_budget(self):
        _, total = embed.render(embed.sources(WEB), "x")
        self.assertLess(total, embed.BUDGET_BYTES)

    def test_index_is_served_at_root(self):
        text, _ = embed.render(embed.sources(WEB), "x")
        self.assertIn('{"/", "text/html; charset=utf-8", kindex_html', text)


if __name__ == "__main__":
    unittest.main()
