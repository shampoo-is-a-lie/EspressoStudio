<script>
  import { createEventDispatcher } from 'svelte'

  export let value = 0
  export let min = 0
  export let max = 1
  export let label = ''
  export let resetValue = null
  export let format = (v) => v.toFixed(2)

  const dispatch = createEventDispatcher()
  const SWEEP = 270          // degrees of travel
  const R = 20               // svg arc radius
  const C = 2 * Math.PI * R  // circumference
  const ARC = (SWEEP / 360) * C

  // While dragging, the knob owns its value; otherwise it follows the engine.
  let dragging = false
  let local = value
  $: if (!dragging) local = value

  $: frac = max > min ? (local - min) / (max - min) : 0
  $: angle = -135 + frac * SWEEP

  let startY = 0
  let startVal = 0

  function down (e) {
    dragging = true
    startY = e.clientY
    startVal = local
    e.currentTarget.setPointerCapture?.(e.pointerId)
  }
  function move (e) {
    if (!dragging) return
    const dy = startY - e.clientY
    const speed = e.shiftKey ? 0.25 : 1            // hold shift for fine control
    const next = clamp(startVal + (dy / 200) * (max - min) * speed)
    apply(next)
  }
  function up (e) {
    dragging = false
    e.currentTarget.releasePointerCapture?.(e.pointerId)
  }
  function wheel (e) {
    e.preventDefault()
    const step = (max - min) * (e.shiftKey ? 0.005 : 0.02)
    apply(clamp(local - Math.sign(e.deltaY) * step))
  }
  function reset () {
    if (resetValue != null) apply(resetValue)
  }

  const clamp = (v) => Math.min(max, Math.max(min, v))
  function apply (v) {
    local = v
    dispatch('change', v)
  }
</script>

<div class="knob-wrap">
  <div class="knob" class:dragging
       role="slider" tabindex="0"
       aria-label={label} aria-valuemin={min} aria-valuemax={max} aria-valuenow={local}
       on:pointerdown={down} on:pointermove={move} on:pointerup={up}
       on:dblclick={reset} on:wheel|nonpassive={wheel}>
    <svg viewBox="0 0 48 48">
      <circle class="arc-bg" cx="24" cy="24" r={R}
              stroke-dasharray="{ARC} {C}" transform="rotate(135 24 24)" />
      <circle class="arc-fill" cx="24" cy="24" r={R}
              stroke-dasharray="{frac * ARC} {C}" transform="rotate(135 24 24)" />
    </svg>
    <div class="cap" style="transform: rotate({angle}deg)">
      <span class="indicator"></span>
    </div>
  </div>
  <div class="label">{label}</div>
  <div class="value">{format(local)}</div>
</div>

<style>
  .knob-wrap {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 3px;
    width: 64px;
  }
  .knob {
    position: relative;
    width: 56px;
    height: 56px;
    cursor: ns-resize;
    touch-action: none;
  }
  svg {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
  }
  .arc-bg {
    fill: none;
    stroke: var(--es-knob-edge);
    stroke-width: 4;
    stroke-linecap: round;
  }
  .arc-fill {
    fill: none;
    stroke: var(--es-accent);
    stroke-width: 4;
    stroke-linecap: round;
    filter: drop-shadow(0 0 3px rgba(212, 163, 115, 0.5));
    transition: stroke-dasharray 0.02s linear;
  }
  .cap {
    position: absolute;
    inset: 9px;
    border-radius: 50%;
    background: var(--es-knob-face);
    box-shadow:
      inset 0 1px 1px rgba(255, 255, 255, 0.15),
      inset 0 -2px 4px rgba(0, 0, 0, 0.6),
      0 2px 4px rgba(0, 0, 0, 0.5);
  }
  .knob.dragging .cap {
    box-shadow:
      inset 0 1px 1px rgba(255, 255, 255, 0.2),
      inset 0 -2px 4px rgba(0, 0, 0, 0.6),
      0 0 0 2px rgba(212, 163, 115, 0.35);
  }
  .indicator {
    position: absolute;
    top: 3px;
    left: 50%;
    width: 2px;
    height: 9px;
    margin-left: -1px;
    border-radius: 1px;
    background: var(--es-accent);
    box-shadow: 0 0 3px rgba(212, 163, 115, 0.7);
  }
  .label {
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.5px;
    text-transform: uppercase;
    color: var(--es-text-muted);
  }
  .value {
    font-family: var(--es-mono);
    font-size: 9px;
    color: var(--es-text-2);
  }
</style>
