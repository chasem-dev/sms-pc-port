"""Check the reviewed layout names against source consumers and the owner's GMSE01 disc."""
import argparse
from pathlib import Path
import re
import struct
import sys

from extract_icon import Disc, be32, yaz0

ROOT = Path(__file__).resolve().parents[1]


def archive_layouts(disc):
    boot = disc.read(0, 0x440)
    if boot[:6] != b'GMSE01':
        raise ValueError('expected GMSE01')
    fst = disc.read(be32(boot, 0x424), be32(boot, 0x428))
    count = be32(fst, 8)
    names = set()
    for i in range(1, count):
        kind, off, length = struct.unpack_from('>III', fst, 12*i)
        if kind >> 24 or length < 16:
            continue
        start = count*12 + kind
        name = fst[start:fst.index(0,start)].decode('latin1').lower()
        if name.endswith('.blo'):
            names.add(name)
        head = disc.read(off, 16)
        if head[:4] not in (b'RARC', b'Yaz0'):
            continue
        # Only decompress the directory, not the assets after it.
        data = disc.read(off, length)
        header = yaz0(data, 32)
        if header[:4] != b'RARC':
            continue
        directory = yaz0(data, 32 + be32(header, 12))
        entries, strings = 32 + be32(directory, 44), 32 + be32(directory, 52)
        for j in range(be32(directory, 40)):
            start = strings + struct.unpack_from('>H', directory, entries + 20*j + 6)[0]
            name = directory[start:directory.index(0,start)].decode('latin1').lower()
            if name.endswith('.blo'):
                names.add(name)
    return names


def source_layouts():
    names = set()
    for folder in ('decomp/src', 'decomp-patches', 'src', 'ext', 'platform'):
        for path in (ROOT/folder).rglob('*'):
            if path.suffix in ('.c', '.cpp', '.h', '.hpp', '.patch'):
                names.update(re.findall(r'([\w-]+\.blo)\b', path.read_text(errors='replace')))
    # Native menus have no BLO file. Include each menu draw owner and saved
    # settings page so a new one cannot inherit a class by accident.
    for path in (ROOT/'src').glob('*.cpp'):
        names.update(re.findall(r'\bvoid\s+(\w+MenuDraw|PauseTrackerDraw|RadarDraw|drawFullMap)\(',
                                path.read_text(errors='replace')))
    header = (ROOT/'src/port_include/port_settings.h').read_text()
    names.update(re.findall(r'^\s*(PORT_SETTINGS_PAGE_(?!COUNT)\w+),?$', header, re.M))
    return names


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--disc', type=Path)
    args = parser.parse_args()
    names = source_layouts()
    if args.disc:
        names |= archive_layouts(Disc(args.disc))
    valid_classes = {'gameplay HUD', 'menu', 'content', 'world-attached', 'full-screen', 'split-screen'}
    covered = set()
    for line in (ROOT / 'docs/SCREEN-LAYOUTS.md').read_text().splitlines():
        if line.startswith('| `'):
            _, resource, kind, rule, _ = line.split('|')
            name = resource.strip(' `')
            if kind.strip() not in valid_classes or not rule.strip() or name in covered:
                sys.exit(f'Invalid or duplicate screen class: {line}')
            covered.add(name)
    missing = sorted(names - covered)
    if missing:
        sys.exit('Unclassified layout(s): ' + ', '.join(missing))
    print(f'{len(names)} layouts covered')
