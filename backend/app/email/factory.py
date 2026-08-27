"""Email provider factory — returns the correct provider based on EMAIL_PROVIDER env."""
from functools import lru_cache

from app.core.config import get_settings
from app.email.base import EmailProvider

settings = get_settings()


@lru_cache
def get_email_provider() -> EmailProvider:
    provider = settings.email_provider.lower()
    if provider == "smtp":
        from app.email.smtp_provider import SMTPProvider
        return SMTPProvider()
    if provider == "console":
        from app.email.console_provider import ConsoleProvider
        return ConsoleProvider()
    # Future: elif provider == "sendgrid": ...
    raise ValueError(f"Unknown EMAIL_PROVIDER: {provider!r}")
