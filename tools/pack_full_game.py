"""Pack the allowlisted installer payload or the player download."""
import argparse
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo

root = Path(__file__).resolve().parents[1]
files = ['Install.ps1', 'Uninstall.ps1', '安装.cmd', '卸载.cmd', 'd3d9.dll',
         'package-manifest.json', 'mahoyo-learning/local/story.dat',
         'mahoyo-learning/local/provenance.json','mahoyo-learning/settings.ini',
         'mahoyo-learning/local/StudyBook.exe','mahoyo-learning/local/word-index.dat',
         'mahoyo-learning/local/NotoSerifSC-Medium.ttf','mahoyo-learning/local/NotoSerifSC-OFL.txt',
         'mahoyo-learning/local/shogakukan.zip','README.md','LICENSE','license-research.md','licenses/MinHook-LICENSE.txt',
         'licenses/NotoSerifSC-OFL.txt']


def write_zip(target, members):
    target.parent.mkdir(exist_ok=True)
    temp = target.with_suffix('.zip.tmp')
    with ZipFile(temp, 'w') as archive:
        for name, source in members:
            entry = ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            entry.create_system = 0
            archive.writestr(entry, source.read_bytes(), compress_type=ZIP_DEFLATED, compresslevel=8)
    with ZipFile(temp) as archive:
        if archive.testzip() is not None:
            raise ValueError('Archive CRC validation failed')
    temp.replace(target)
    print(target)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', action='store_true', help='wrap the compiled graphical installer for players')
    dist = root/'dist'
    if parser.parse_args().release:
        members = [('魔夜读本安装器.exe', dist/'魔夜读本安装器.exe'),
                   ('玩家指南.md', root/'docs/player-guide.md'),
                   ('可选词典/mahoyo-ja-zh-optional.zip', dist/'mahoyo-ja-zh-optional.zip')]
        for name in ['LICENSE', 'license-research.md', 'licenses/MinHook-LICENSE.txt', 'licenses/NotoSerifSC-OFL.txt']:
            members.append((name, root/name))
        write_zip(dist/'魔夜读本-Windows全游戏版.zip', members)
    else:
        write_zip(dist/'mahoyo-yomibito-full.zip',
                  [('mahoyo-yomibito/'+name, root/'package'/name) for name in files])
