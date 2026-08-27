"""Application configuration — loaded from environment / .env file."""
from functools import lru_cache
from pydantic import Field, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    # ── Application ──────────────────────────────────────────────
    debug: bool = False
    allowed_origins_raw: str = Field(default="http://localhost:3000", validation_alias="allowed_origins")

    @property
    def allowed_origins(self) -> list[str]:
        return [x.strip() for x in self.allowed_origins_raw.split(",") if x.strip()]

    # ── Database ─────────────────────────────────────────────────
    database_url: str  # postgresql+asyncpg://user:pass@host:port/db

    # ── Redis ────────────────────────────────────────────────────
    redis_url: str  # redis://...  or rediss://...

    # ── MQTT ─────────────────────────────────────────────────────
    mqtt_host: str = "localhost"
    mqtt_port: int = 1883
    mqtt_username: str = ""
    mqtt_password: str = ""
    mqtt_tls: bool = False
    # Off by default so a web-only deploy doesn't start a listener that can never
    # connect — it would retry every 5s forever and bury real errors in the log.
    # Flip to true once a broker exists; no code change needed.
    mqtt_enabled: bool = False

    # ── Email ────────────────────────────────────────────────────
    email_provider: str = "smtp"  # smtp | sendgrid | ses | resend
    email_address: str = ""
    email_app_password: str = ""
    smtp_server: str = "smtp.gmail.com"
    smtp_port: int = 587

    # ── Auth ──────────────────────────────────────────────────────
    session_ttl_seconds: int = 3600          # 1 hour
    cookie_secure: bool = True
    # Must stay LONGER than the firmware's CODE_ROTATE_MS, otherwise a code can
    # expire in Redis before the cabinet mints its replacement and there is a
    # window where the code on the portal screen is already dead. 6 min TTL
    # against a 5 min rotation leaves the displayed code at least 60 s of life,
    # which is what the walk-outside-and-switch-networks handoff needs.
    proximity_code_ttl_seconds: int = 360    # 6 minutes
    proximity_flag_ttl_seconds: int = 300    # 5 minutes
    login_max_attempts: int = 5
    login_lockout_seconds: int = 900         # 15 minutes
    totp_max_attempts: int = 5
    totp_lockout_seconds: int = 300          # 5 minutes

    # Local demo only. Any non-empty value is accepted as a valid TOTP code for
    # every user, so leave it unset in any deployed environment.
    totp_demo_bypass_code: str = ""

    # Fernet key encrypting users.totp_secret at rest. Generate with:
    #   python -c "from cryptography.fernet import Fernet; print(Fernet.generate_key().decode())"
    # Losing it means every enrolled user has to re-enroll their authenticator.
    totp_encryption_key: str = ""

    # ── Notification schedule ─────────────────────────────────────
    reminder_before_due_minutes: int = 30
    escalation_after_due_hours: int = 2

    # ── Possession window ─────────────────────────────────────────
    default_possession_hours: int = 6

    # ── WebSocket ticket ──────────────────────────────────────────
    # A /ws/keys ticket is redeemed within a page load or not at all.
    ws_ticket_ttl_seconds: int = 30

    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8", extra="ignore")

    @model_validator(mode="after")
    def _reject_totp_bypass_outside_debug(self) -> "Settings":
        """Refuse to boot with the TOTP bypass armed in a non-debug environment.

        verify_totp() accepts this value as a valid code for every user, so a
        stray env var would disable the second factor for the whole system with
        nothing in the logs to show it. Fail loudly at startup instead of
        silently at the first login.
        """
        if self.totp_demo_bypass_code and not self.debug:
            raise ValueError(
                "TOTP_DEMO_BYPASS_CODE is set while DEBUG is false. This disables "
                "two-factor authentication for every user. Unset it, or set DEBUG=true "
                "if this really is a local demo."
            )
        return self

    @model_validator(mode="after")
    def _require_usable_totp_encryption_key(self) -> "Settings":
        """Refuse to boot without a valid key for the TOTP secret column.

        Defaulting to plaintext would be silent: enrollment would keep working
        and the secrets would sit readable in every database dump. Fail here
        instead, where the message can say what to do about it.
        """
        hint = (
            'Generate one with: python -c "from cryptography.fernet import Fernet; '
            'print(Fernet.generate_key().decode())"'
        )
        if not self.totp_encryption_key:
            raise ValueError(f"TOTP_ENCRYPTION_KEY is not set. {hint}")
        from cryptography.fernet import Fernet

        try:
            Fernet(self.totp_encryption_key.encode())
        except Exception as exc:
            raise ValueError(
                f"TOTP_ENCRYPTION_KEY is not a valid Fernet key ({exc}). {hint}"
            ) from exc
        return self


@lru_cache
def get_settings() -> Settings:
    return Settings()
