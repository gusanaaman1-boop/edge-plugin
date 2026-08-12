# EDGE — design innovation prompt (v0.14)

Paste everything below the line into the review chat. Attach the five
screenshots from `outputs/ui/`.

---

## Where this stands

Everything structural is done and verified in Cubase 15: light titanium shell
over a dark instrument, fixed centred inspector, five musical slopes, a
pre-filter analyzer on its own scale, EDGE PATH with a live puck, semantic
labels, 227 automated checks green. The owner's verdict:

> *"Works great. But the design looks a bit mediocre — not innovative."*

He is right, and it is a different problem from every previous round. Nothing
is broken, nothing collides, nothing lies. It is **correct and forgettable**.
Philta's hierarchy got us to "professional"; it cannot get us to "this could
only be EDGE". That is this round's job.

**This is not a rebuild request.** The layout, palette values, inspector
behaviour and all interaction contracts stay. The question is what to ADD or
SHARPEN so the product has a signature — the thing a producer screenshots and
posts.

## The diagnosis I want you to react to

Three reasons it reads as generic, in my view. Agree or correct with reasons:

1. **The hero has no stage.** The product's whole idea is *travel* — a
   destination you draw and a journey the sound makes. But the EDGE PATH is a
   thin rail with a dot, visually weaker than the grid. The one thing no other
   filter has is the least visible thing on screen.
2. **Nothing breathes.** FOLLOW is the second half of the identity — the
   signal pushing the filter — and its only trace is a violet tick on a knob
   arc. The engine already publishes the envelope at 60 Hz; the UI spends it
   nowhere. A static screenshot of EDGE and a static screenshot of any EQ look
   like the same category of object.
3. **Everything is a rectangle with radius 8 or 16.** Header strip, graph
   card, deck card, inspector card, segment buttons. Disciplined — and
   anonymous. There is no single shape, angle or construction that belongs to
   EDGE.

## Raw material the engine already provides

Design with these; they cost nothing and need no DSP work:

* `liveEdge01` — EDGE's resolved position after FOLLOW, per frame
* `followEnv01` — the envelope itself, per frame
* live + target corner frequencies per edge (the puck/diamond already ride them)
* `colourEngage01` — how hard the hidden saturation is actually working
* per-channel L/R resolved shapes (the SPREAD traces)
* the 96-band pre-filter spectrum
* a display pipeline that honestly animates at 60 Hz during interaction

Hard rules that stay: violet = movement only; amber = low edge; cyan = high
edge; no invented motion the engine is not producing; no LFO/sequencer/
randomiser; JUCE code only; 24 parameter IDs frozen.

## Directions to evaluate — commit to numbers, or kill them with reasons

**A. Make the journey the hero.** The EDGE PATH becomes the product's
signature object: wider rail, the travelled portion glowing, the puck leaving
a short decaying trail when FOLLOW moves it (honest: driven only by actual
positions the engine occupied). At EDGE 0 the path alone shows what WILL
happen — the plug-in advertises its idea while idle.

**B. Let the signal be visible pushing the filter.** The puck and the EDGE
knob's outer marker already move with `followEnv01`; give that movement
presence — a violet energy treatment around the puck proportional to the
envelope, the FOLLOW→EDGE knob's arc filling live. When the kick hits, the
interface should visibly *push*. This is the ten-second demo.

**C. One signature construction.** Choose a single geometric motif — an angled
cut, a beveled corner, a characteristic slope — derived from the response
curve itself, and apply it in exactly three places (wordmark, deck card,
inspector). Everything else stays rectangular. One motif, not a theme park.

**D. The wordmark is a placeholder.** "E D G E" in tracked-out system bold is
the most generic possible mark. Design a real one in JUCE-drawable geometry —
strokes and paths, no font licence — ideally one that quotes the shelf→cut
curve.

**E. State-reactive chrome (evaluate honestly).** The shell is static
titanium. Options: none (dignity), or minimal — e.g. the header's hairline
under-glow taking the active mode's colour. If you keep it static, say so and
why; if it reacts, one element only.

For each direction: keep/kill, and if keep — the exact drawing spec (sizes,
opacities, decay times, colour roles) plus an acceptance number the existing
screenshot/pixel-probe rig can assert.

## Constraints on motion

* Everything animated must be driven by real engine state (`followEnv01`,
  `liveEdge01`, `colourEngage01`, audio levels). Decorative idle animation is
  forbidden.
* 60 Hz interaction / 30 Hz analyzer clocks stay.
* Trails/decays must be display-side only, zero audio-thread cost.
* A user with no audio playing sees a calm, static, premium instrument.

## The answer format

```
DIRECTION <letter> — KEEP or KILL

  RATIONALE   two sentences max
  SPEC        the drawing spec, with numbers
  ACCEPTANCE  something the pixel-probe/screenshot rig can assert
  EFFORT      S / M / L
```

Then: the one thing you would do FIRST, and the mock (described precisely
enough to implement without a picture) of how the graph looks at EDGE 55 %,
FOLLOW active, on a techno loop — the shot that goes on the product page.
