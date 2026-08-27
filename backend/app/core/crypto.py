"""Encryption at rest for TOTP secrets.

A TOTP secret is a bearer credential: whoever reads it can mint valid codes
forever, so a leaked database dump or a stray `SELECT` in a support session
hands over the whole second factor. Storing it as plaintext next to the
password hash means one dump defeats both factors at once.

Encryption happens in a SQLAlchemy TypeDecorator rather than at each call
site. Application code keeps reading and writing `user.totp_secret` as a
plain string, so there is no way to add a new write path that forgets to
encrypt — the column type is the only place that has to be right.
"""
from __future__ import annotations

from functools import lru_cache

from cryptography.fernet import Fernet, InvalidToken
from sqlalchemy import String
from sqlalchemy.types import TypeDecorator

from app.core.config import get_settings

# Every Fernet token begins with a 0x80 version byte and a big-endian
# timestamp, which base64-encodes to this fixed prefix. Base32 TOTP secrets
# use only A-Z and 2-7, so the two encodings can never be confused.
_FERNET_PREFIX = "gAAAAA"


@lru_cache
def _fernet() -> Fernet:
    return Fernet(get_settings().totp_encryption_key.encode())


def encrypt_secret(plain: str) -> str:
    return _fernet().encrypt(plain.encode()).decode()


def decrypt_secret(stored: str) -> str:
    """Decrypt a stored secret, passing through pre-encryption plaintext.

    Rows written before this column was encrypted hold a bare base32 secret.
    They are returned as-is and re-encrypted the next time they are written,
    so no backfill migration is needed.
    """
    if not stored.startswith(_FERNET_PREFIX):
        return stored
    try:
        return _fernet().decrypt(stored.encode()).decode()
    except InvalidToken as exc:
        # Silently returning the ciphertext here would surface as "Invalid TOTP
        # code" for every user and send whoever debugs it hunting through the
        # authenticator app. The cause is almost always a rotated or truncated
        # TOTP_ENCRYPTION_KEY.
        raise RuntimeError(
            "Stored TOTP secret could not be decrypted. TOTP_ENCRYPTION_KEY does "
            "not match the key the secret was encrypted with."
        ) from exc


class EncryptedSecret(TypeDecorator):
    """String column that is ciphertext in the database and plaintext in Python."""

    impl = String
    cache_ok = True

    def process_bind_param(self, value: str | None, dialect) -> str | None:
        return None if value is None else encrypt_secret(value)

    def process_result_value(self, value: str | None, dialect) -> str | None:
        return None if value is None else decrypt_secret(value)
