# Cognitive Coach AI — a Rummikub agent that finds the move and refuses to play it

*Chinese version: [06_認知教練型AI_設計說明.md](06_認知教練型AI_設計說明.md)*

---

## The claim

A competitive Rummikub agent wins by playing the best move. This one solves the board
and then does not play it. It is built to teach, not to beat you.

That inversion is the whole project. The engine is the same scanner the competitive
agent uses — the difference is what it does with the answer once it has one.

The design question underneath: **when should a system say less?**

---

## Guidance decay, as two testable parameters

"Guidance fades as the learner improves" is a slogan. Slogans cannot be tested.
So it is split into two numbers per level: **how many stuck turns before the coach speaks**,
and **the deepest tier it is allowed to reach**.

| Level | Technique | Guidance | Max tier | To clear |
|---|---|---|---|---|
| 1 | Attach to a run | 100% | REVEAL | 3 uses, 1 unaided |
| 2 | Complete a group | 88% | REVEAL | 3 uses, 1 unaided |
| 3 | Joker fills a gap | 76% | REVEAL | 3 uses, 1 unaided |
| 4 | Initial meld of 30 | 64% | POINT | 3 uses, 2 unaided |
| 5 | Board reshuffle | 52% | POINT | 3 uses, 2 unaided |
| 6 | Run split | 40% | NUDGE only | 3 uses, all unaided |

Three tiers of hint: **NUDGE** (there is something here), **POINT** (look over there),
**REVEAL** (play this tile). The resulting curve, by stuck turn, where a dot is silence:

```
stuck turn ->   0  1  2  3  4  5  6
L1            N  P  R  R  R  R  R
L2            .  N  P  P  R  R  R
L3            .  N  N  P  P  R  R
L4            .  .  N  N  P  P  P
L5            .  .  N  N  N  P  P
L6            .  .  .  N  N  N  N
```

These 42 cells are hard-coded in the test suite as a golden table, not recomputed
from the formula. Recomputing only proves the code agrees with itself and passes
whatever the parameters become. The frozen table proves the code still agrees with
the decision that was made — so changing a threshold breaks it, which is the point.

**Mastery only rises.** One star for copying a revealed move, two for finding it from
a direction, three for finding it with no hint at all. A technique already discovered
is not demoted because the player later peeks at an answer. Learned is learned.

**Clearing a level counts unaided uses, not uses.** Three plays with the answer
revealed every time is not learning. Level 6 requires all three unaided, because what
that level teaches is not the technique — it is doing it without being told.

---

## Technique detection: miss rather than misfire

Players do not announce what they just did. The only reliable evidence is the board:
what it looked like before the move, after the move, and which tiles left the hand.

Every rule is deliberately conservative. A missed detection costs one unrecorded use —
the player does it again and the progress comes back. A false detection clears a level
the player has not actually learned, which means the system lied to them.

So the test suite spends most of its effort not on the six positive cases but on the
**near misses**, each of which must return nothing: opening a fresh set (not an attach),
a joker placed at either end of a run (extension, not gap-filling), a five-tile run
broken up (below the split threshold), a long run absorbed into a longer one
(extension, not a split).

---

## Tests: four suites, 1,475 checks

| Suite | Checks | What it guards |
|---|---|---|
| test_coach_campaign.cpp | 1,322 | The 42-cell curve, the max-tier ceiling, mastery never falling, clearing thresholds, quiz integrity |
| test_technique_detector.cpp | 62 | Six positive cases, plus six near misses that must not fire |
| test_cognitive_hint_engine.cpp | 51 | Three honesty invariants, cross-checked over 3,000 random boards |
| test_validator.cpp | 40 | The existing rule validation |

The three honesty invariants are the core promise of the product:

1. When it says a move exists, a move must exist.
2. When it says nothing connects, nothing in scope must connect.
3. All three tiers must agree on whether a move exists.

The third is the worst failure available to a coach: encouraging at the nudge tier and
then admitting defeat at the reveal tier. It is checked across 3,000 randomly generated
legal boards, with the reference answer produced entirely by Validator — the candidate
tile is actually attached and the validator is asked whether the result is legal.
Re-implementing the engine arithmetic as the reference would only prove the code agrees
with itself. Result: zero false claims, zero missed moves, zero tier disagreements.

---

## Three things the coach said that were not true

None of these crash. None raise an error. They only make the system tell a player
something false.

**Duplicate numbers merged into a run.** Rummikub has two copies of every tile, so a
hand holding the same colour and number twice is ordinary. The scan computed
gap = 5 - 5 - 1 = -1, found it not greater than zero, appended the second red 5, and
reported a three-tile run:

> Try laying down red 5, red 5 and red 6 as a run — 16 points

Validator rejects that arrangement outright. A beginner who follows the advice is
refused by the engine and concludes they misunderstood the rules.

**A 36-point opening reported as impossible.** Jokers were only used to fill internal
gaps, never to extend an end. A hand of red 11, red 12 and a joker is red 11-12-13 —
36 points, comfortably over the 30 needed to open. The engine told the player to draw.

Under-reporting one option is acceptable caution. Telling a player to abandon a valid
36-point opening is not caution; it is a false statement. Extending at the ends is a
constant-time check, so the simplification saved almost nothing and cost a great deal.

