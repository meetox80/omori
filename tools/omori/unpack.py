import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

from omori.game import Game

EXT_MAP = {
    ".OMORI": ".js", ".KEL": ".json", ".AUBREY": ".json", ".HERO": ".yaml", ".PLUTO": ".yaml",
    ".rpgmvp": ".png", ".rpgmvo": ".ogg", ".rpgmvm": ".m4a",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game")
    ap.add_argument("out")
    ap.add_argument("--only", nargs="*", default=None)
    args = ap.parse_args()
    game = Game(args.game)
    out = pathlib.Path(args.out)
    folders = args.only or ["js", "data", "maps", "languages", "img", "audio", "fonts", "movies", "icon"]
    for folder in folders:
        for rel in game.files(folder):
            p = pathlib.Path(rel)
            dest = out / p.with_suffix(EXT_MAP.get(p.suffix, p.suffix))
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(game.read(rel))
    print("ok", game.root)


if __name__ == "__main__":
    main()
