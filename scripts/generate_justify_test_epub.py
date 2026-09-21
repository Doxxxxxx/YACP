#!/usr/bin/env python3
"""
Generate a test EPUB for paragraph justification.

The book contains long paragraphs (80 to 130 words) so that most lines are full
lines, which is the only place where Justify and Left differ. The stylesheet
sets no text-align, so the Reader "Paragraph Alignment" setting decides.

Chapters:
  1. English prose, ordinary word lengths.
  2. French prose with accents, apostrophes, guillemets and narrow no-break
     spaces before high punctuation (`;`, `:`, `!`, `?`).
  3. Long words, so lines hold only two to four gaps and any per-gap cap on the
     justification stretch becomes visible.

Expected result with Paragraph Alignment = Justify: every line except the last
one of each paragraph ends exactly at the right margin. With Left, lines end
where their last word ends.
"""

import os
import zipfile
from pathlib import Path

OUTPUT_DIR = Path(__file__).parent.parent / "test" / "epubs"
OUTPUT_PATH = OUTPUT_DIR / "test_justify_alignment.epub"

CSS = """\
body { margin: 0; padding: 0; }
p    { margin-top: 0; margin-bottom: 0; text-indent: 1em; }
h1   { text-align: center; margin-top: 0.5em; margin-bottom: 0.5em; }
"""


def xhtml(title, lang, body):
    return f"""\
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="{lang}" lang="{lang}">
<head>
  <title>{title}</title>
  <link rel="stylesheet" type="text/css" href="styles/test.css"/>
</head>
<body>
{body}
</body>
</html>"""


def paragraphs(texts):
    return "\n".join(f"<p>{t}</p>" for t in texts)


# ---------------------------------------------------------------------------
# Chapter 1 - English prose
# ---------------------------------------------------------------------------
EN = [
    "PASS: with Paragraph Alignment set to Justify, every line of the paragraphs below except the last line of "
    "each paragraph must end exactly at the right margin. With Left, the right edge is ragged. Compare full lines "
    "only, since the last line of a paragraph is never stretched in either mode.",
    "The lighthouse keeper had kept the same routine for thirty-one years, and the routine had kept him. He rose "
    "before the gulls, trimmed the wick that no longer needed trimming since the lamp had gone electric, and wrote "
    "the weather in a ledger nobody read. The ledger mattered to him because it proved that the days had happened. "
    "When his daughter visited in August she counted the volumes on the shelf, forty of them, bound in cloth that "
    "had faded from green to the colour of old tea, and she understood for the first time that her father had never "
    "been lonely, only quiet, which was a different thing altogether.",
    "Below the tower the village argued about the ferry. Half the council wanted a bigger boat, the other half "
    "wanted fewer visitors, and both halves agreed that the harbour wall was crumbling faster than the grant "
    "application could be written. The keeper listened to the meetings on the radio, the way other people listened "
    "to plays, and formed no opinion. He had watched the wall for three decades and knew it would outlast the "
    "argument, the council, and very probably the ferry. Stone was patient. People were not, and that was the whole "
    "of the difference between a harbour and a committee.",
    "On the night of the equinox the wind turned and brought the smell of the mainland across the strait, cut grass "
    "and diesel and something sweet that might have been a bakery. He stood on the gallery with his coat open and "
    "let the smell settle on him. Tomorrow he would write it in the ledger under the barometric pressure, not "
    "because a smell was weather but because it had happened, and he was the only one there to say so. The lamp "
    "turned behind him, white then dark then white, indifferent and entirely reliable, exactly as he preferred his "
    "company to be.",
]

# ---------------------------------------------------------------------------
# Chapter 2 - French prose with accents and typographic spacing
# ---------------------------------------------------------------------------
NNBSP = "\u202f"  # narrow no-break space before high punctuation
FR = [
    f"PASS{NNBSP}: avec l'alignement Justifié, chaque ligne des paragraphes ci-dessous, sauf la dernière de chaque "
    f"paragraphe, doit atteindre exactement la marge droite. En Gauche, le bord droit est en drapeau. Ne comparer "
    f"que les lignes pleines{NNBSP}; la dernière ligne d'un paragraphe n'est jamais étirée.",
    f"La gardienne de l'écluse avait appris à lire l'eau avant d'apprendre à lire les livres. Elle savait à la "
    f"couleur du courant si la pluie était tombée en amont, et au bruit des vannes si un bateau attendait derrière "
    f"le coude du canal. Les mariniers l'appelaient «{NNBSP}la vieille{NNBSP}» depuis qu'elle avait vingt ans, sans "
    f"méchanceté, parce qu'elle tenait la maison éclusière comme on tient une promesse{NNBSP}: sans y penser, et "
    f"sans jamais la lâcher. Quand la navigation s'arrêta, l'hiver de la grande gelée, elle continua d'ouvrir les "
    f"portes chaque matin, pour que le canal se souvienne qu'il servait à quelque chose.",
    f"Le village, lui, avait déjà oublié. On avait comblé le bassin pour faire un parking, puis on avait regretté "
    f"le bassin et planté des tilleuls sur le parking. Les tilleuls poussaient mal{NNBSP}; la terre en dessous "
    f"gardait la mémoire du béton. Le maire parlait d'un «{NNBSP}projet de valorisation{NNBSP}» à chaque "
    f"élection, "
    f"et à chaque élection le projet changeait de nom sans changer de contenu. La gardienne écoutait ces discours "
    f"comme elle écoutait les vannes{NNBSP}: pour savoir ce qui arrivait derrière, et non pour ce qu'ils disaient.",
    f"Un soir d'équinoxe, un bateau vint pourtant, une péniche basse chargée de sable, dont le patron ne "
    f"connaissait "
    f"plus le canal que par les cartes de son grand-père. Elle manœuvra les vannes sans un mot, fit monter l'eau, "
    f"regarda la coque s'élever lentement entre les murs verdis, et, quand la porte d'amont s'ouvrit sur la brume, "
    f"elle dit seulement{NNBSP}: «{NNBSP}Vous pouvez passer.{NNBSP}» Le patron la remercia, gêné, comme on "
    f"remercie "
    f"une horloge d'avoir donné l'heure. Elle referma les portes derrière lui. Le canal avait servi{NNBSP}; cela "
    f"suffisait pour aujourd'hui.",
]

