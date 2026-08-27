"""Widen the key slot range from 8 to 24

The rack is a rotating carousel driven by a single stepper, not a bank of
per-slot solenoids, so its capacity is a mechanical property rather than a
GPIO count. It carries 24 keys.

The old CHECK rejected slot_number > 8 outright, which is a hard IntegrityError
on insert rather than a silent wrong value — good, but it means seeding a real
rack fails until this runs.

Postgres cannot alter a CHECK in place; it has to be dropped and recreated.
The downgrade will fail if any row is already above 8, which is correct: it
refuses rather than quietly deleting keys.

Revision ID: widen_slot_range_to_24
Revises: add_script_executions
"""
from alembic import op

revision = "widen_slot_range_to_24"
down_revision = "add_script_executions"
branch_labels = None
depends_on = None

CONSTRAINT = "slot_number_range"
TABLE = "key_slots"


def upgrade() -> None:
    op.drop_constraint(CONSTRAINT, TABLE, type_="check")
    op.create_check_constraint(CONSTRAINT, TABLE, "slot_number BETWEEN 1 AND 24")


def downgrade() -> None:
    op.drop_constraint(CONSTRAINT, TABLE, type_="check")
    # Fails loudly if slots 9-24 are populated. Dropping those rows to fit the
    # old bound would delete live key assignments and their retrieval history.
    op.create_check_constraint(CONSTRAINT, TABLE, "slot_number BETWEEN 1 AND 8")
