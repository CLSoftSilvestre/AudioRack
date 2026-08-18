# ADR 0003 — ProcessContext, choice parameters, dynamics design choices

Date: 2026-08-18 · Status: accepted

## 1. `process(block, ProcessContext&)` replaces `process(block, TransportInfo&)`

The brief's module contract passed only transport to `process()`. The
compressor and gate need the external sidechain bus audio, which is engine
infrastructure, not per-module plumbing. Rather than grow the signature again
later, modules now receive one `ProcessContext` carrying the transport
snapshot plus read-only sidechain channel pointers (null when the bus is
disabled). One breaking change, made before any module shipped; future needs
(e.g. MIDI for M7's MIDI learn) extend the struct, not the signature.

## 2. `ParamSpec.choices` → `AudioParameterChoice`

Discrete module switches (FF/FB, Peak/RMS, Int/Ext…) are declared as choice
specs and become `juce::AudioParameterChoice` in the APVTS, so hosts show
"Feed-Forward / Feed-Back" instead of 0/1. Raw-value semantics for the DSP
are unchanged (atomic float index, ≥ 0.5 test for two-state switches).

## 3. Compressor: Giannoulis/Massberg/Reiss topology

dB-domain gain computer with quadratic soft knee and the paper's *smooth
decoupled peak detector* (release stage inside an attack one-pole). Chosen
over branching-per-sample designs because attack/release semantics match the
published analysis, which is what the timing tests assert against.
Program-dependent release scales τ_release by a 1 s GR average
(0.25×…4×) — an opto-style behaviour that is simple, bounded and testable.
Auto-makeup is `−G(0)/2`; full compensation (`−G(0)`) pumps perceived
loudness far too hard at high ratios.

## 4. Limiter: sliding-min + bounded smoothing, detection-only oversampling

The no-overshoot guarantee comes from structure (min window ≥ smoothing
support), not from tuning. Oversampling (JUCE half-band FIR, 4×, integer
latency) runs only on the mono detector, so the programme path stays
bit-transparent below the ceiling — verified by a null test — while the
detector still sees inter-sample peaks. Full-path oversampling would smear
the programme through two FIR passes for no audible benefit at these gains.
A final hard clamp at the ceiling stays in as a guard; tests confirm it is
not what does the limiting.

## 5. Move-semantics consequence for dynamics

Reordering (ADR 0002) remounts fresh instances: a moved limiter/gate loses
its delay-line contents, which is momentarily audible under signal. Accepted
for v1 — reordering audibly changes the chain anyway.
