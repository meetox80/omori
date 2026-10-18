import json
import pathlib
import re

from . import crypto


class GameError(Exception):
    pass


class Game:
    def __init__(self, path, key=None):
        root = self._find_root(pathlib.Path(path).expanduser().resolve())
        if root is None:
            raise GameError(f"could not find the game in {path} (expected a folder containing www/js/rpg_managers.js)")
        self.root = root
        self.key = (key or self._find_key()).encode("ascii")
        if len(self.key) != 32:
            raise GameError("the decryption key must be 32 characters")
        self._mv_key = None

    @staticmethod
    def _is_root(p):
        return (p / "js" / "rpg_managers.js").is_file() and (p / "data").is_dir() and (p / "maps").is_dir()

    @classmethod
    def _find_root(cls, start):
        if not start.is_dir():
            return None
        for cand in (start, start / "www"):
            if cls._is_root(cand):
                return cand
        for c in sorted(start.rglob("rpg_managers.js")):
            cand = c.parent.parent
            if cls._is_root(cand):
                return cand
        return None

    def _find_key(self):
        for cand in (self.root.parent / "Launch_OMORI.bat", self.root / "Launch_OMORI.bat"):
            if cand.is_file():
                m = re.search(r"--([0-9a-f]{32})", cand.read_text(errors="replace"))
                if m:
                    return m.group(1)
        raise GameError("could not find the decryption key (Launch_OMORI.bat); pass it explicitly")

    def read_raw(self, rel):
        return (self.root / rel).read_bytes()

    def read(self, rel):
        raw = self.read_raw(rel)
        if rel.upper().endswith(crypto.AES_EXTS):
            return crypto.aes_decrypt(raw, self.key)
        if rel.endswith((".rpgmvp", ".rpgmvo", ".rpgmvm")):
            return crypto.mv_decrypt(raw, self.mv_key)
        return raw

    @property
    def mv_key(self):
        if self._mv_key is None:
            self._mv_key = self.json("data/System.KEL")["encryptionKey"]
        return self._mv_key

    def json(self, rel):
        return json.loads(self.read(rel).decode("utf-8-sig"))

    def files(self, folder):
        base = self.root / folder
        return sorted(str(p.relative_to(self.root)) for p in base.rglob("*") if p.is_file())

    def plugins(self):
        text = (self.root / "js" / "plugins.js").read_text(encoding="utf-8", errors="replace")
        start = text.index("[")
        end = text.rindex("]")
        body = re.sub(r",\s*\]$", "]", text[start : end + 1])
        return json.loads(body)
