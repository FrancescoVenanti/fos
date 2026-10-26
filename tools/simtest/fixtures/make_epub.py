#!/usr/bin/env python3
"""Writes small EPUB 3 test books (original text) for the Reader scenarios.

    python tools/simtest/fixtures/make_epub.py   # -> tools/simtest/sd-overlays/reader-books/books/
"""

import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "sd-overlays", "reader-books", "books")

BOOKS = [
    {
        "file": "The Lighthouse Keeper.epub",
        "id": "fos-test-lighthouse",
        "title": "The Lighthouse Keeper",
        "author": "Ada Marino",
        "chapters": [
            ("The Rock", [
                "The lighthouse stood on a rock that the sea had been trying to swallow for three hundred years. "
                "It had not succeeded yet, and Elio, who kept the light, did not intend to let it start now.",
                "Every evening he climbed the hundred and twelve steps with a can of oil in one hand and a book in "
                "the other. The oil was for the lamp. The book was for the long hours when nothing happened, which "
                "was most of them.",
                "He had read every book on the island twice. The supply boat brought new ones in spring, wrapped in "
                "oilcloth, and he rationed them like coffee.",
            ]),
            ("The Storm", [
                "The storm came in the second week of November, low and grey, with a wind that pressed the door shut "
                "so hard he had to lean on it with his whole weight.",
                "He kept the lamp turning all night. Somewhere out there a fishing boat was looking for the harbour, "
                "and the only thing between it and the rocks was a circle of light that he refused to let go out.",
                "At dawn the wind dropped. A small boat rounded the point, its crew waving their caps at the tower.",
            ]),
            ("Spring", [
                "In spring the supply boat brought flour, lamp oil, a letter from his sister and seven new books.",
                "He carried them up the hundred and twelve steps, set them on the shelf by the window, and chose the "
                "thinnest one to start with, because a man should save the best for last.",
            ]),
        ],
    },
    {
        "file": "Notes on Small Machines.epub",
        "id": "fos-test-machines",
        "title": "Notes on Small Machines",
        "author": "Tomas Reiner",
        "chapters": [
            ("Why small", [
                "A small machine does one thing. It does not ask to be updated, it does not want your attention, and "
                "when it is done it goes quiet.",
                "This is a book about such machines: clocks, calculators, pocket notebooks with batteries.",
            ]),
            ("Paper that changes", [
                "Electronic paper holds its image without power. Tiny capsules of pigment move up or down and then "
                "stay there, which is why a reader can sit on a shelf for a month and still show the page you left.",
            ]),
        ],
    },
]

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""


def chapter_xhtml(title, paragraphs):
    body = "\n".join("<p>%s</p>" % p for p in paragraphs)
    return ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n"
            "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>%s</title></head>\n"
            "<body><h1>%s</h1>\n%s\n</body></html>\n" % (title, title, body))


def write_book(book):
    chapters = book["chapters"]
    manifest = "\n".join('<item id="c%d" href="c%d.xhtml" media-type="application/xhtml+xml"/>' % (i, i)
                         for i in range(len(chapters)))
    spine = "\n".join('<itemref idref="c%d"/>' % i for i in range(len(chapters)))
    nav_items = "\n".join('<li><a href="c%d.xhtml">%s</a></li>' % (i, t) for i, (t, _) in enumerate(chapters))
    opf = ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"uid\">\n"
           "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
           "<dc:identifier id=\"uid\">%s</dc:identifier><dc:title>%s</dc:title><dc:creator>%s</dc:creator>\n"
           "<dc:language>en</dc:language><meta property=\"dcterms:modified\">2026-01-01T00:00:00Z</meta>\n"
           "</metadata>\n<manifest>\n"
           "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n%s\n"
           "</manifest>\n<spine>\n%s\n</spine>\n</package>\n" % (book["id"], book["title"], book["author"], manifest,
                                                                 spine))
    nav = ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n"
           "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
           "<head><title>Contents</title></head><body><nav epub:type=\"toc\"><ol>\n%s\n</ol></nav></body></html>\n"
           % nav_items)
    path = os.path.join(OUT, book["file"])
    with zipfile.ZipFile(path, "w") as z:
        # The mimetype entry must be first and stored uncompressed.
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/nav.xhtml", nav, compress_type=zipfile.ZIP_DEFLATED)
        for i, (title, paragraphs) in enumerate(chapters):
            z.writestr("OEBPS/c%d.xhtml" % i, chapter_xhtml(title, paragraphs), compress_type=zipfile.ZIP_DEFLATED)
    print(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    for book in BOOKS:
        write_book(book)


if __name__ == "__main__":
    main()