**A quiz question with two correct answers.** Level 3, question 5 asks whether a joker
can attach to blue 11-12-13. Both "no, 13 is the maximum" and "yes, in front as blue 10"
were flagged correct. They are contradictory answers, not two phrasings of one judgement;
the second is right. Since the grader accepts any option flagged correct, a player
choosing the wrong one was told they were right and carried the misunderstanding forward.

That one surfaced from an assertion that every question has exactly one correct option.
No compiler catches wrong content. Only a test does.

---

## Wiring: the difference between existing and taking effect

This is the step that is easiest to skip and mattered most.

Before it, the agent asked tierFromStuckTurns() — one flat three-step threshold,
identical at every level, and one that **always returns a tier**. There was no option
to stay quiet.

Which meant that in an actual game, none of the design above existed. The six-level
curve did nothing. The max-tier ceiling did nothing. The promise that level 6 never
reveals did nothing. And staying quiet — the central claim of the whole project —
had never once happened.

What the wiring changed:

- The tier now comes from campaign.shouldGiveHint(). False means print nothing.
- **The front-end mailbox is cleared too.** Otherwise the browser keeps displaying the
  previous turn hint and the silence is invisible. Silence has to happen where the user
  can see it, not only inside the control flow.
- The board is snapshotted before and after the move and handed to the detector, which
  records progress and level completion automatically.

**What counts as having seen a hint**: the deepest tier shown since the last successful
play. A player stuck for five turns who saw the answer on turn four and moved on turn
five played that move with the answer in hand. A hint does not expire because a turn
passed.

tools/coach_demo.cpp verifies the wiring by calling the same two lines the agent calls,
without waiting on input, and printing every cell. If shouldGiveHint were not connected,
all six rows would come out identical.

The same solvable position, on stuck turn 6:

```
Level 1: Try attaching your red 3 to the front of that red run on the table.
Level 6: There might be something playable in your hand — take another look at the table.
```

Ask the reveal tier directly and it still names red 3 and the front of the run.
**The silence at level 6 is a decision, not a limit.**

---

## The recap protocol

state.json gains a recap field: null in normal play, and during a recap an object
carrying the level, question index, prompt, options and feedback. The player replies
with action=answer and choice=N.

A first wrong answer returns the hint; a second returns the explanation — but the player
**still has to select the right option to move on**. Skipping ahead after the explanation
was deliberately not implemented: it would let someone click through five explanations
and turn the recap into an answer key.

Off by default, enabled with COACH_RECAP=1, because the current front end does not know
the protocol and would otherwise stall the game waiting for an answer that never arrives.
A timeout (180 seconds by default) is the second line of defence.

---

## Limits, stated plainly

**One of five modes is built.** The plan is: beginner drill, signature moves (players
name their own techniques), head-to-head, endurance, and open matchmaking. Only the
beginner drill exists. The other four have no files yet.

**The front end does not display recaps.** Protocol and backend are done; the browser
side is not, so interactive answering currently only works in the terminal.

**Stuck detection watches the whole table**, not this player specifically. In a
two-player game the two are equivalent; a four-player game would need different logic.

**Detection under-reports by design.** That is the price of refusing to misfire.

**The thresholds are design values, not experimental results.** Two stuck turns before
pointing, guidance falling to 40 percent at level 6 — these come from judgement, not
from user testing. They are parameters that could be measured. They have not been.

**No trial with real learners has been run.** Every claim above about this teaching
better than a louder coach remains a design argument, not a finding.

---

## Human-AI collaboration on this round

Following the disclosure practice in the main README:

**Designed and written by the author**: the six levels and their teaching order, every
threshold in the decay curve, the three mastery grades and the rule that they never
fall, the unaided-use requirement for clearing a level, all 30 recap questions, the
detection rules and the miss-rather-than-misfire principle, and the coaching claim
itself.

**Assisted by AI (Claude)**: writing the four test suites, identifying the three false
statements above during testing and proposing the fixes, two corrections inside
findBestMeldCandidate, wiring the existing modules into the agent, tools/coach_demo.cpp,
and drafting this document.

Two of the three false statements were forced out by writing tests. That is worth
recording on its own: the value of a test suite is not proving the code right, it is
surfacing the places where you were sure and wrong.

---

## Build and run

```
# The demo that prints the decay table
g++ -std=c++17 -I src src/tile.cpp src/validator.cpp src/cognitive_hint_engine.cpp src/coach_campaign.cpp tools/coach_demo.cpp -o coach_demo
./coach_demo

# The four test suites
g++ -std=c++17 -I src src/coach_campaign.cpp tests/test_coach_campaign.cpp -o t1 && ./t1
g++ -std=c++17 -I src src/tile.cpp src/validator.cpp src/cognitive_hint_engine.cpp tests/test_cognitive_hint_engine.cpp -o t2 && ./t2
g++ -std=c++17 -I src src/tile.cpp src/validator.cpp src/coach_campaign.cpp src/technique_detector.cpp tests/test_technique_detector.cpp -o t3 && ./t3
g++ -std=c++17 -I src src/tile.cpp src/validator.cpp tests/test_validator.cpp -o t4 && ./t4
```

---

*Yu-Shin Lan · Shin Min High School, Taichung, Taiwan*
