"""Console email provider — prints instead of sending. Local/demo use only.

SMTP needs real Gmail app-password credentials. Without them aiosmtplib raises
mid-request and the caller 500s, which takes down key retrieve even though the
key itself dispensed fine. Set EMAIL_PROVIDER=console to keep those flows
working while email is still unconfigured.
"""
import logging

from app.email.base import EmailMessage, EmailProvider

logger = logging.getLogger(__name__)


class ConsoleProvider(EmailProvider):
    async def send(self, message: EmailMessage) -> None:
        logger.info("[email:console] to=%s subject=%s", message.to, message.subject)
