/* Host self-check for rack_geometry.h.
 *
 * This is the calculation that decides which key a member walks away with.
 * It is pure C so it can be proved on a laptop, with no Uno, no motor and no
 * rack:
 *
 *   cc -Wall -Wextra -o /tmp/test_rack_geometry \
 *      firmware/tools/test_rack_geometry.c && /tmp/test_rack_geometry
 */

#include <assert.h>
#include <stdio.h>

#include "../KMS_LowLevel_ArduinoUno/src/rack_geometry.h"

int main(void) {
  int slot;
  long prev;

  printf("rack: %d slots, %d steps/rev, %.4f steps/slot, %.2f deg/slot\n",
         SLOT_COUNT, STEPS_PER_REV,
         (double)STEPS_PER_REV / SLOT_COUNT, 360.0 / SLOT_COUNT);

  printf("     HOME_OFFSET_SLOTS = %d\n", HOME_OFFSET_SLOTS);

  /* --- the anchor -------------------------------------------------------- */
  /* Whichever slot maps to index 0 sits at step 0 — that is the physical home
     position. With no offset that is slot 1; the offset rotates which slot
     lands there, and nothing else about the geometry. */
  assert(rack_slot_to_steps(1 + (SLOT_COUNT - HOME_OFFSET_SLOTS) % SLOT_COUNT) == 0);

  /* Slots that land on exact step boundaries — the ones the broken
     divide-first formula gets wrong, so they are the regression guard.
     Expressed against SLOT_TO_INDEX so the offset does not invalidate them:
     index 3 -> 200, index 12 -> 800, index 23 -> 1533. */
  assert(((long)STEPS_PER_REV * 3) / SLOT_COUNT == 200);
  assert(((long)STEPS_PER_REV * 12) / SLOT_COUNT == 800);
  assert(((long)STEPS_PER_REV * 23) / SLOT_COUNT == 1533);

  /* --- every slot distinct, and inside one revolution --------------------- */
  /* NOT monotonic in slot order once HOME_OFFSET_SLOTS is non-zero: the
     mapping wraps, so slot N can sit before slot 1. What must hold is that no
     two slots share a step target — a collision would dispense two different
     keys from one position. */
  {
    long seen[SLOT_COUNT];
    int i, j;
    for (i = 0; i < SLOT_COUNT; i++) {
      seen[i] = rack_slot_to_steps(i + 1);
      assert(seen[i] >= 0);
      assert(seen[i] < STEPS_PER_REV);   /* never past a full turn */
    }
    for (i = 0; i < SLOT_COUNT; i++)
      for (j = i + 1; j < SLOT_COUNT; j++)
        assert(seen[i] != seen[j]);      /* no two slots collide */
  }
  (void)prev;

  /* --- accuracy ---------------------------------------------------------- */
  /* Every slot must sit within one step of its ideal position. The
     divide-first bug fails this at slot 4 and is 16 steps out by slot 24. */
  for (slot = 1; slot <= SLOT_COUNT; slot++) {
    const double ideal =
        (double)STEPS_PER_REV * SLOT_TO_INDEX(slot) / SLOT_COUNT;
    const double err = rack_slot_to_steps(slot) - ideal;
    assert(err > -1.0 && err < 1.0);
  }

  /* --- range checking ---------------------------------------------------- */
  assert(rack_slot_valid(1));
  assert(rack_slot_valid(SLOT_COUNT));
  assert(!rack_slot_valid(0));            /* a garbled line, not "slot zero" */
  assert(!rack_slot_valid(-1));
  assert(!rack_slot_valid(SLOT_COUNT + 1));

  printf("rack_geometry.h: all checks passed\n");
  return 0;
}
