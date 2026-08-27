.PHONY: dev down logs backend frontend test test-unit migrate seed migrate-new install-backend install-frontend setup

# The whole stack in containers. Nothing to install, nothing to configure —
# migrations and the demo seed run on boot. This is the recommended way in.
dev:
	docker compose up --build

down:
	docker compose down

logs:
	docker compose logs -f backend

# ── Running on the host instead (needs `make setup` first) ────────────────────

backend:
	cd backend && .venv/bin/uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

frontend:
	cd frontend && npm run dev

test:
	cd backend && .venv/bin/python -m pytest tests/ -v

test-unit:
	cd backend && .venv/bin/python -m pytest tests/test_unit.py -v

migrate:
	cd backend && .venv/bin/alembic upgrade head

seed:
	cd backend && .venv/bin/python seed.py

migrate-new:
	cd backend && .venv/bin/alembic revision --autogenerate -m "$(MSG)"

install-backend:
	cd backend && python3 -m venv .venv && .venv/bin/pip install -r requirements.txt -r requirements-dev.txt

install-frontend:
	cd frontend && npm install

setup: install-backend install-frontend
	cp -n backend/.env.example backend/.env || true
	cp -n frontend/.env.example frontend/.env.local || true
	@echo "Setup complete. Edit backend/.env with your credentials, then 'make backend' and 'make frontend'."
	@echo "Or skip all of this and run 'make dev' for the containerised stack."
