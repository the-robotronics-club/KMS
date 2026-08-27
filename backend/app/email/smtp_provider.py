"""SMTP email provider implementation using aiosmtplib."""
import aiosmtplib
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText

from app.core.config import get_settings
from app.email.base import EmailMessage, EmailProvider

settings = get_settings()


class SMTPProvider(EmailProvider):
    async def send(self, message: EmailMessage) -> None:
        msg = MIMEMultipart("alternative")
        msg["Subject"] = message.subject
        msg["From"] = settings.email_address
        msg["To"] = message.to

        if message.text_body:
            msg.attach(MIMEText(message.text_body, "plain"))
        msg.attach(MIMEText(message.html_body, "html"))

        await aiosmtplib.send(
            msg,
            hostname=settings.smtp_server,
            port=settings.smtp_port,
            username=settings.email_address,
            password=settings.email_app_password,
            start_tls=True,
        )
