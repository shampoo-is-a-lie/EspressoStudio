<script>
  import { onMount, onDestroy } from 'svelte'
  import { tracks, transport, editLength, peaks, send } from '../stores.js'

  const ROW = 72
  const RULER = 28

  let wrap
  let canvas
  let pxPerSec = 60
  let raf = 0

  // local mirrors for the draw loop
  let trackData = []
  let pos = 0
  let playing = false
  let recording = false
  let lengthSec = 30
  let peakCache = {}

  const u1 = tracks.subscribe(v => { trackData = v })
  const u2 = transport.subscribe(v => { pos = v.position; playing = v.playing; recording = v.recording })
  const u3 = editLength.subscribe(v => { lengthSec = Math.max(v + 10, 30) })
  const u4 = peaks.subscribe(v => { peakCache = v })

  $: canvasW = Math.ceil(lengthSec * pxPerSec)
  $: canvasH = RULER + Math.max(trackData.length, 1) * ROW

  function draw () {
    if (!canvas) return
    const dpr = window.devicePixelRatio || 1
    const w = canvasW, h = canvasH

    if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
      canvas.width = w * dpr
      canvas.height = h * dpr
      canvas.style.width = w + 'px'
      canvas.style.height = h + 'px'
    }

    const ctx = canvas.getContext('2d')
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)

    // background
    ctx.fillStyle = '#141416'
    ctx.fillRect(0, 0, w, h)

    // track rows
    for (let i = 0; i < Math.max(trackData.length, 1); i++) {
      const y = RULER + i * ROW
      ctx.fillStyle = i % 2 ? '#17171a' : '#19191c'
      ctx.fillRect(0, y, w, ROW)
      ctx.fillStyle = '#0c0c0e'
      ctx.fillRect(0, y + ROW - 1, w, 1)
    }

    // grid lines (1s minor, 5s major)
    for (let s = 0; s <= lengthSec; s++) {
      const x = Math.round(s * pxPerSec) + 0.5
      ctx.strokeStyle = s % 5 === 0 ? '#2a2a30' : '#1f1f24'
      ctx.beginPath()
      ctx.moveTo(x, RULER)
      ctx.lineTo(x, h)
      ctx.stroke()
    }

    // clips
    trackData.forEach((t, ti) => {
      const rowY = RULER + ti * ROW
      t.clips.forEach((c) => {
        const x = c.start * pxPerSec
        const cw = Math.max(c.length * pxPerSec, 3)
        const y = rowY + 5
        const ch = ROW - 11

        ctx.fillStyle = '#23352a'
        ctx.strokeStyle = '#4d7a50'
        ctx.beginPath()
        ctx.roundRect(x, y, cw, ch, 5)
        ctx.fill()
        ctx.stroke()

        // waveform
        const pk = peakCache[c.id]
        if (pk && pk.data.length) {
          ctx.fillStyle = 'rgba(159, 226, 176, 0.85)'
          const mid = y + ch / 2
          const maxAmp = ch / 2 - 3
          const step = Math.max(1, Math.floor(pk.data.length / cw))
          for (let px = 0; px < cw - 2; px++) {
            const idx = Math.floor(px / cw * pk.data.length)
            let mag = 0
            for (let k = 0; k < step && idx + k < pk.data.length; k++) mag = Math.max(mag, pk.data[idx + k])
            const amp = Math.max(1, mag * maxAmp * 3) // gain for visibility
            ctx.fillRect(x + 1 + px, mid - Math.min(amp, maxAmp), 1, Math.min(amp, maxAmp) * 2)
          }
        }

        // name + takes badge
        ctx.fillStyle = '#bfe9cb'
        ctx.font = '600 9px Inter, sans-serif'
        ctx.save()
        ctx.beginPath()
        ctx.rect(x, y, cw, ch)
        ctx.clip()
        ctx.fillText(c.name, x + 6, y + 12)
        if (c.takes > 1)
          ctx.fillText(`◆ ${c.takes} takes`, x + 6, y + ch - 5)
        ctx.restore()
      })
    })

    // ruler
    ctx.fillStyle = '#19191c'
    ctx.fillRect(0, 0, w, RULER)
    ctx.fillStyle = '#0c0c0e'
    ctx.fillRect(0, RULER - 1, w, 1)
    ctx.font = '9px JetBrains Mono, monospace'
    for (let s = 0; s <= lengthSec; s++) {
      const x = Math.round(s * pxPerSec) + 0.5
      const major = s % 5 === 0
      ctx.strokeStyle = major ? '#55555c' : '#33333a'
      ctx.beginPath()
      ctx.moveTo(x, major ? RULER - 12 : RULER - 6)
      ctx.lineTo(x, RULER)
      ctx.stroke()
      if (major && pxPerSec > 14) {
        ctx.fillStyle = '#8a8a92'
        const m = Math.floor(s / 60), sec = s % 60
        ctx.fillText(`${m}:${String(sec).padStart(2, '0')}`, x + 3, RULER - 14)
      }
    }

    // playhead
    const phX = Math.round(pos * pxPerSec) + 0.5
    ctx.strokeStyle = recording ? '#ff4438' : '#e8c468'
    ctx.lineWidth = 1.5
    ctx.beginPath()
    ctx.moveTo(phX, 0)
    ctx.lineTo(phX, h)
    ctx.stroke()
    ctx.lineWidth = 1
    ctx.fillStyle = recording ? '#ff4438' : '#e8c468'
    ctx.beginPath()
    ctx.moveTo(phX - 5, 0)
    ctx.lineTo(phX + 5, 0)
    ctx.lineTo(phX, 8)
    ctx.closePath()
    ctx.fill()

    // keep playhead in view while rolling
    if ((playing || recording) && wrap) {
      const margin = 120
      if (phX > wrap.scrollLeft + wrap.clientWidth - margin || phX < wrap.scrollLeft)
        wrap.scrollLeft = Math.max(0, phX - margin)
    }

    raf = requestAnimationFrame(draw)
  }

  function onClick (e) {
    const r = canvas.getBoundingClientRect()
    const secs = Math.max(0, (e.clientX - r.left) / pxPerSec)
    send({ cmd: 'seek', seconds: secs })
  }

  function zoom (factor) {
    pxPerSec = Math.min(300, Math.max(8, pxPerSec * factor))
  }

  onMount(() => { raf = requestAnimationFrame(draw) })
  onDestroy(() => { cancelAnimationFrame(raf); u1(); u2(); u3(); u4() })
</script>

<div class="arrange">
  <div class="zoom">
    <button on:click={() => zoom(1 / 1.4)} title="Zoom out">−</button>
    <button on:click={() => zoom(1.4)} title="Zoom in">+</button>
  </div>
  <div class="scroll" bind:this={wrap}>
    <canvas bind:this={canvas} on:click={onClick}></canvas>
  </div>
</div>

<style>
  .arrange { flex: 1; position: relative; overflow: hidden; background: #141416; }
  .scroll { width: 100%; height: 100%; overflow: auto; }
  canvas { display: block; cursor: text; }

  .zoom {
    position: absolute; right: 14px; bottom: 14px;
    display: flex; gap: 4px;
    z-index: 20;
  }
  .zoom button {
    width: 30px; height: 30px;
    border-radius: 6px;
    border: 1px solid #38383e;
    background: rgba(35, 35, 39, 0.9);
    color: #b8b8c0;
    font-size: 15px;
    cursor: pointer;
  }
  .zoom button:hover { color: #d4a373; }
</style>
