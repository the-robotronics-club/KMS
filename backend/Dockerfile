FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

RUN useradd --create-home --uid 1001 appuser && chown -R appuser:appuser /app
USER appuser

# Render injects PORT and routes to whatever it picks; compose doesn't set it.
ENV PORT=8000
EXPOSE 8000

# Migrations run on boot so a fresh deploy reaches a working schema without a
# manual shell step. Safe on one instance — Alembic takes a lock, and `upgrade
# head` is a no-op once current. Past one instance, move this to a release step.
#
# Shell form so $PORT expands. `exec` gives uvicorn PID 1 so SIGTERM reaches it
# and the MQTT listener closes cleanly instead of being killed.
CMD ["sh", "-c", "alembic upgrade head && exec uvicorn app.main:app --host 0.0.0.0 --port $PORT"]
