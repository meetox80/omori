from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

MV_HEADER = bytes.fromhex("5250474d560000000003010000000000")
AES_EXTS = (".OMORI", ".KEL", ".AUBREY", ".HERO", ".PLUTO")


def aes_decrypt(buf, key):
    iv, body = buf[:16], buf[16:]
    dec = Cipher(algorithms.AES(key), modes.CTR(iv)).decryptor()
    return dec.update(body) + dec.finalize()


def mv_decrypt(buf, key_hex):
    if buf[:16] != MV_HEADER:
        return bytes(buf)
    key = bytes.fromhex(key_hex)
    body = bytearray(buf[16:])
    for i in range(min(16, len(body))):
        body[i] ^= key[i]
    return bytes(body)