# ---------------------------------------------------------------------------
# Chapter 3 - long words, few gaps per line
# ---------------------------------------------------------------------------
LONG = [
    "PASS: these paragraphs use long words so each line holds only two to four gaps. With Justify, the gaps on a "
    "full line grow as much as needed to reach the right margin, even when that means several times a normal "
    "space. With Left, the lines end early. A justified line that stops short of the margin is a failure.",
    "Uncharacteristically, the parliamentary subcommittee's counterproductive recommendations disproportionately "
    "disadvantaged internationally recognised environmentalists, notwithstanding overwhelmingly straightforward "
    "counterarguments. Representatives characteristically misrepresented institutionalised responsibilities, "
    "simultaneously acknowledging unprecedented telecommunications infrastructure vulnerabilities. Nevertheless, "
    "interdisciplinary collaboration demonstrated extraordinarily counterintuitive electromagnetic phenomena, "
    "revolutionising conventional thermodynamics interpretations throughout contemporary academia.",
    "Incompréhensiblement, l'administration départementale réorganisait perpétuellement ses responsabilités "
    "institutionnelles, désorganisant systématiquement l'approvisionnement intercommunal. Paradoxalement, "
    "l'internationalisation des télécommunications professionnalisait extraordinairement l'interdépendance "
    "constitutionnelle. Inconditionnellement, l'anticonstitutionnalité présumée démultipliait irrémédiablement "
    "l'incompréhension gouvernementale, particulièrement concernant l'électrification transfrontalière.",
]

ch1 = xhtml("Ch1: English", "en", "<h1>Ch 1: English Prose</h1>\n" + paragraphs(EN))
ch2 = xhtml("Ch2: Français", "fr", f"<h1>Ch 2{NNBSP}: Prose française</h1>\n" + paragraphs(FR))
ch3 = xhtml("Ch3: Long words", "en", "<h1>Ch 3: Long Words</h1>\n" + paragraphs(LONG))

CHAPTERS = [
    ("ch1", "chapter1.xhtml", "Chapter 1: English prose", ch1),
    ("ch2", "chapter2.xhtml", "Chapter 2: Prose française", ch2),
    ("ch3", "chapter3.xhtml", "Chapter 3: Long words", ch3),
]


def build_epub(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as epub:
        # mimetype must be first and uncompressed
        epub.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)

        epub.writestr("META-INF/container.xml", """\
<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf"
              media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>""")

        epub.writestr("OEBPS/styles/test.css", CSS)

        manifest_items = []
        spine_items = []
        nav_items = []

        for (chid, chfile, chtitle, chcontent) in CHAPTERS:
            epub.writestr(f"OEBPS/{chfile}", chcontent)
            manifest_items.append(
                f'    <item id="{chid}" href="{chfile}" media-type="application/xhtml+xml"/>')
            spine_items.append(f'    <itemref idref="{chid}"/>')
            nav_items.append(f'      <li><a href="{chfile}">{chtitle}</a></li>')

        manifest_items.append(
            '    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>')
        manifest_items.append('    <item id="css" href="styles/test.css" media-type="text/css"/>')

        content_opf = f"""\
<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">test-epub-justify-alignment</dc:identifier>
    <dc:title>Test: Justify Alignment</dc:title>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
{chr(10).join(manifest_items)}
  </manifest>
  <spine>
{chr(10).join(spine_items)}
  </spine>
</package>"""
        epub.writestr("OEBPS/content.opf", content_opf)

        nav_xhtml = f"""\
<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Table of Contents</title></head>
<body>
  <nav epub:type="toc">
    <ol>
{chr(10).join(nav_items)}
    </ol>
  </nav>
</body>
</html>"""
        epub.writestr("OEBPS/nav.xhtml", nav_xhtml)


if __name__ == "__main__":
    build_epub(str(OUTPUT_PATH))
    print(f"Wrote {OUTPUT_PATH}")
