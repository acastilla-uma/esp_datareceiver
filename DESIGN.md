# Design

## Source of truth
- Status: Active
- Last refreshed: 2026-09-30
- Primary product surfaces: Live telemetry, RTK/GNSS health, and historical CSV playback in the local dashboard.
- Evidence reviewed: `windows_dashboard/static/index.html`, `app.js`, `app.css`, `server.py`.

## Brand
- Personality: Technical, calm, operational.
- Trust signals: Explicit units, source mode, sample position and timestamp.
- Avoid: Decorative charts that hide current values or silently mix live and historical data.

## Product goals
- Goals: Show live stability, expose RTK precision/connectivity, and inspect a CSV sample-by-sample with the same calculations.
- Non-goals: Editing the source CSV or uploading it to a remote service.
- Success signals: A user can load a CSV, move backward/forward in time, and see every dashboard value update.

## Personas and jobs
- Primary personas: Researcher or operator validating vehicle stability measurements.
- User jobs: Compare sensor values, RTK quality, and the stability equation at a precise recorded sample.
- Key contexts of use: Local Windows browser, desktop first, occasional narrow viewport.

## Information architecture
- Primary navigation: `En directo` and `Reproducir CSV` tabs.
- Core views: Live dashboard, historical playback controls, shared telemetry/equation surface.
- Content hierarchy: Mode/status -> sample controls -> sensor values -> RTK/GNSS state -> stability terms -> supporting data.

## Design principles
- Principle 1: Never mix live and historical samples without a visible mode indicator.
- Principle 2: Reuse the live cards and units for historical playback, including the RTK state reconstructed from CSV.
- Tradeoffs: Browser-local parsing keeps user data private but does not persist the file after reload.

## Visual language
- Color: Existing teal live state, blue static term, violet dynamic term.
- Typography: Existing Segoe UI with Cambria for equations.
- Spacing/layout rhythm: Existing 14px grid and responsive one-column fallback.
- Shape/radius/elevation: Existing rounded panels and restrained shadows.
- Motion: No required animation; playback controls update immediately.
- Imagery/iconography: Text and native controls only.

## Components
- Existing components to reuse: Sensor cards, stability equation, `dl` metric lists, panel tokens.
- New/changed components: Mode tabs, CSV picker, sample slider, previous/next/playback controls, historical status, structured GNSS/RTK panel.
- Variants and states: Empty, loaded, invalid CSV, first/last sample, playing, live, RTK fixed, RTK float, autonomous, stale, disconnected.
- Token/component ownership: `windows_dashboard/static/app.css`.

## Accessibility
- Target standard: Semantic HTML and keyboard-operable controls.
- Keyboard/focus behavior: Buttons, file picker and range slider are native controls.
- Contrast/readability: Reuse existing color tokens and visible labels.
- Screen-reader semantics: Tab buttons expose selected state; sample status is live text; GNSS sections use labelled headings and text status.
- Reduced motion and sensory considerations: No essential motion or sound.

## Responsive behavior
- Supported breakpoints/devices: Existing desktop and mobile breakpoints.
- Layout adaptations: Playback toolbar wraps; sample slider remains full width.
- Touch/hover differences: Native range and buttons remain usable on touch.

## Interaction states
- Loading: File parsing status is shown beside the picker.
- Empty: Prompt to select a CSV.
- Error: Human-readable delimiter/header/empty-file error.
- Success: Filename, row count and timestamp are shown.
- Disabled: Navigation disables at first/last sample; play disables when empty.
- Offline/slow network, if applicable: Historical playback does not require network.

## Content voice
- Tone: Concise Spanish operational labels.
- Terminology: `Muestra`, `Anterior`, `Siguiente`, `En directo`, `Reproducir CSV`.
- Microcopy rules: Always show units and whether values are live or historical; distinguish RTK solution quality from USB/NTRIP connectivity.

## Implementation constraints
- Framework/styling system: Vanilla HTML/CSS/JavaScript; no new dependencies.
- Design-token constraints: Extend existing `app.css` tokens.
- Performance constraints: Parse locally and process one selected file at a time.
- Compatibility constraints: Windows browsers supported by the existing dashboard.
- Test/screenshot expectations: JavaScript syntax check, Node tests for CSV/GNSS reconstruction, server tests, and visual smoke test with a CSV row.

## Open questions
- [ ] Should historical playback eventually support synchronized charts? Out of scope for this iteration.
