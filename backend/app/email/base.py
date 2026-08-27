"""Abstract email provider interface."""
from abc import ABC, abstractmethod
from dataclasses import dataclass


@dataclass
class EmailMessage:
    to: str
    subject: str
    html_body: str
    text_body: str = ""


class EmailProvider(ABC):
    """Provider-pattern base — same interface regardless of backend (SMTP, SendGrid, SES, Resend)."""

    @abstractmethod
    async def send(self, message: EmailMessage) -> None: ...
