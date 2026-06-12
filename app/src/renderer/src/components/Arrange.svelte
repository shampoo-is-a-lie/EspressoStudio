<script>
  import { onMount, onDestroy } from 'svelte'
  import { tracks, transport, editLength, peaks, selection, send } from '../stores.js'

  const ROW = 72
  const RULER = 28
  const EDGE = 6        // px hit zone for trim handles
  const FADE_R = 5      // fade handle radius
  const SNAP = 0.25     // seconds (hold Alt to disable)

  let wrap
  let canvas
  let pxPerSec = 60
  let raf = 0

  // local mirrors for the draw loop
  let trackData = []
  let pos = 0
  let playing = false
  let recording = false
  let looping = false
  let loopStart = 0
  let loopEnd = 0
  let lengthSec = 30
  let peakCache = {}
  let sel = null

  // active pointer gesture: { mode, track, clip, ...gesture state } or null
  let drag = null
  // visual override while dragging: { id, start, length, offset, fadeIn, fadeOut }
  let preview = null
  let hoverCursor = 'default'

  const u1 = tracks.subscribe(v => { trackData = v; preview = null })
  const u2 = transport.subscribe(v => {
    pos = v.position; playing = v.playing; recording = v.recording
    looping = v.looping; loopStart = v.loopStart; loopEnd = v.loopEnd
  })
  const u3 = editLength.subscribe(v => { lengthSec = Math.max(v + 10, 30) })
  const u4 = peaks.subscribe(v => { peakCache = v })
  const u5 = selection.subscribe(v => { sel = v })

  $: canvasW = Math.ceil(lengthSec * pxPerSec)
  $: canvasH = RULER + Math.max(trackData.length, 1) * ROW

  const snap = (secs, e) => e.altKey ? secs : Math.round(secs / SNAP) * SNAP

  function clipRect (ti, c) {
    const o = (preview && preview.id === c.id) ? preview : c
    return { x: o.start * pxPerSec, w: Math.max(o.length * pxPerSec, 3), y: RULER + ti * ROW + 5, h: ROW - 11, o }
  }

  function hitTest (mx, my) {
    for (let ti = 0; ti < trackData.length; ti++) {
      for (let ci = 0; ci < trackData[ti].clips.length; ci++) {
        const c = trackData[ti].clips[ci]
        const { x, w, y, h } = clipRect(ti, c)
        if (my < y || my > y + h) continue
        if (mx < x - EDGE || mx > x + w + EDGE) continue

        // fade handles sit on the top edge
        const fiX = x + c.fadeIn * pxPerSec
        const foX = x + w - c.fadeOut * pxPerSec
        if (Math.abs(mx - fiX) <= FADE_R + 2 && my <= y + 10) return { mode: 'fadeIn', ti, ci, c }
        if (Math.abs(mx - foX) <= FADE_R + 2 && my <= y + 10) return { mode: 'fadeOut', ti, ci, c }

        // take badge bottom-left
        if (c.takes > 1 && mx >= x + 4 && mx <= x + 62 && my >= y + h - 15) return { mode: 'take', ti, ci, c }

        if (Math.abs(mx - x) <= EDGE) return { mode: 'trimL', ti, ci, c }
        if (Math.abs(mx - (x + w)) <= EDGE) return { mode: 'trimR', ti, ci, c }
        return { mode: 'move', ti, ci, c }
      }
    }
    return null
  }

  function canvasXY (e) {
    const r = canvas.getBoundingClientRect()
    return { mx: e.clientX - r.left, my: e.clientY - r.top }
  }

  function onPointerDown (e) {
    const { mx, my } = canvasXY(e)

    // ruler: drag sets a loop range, plain click seeks (and clears the loop)
    if (my <= RULER) {
      drag = { mode: 'loop', startMx: mx, moved: false }
      canvas.setPointerCapture(e.pointerId)
      return
    }

    const hit = hitTest(mx, my)

    if (!hit) {
      selection.set(null)
      send({ cmd: 'seek', seconds: Math.max(0, snap(mx / pxPerSec, e)) })
      return
    }

    selection.set({ track: hit.ti, clip: hit.ci, id: hit.c.id })

    if (hit.mode === 'take') {
      const next = (hit.c.currentTake + 1) % hit.c.takes
      send({ cmd: 'setTake', track: hit.ti, clip: hit.ci, take: next })
      return
    }

    drag = {
      ...hit,
      startMx: mx,
      orig: { start: hit.c.start, length: hit.c.length, offset: hit.c.offset, fadeIn: hit.c.fadeIn, fadeOut: hit.c.fadeOut },
      moved: false
    }
    canvas.setPointerCapture(e.pointerId)
  }

  function onPointerMove (e) {
    const { mx, my } = canvasXY(e)

    if (!drag) {
      const hit = hitTest(mx, my)
      hoverCursor = !hit ? 'text'
        : hit.mode === 'move' ? 'grab'
        : hit.mode === 'take' ? 'pointer'
        : hit.mode.startsWith('fade') ? 'ns-resize'
        : 'ew-resize'
      return
    }

    const dSec = (mx - drag.startMx) / pxPerSec
    if (Math.abs(mx - drag.startMx) > 3) drag.moved = true

    if (drag.mode === 'loop') {
      // keep the preview on the drag object — the 30Hz state events
      // overwrite the loopStart/loopEnd mirrors mid-drag otherwise
      const a = Math.max(0, snap(drag.startMx / pxPerSec, e))
      const b = Math.max(0, snap(mx / pxPerSec, e))
      drag.a = Math.min(a, b); drag.b = Math.max(a, b)
      return
    }

    const o = drag.orig
    const c = drag.c

    if (drag.mode === 'move') {
      preview = { id: c.id, ...o, start: Math.max(0, snap(o.start + dSec, e)) }
    } else if (drag.mode === 'trimL') {
      const newStart = Math.min(Math.max(0, snap(o.start + dSec, e)), o.start + o.length - 0.05)
      preview = { id: c.id, ...o, start: newStart, length: o.start + o.length - newStart, offset: o.offset + (newStart - o.start) }
    } else if (drag.mode === 'trimR') {
      const newEnd = Math.max(snap(o.start + o.length + dSec, e), o.start + 0.05)
      preview = { id: c.id, ...o, length: newEnd - o.start }
    } else if (drag.mode === 'fadeIn') {
      preview = { id: c.id, ...o, fadeIn: Math.min(Math.max(0, o.fadeIn + dSec), o.length) }
    } else if (drag.mode === 'fadeOut') {
      preview = { id: c.id, ...o, fadeOut: Math.min(Math.max(0, o.fadeOut - dSec), o.length) }
    }
  }

  function onPointerUp (e) {
    if (!drag) return
    const d = drag
    drag = null

    if (d.mode === 'loop') {
      if (d.moved && d.b - d.a > 0.05) {
        send({ cmd: 'setLoop', start: d.a, end: d.b, on: true })
      } else {
        send({ cmd: 'setLoop', start: 0, end: 0, on: false })
        send({ cmd: 'seek', seconds: Math.max(0, snap(d.startMx / pxPerSec, e)) })
      }
      return
    }

    if (!d.moved || !preview) { preview = null; return }

    const p = preview
    if (d.mode === 'move') {
      send({ cmd: 'moveClip', track: d.ti, clip: d.ci, start: p.start })
    } else if (d.mode === 'trimL') {
      send({ cmd: 'trimClip', track: d.ti, clip: d.ci, edge: 'start', to: p.start })
    } else if (d.mode === 'trimR') {
      send({ cmd: 'trimClip', track: d.ti, clip: d.ci, edge: 'end', to: p.start + p.length })
    } else if (d.mode === 'fadeIn' || d.mode === 'fadeOut') {
      send({ cmd: 'setFades', track: d.ti, clip: d.ci, fadeIn: p.fadeIn, fadeOut: p.fadeOut })
    }
    // keep preview until the authoritative tracks event replaces it
  }

  function onKey (e) {
    if (e.target.tagName === 'INPUT') return
    if (e.code === 'Space') { e.preventDefault(); send({ cmd: playing ? 'stop' : 'play' }) }
    else if (e.key === 'Delete' || e.key === 'Backspace') {
      if (sel) send({ cmd: 'deleteClip', track: sel.track, clip: sel.clip })
    }
    else if ((e.ctrlKey || e.metaKey) && e.key === 'z' && !e.shiftKey) { e.preventDefault(); send({ cmd: 'undo' }) }
    else if ((e.ctrlKey || e.metaKey) && (e.key === 'Z' || e.key === 'y')) { e.preventDefault(); send({ cmd: 'redo' }) }
    else if ((e.ctrlKey || e.metaKey) && e.key === 't') {
      e.preventDefault()
      if (sel) send({ cmd: 'splitClip', track: sel.track, clip: sel.clip, at: pos })
    }
    else if (e.key === 'r' && !e.ctrlKey && !e.metaKey) { send({ cmd: 'record' }) }
  }

  function draw () {
    if (!canvas) { raf = requestAnimationFrame(draw); return }
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

    ctx.fillStyle = '#141416'
    ctx.fillRect(0, 0, w, h)

    for (let i = 0; i < Math.max(trackData.length, 1); i++) {
      const y = RULER + i * ROW
      ctx.fillStyle = i % 2 ? '#17171a' : '#19191c'
      ctx.fillRect(0, y, w, ROW)
      ctx.fillStyle = '#0c0c0e'
      ctx.fillRect(0, y + ROW - 1, w, 1)
    }

    for (let s = 0; s <= lengthSec; s++) {
      const x = Math.round(s * pxPerSec) + 0.5
      ctx.strokeStyle = s % 5 === 0 ? '#2a2a30' : '#1f1f24'
      ctx.beginPath(); ctx.moveTo(x, RULER); ctx.lineTo(x, h); ctx.stroke()
    }

    trackData.forEach((t, ti) => {
      t.clips.forEach((c) => {
        const { x, w: cw, y, h: ch, o } = clipRect(ti, c)
        const isSel = sel && sel.id === c.id

        ctx.fillStyle = isSel ? '#2b4534' : '#23352a'
        ctx.strokeStyle = isSel ? '#8fd99a' : '#4d7a50'
        ctx.lineWidth = isSel ? 1.5 : 1
        ctx.beginPath()
        ctx.roundRect(x, y, cw, ch, 5)
        ctx.fill()
        ctx.stroke()
        ctx.lineWidth = 1

        // waveform — slice the source peaks by clip offset/length
        const pk = peakCache[c.id]
        if (pk && pk.data.length) {
          ctx.fillStyle = 'rgba(159, 226, 176, 0.85)'
          const mid = y + ch / 2
          const maxAmp = ch / 2 - 3
          const i0 = Math.floor(o.offset * pk.pps)
          const i1 = Math.min(pk.data.length, Math.ceil((o.offset + o.length) * pk.pps))
          const n = Math.max(1, i1 - i0)
          for (let px = 0; px < cw - 2; px++) {
            const idx = i0 + Math.floor(px / cw * n)
            const step = Math.max(1, Math.floor(n / cw))
            let mag = 0
            for (let k = 0; k < step && idx + k < pk.data.length; k++) mag = Math.max(mag, pk.data[idx + k])
            const amp = Math.max(1, mag * maxAmp * 3)
            ctx.fillRect(x + 1 + px, mid - Math.min(amp, maxAmp), 1, Math.min(amp, maxAmp) * 2)
          }
        }

        // fades: darken faded regions, draw ramps + handles
        const fiW = o.fadeIn * pxPerSec
        const foW = o.fadeOut * pxPerSec
        ctx.save()
        ctx.beginPath(); ctx.roundRect(x, y, cw, ch, 5); ctx.clip()
        if (fiW > 1) {
          ctx.fillStyle = 'rgba(0,0,0,0.35)'
          ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + fiW, y); ctx.lineTo(x, y + ch); ctx.closePath(); ctx.fill()
          ctx.strokeStyle = '#8fd99a'
          ctx.beginPath(); ctx.moveTo(x, y + ch); ctx.lineTo(x + fiW, y); ctx.stroke()
        }
        if (foW > 1) {
          ctx.fillStyle = 'rgba(0,0,0,0.35)'
          ctx.beginPath(); ctx.moveTo(x + cw - foW, y); ctx.lineTo(x + cw, y); ctx.lineTo(x + cw, y + ch); ctx.closePath(); ctx.fill()
          ctx.strokeStyle = '#8fd99a'
          ctx.beginPath(); ctx.moveTo(x + cw - foW, y); ctx.lineTo(x + cw, y + ch); ctx.stroke()
        }
        ctx.restore()

        if (isSel || fiW > 1 || foW > 1) {
          ctx.fillStyle = '#8fd99a'
          ctx.beginPath(); ctx.arc(x + fiW, y + 4, FADE_R - 1, 0, Math.PI * 2); ctx.fill()
          ctx.beginPath(); ctx.arc(x + cw - foW, y + 4, FADE_R - 1, 0, Math.PI * 2); ctx.fill()
        }

        // name + takes badge
        ctx.save()
        ctx.beginPath(); ctx.rect(x, y, cw, ch); ctx.clip()
        ctx.fillStyle = '#bfe9cb'
        ctx.font = '600 9px Inter, sans-serif'
        ctx.fillText(c.name, x + 6, y + 14)
        if (c.takes > 1) {
          ctx.fillStyle = '#1a2b20'
          ctx.beginPath(); ctx.roundRect(x + 4, y + ch - 16, 58, 13, 4); ctx.fill()
          ctx.fillStyle = '#9fe2b0'
          ctx.fillText(`◆ take ${c.currentTake + 1}/${c.takes}`, x + 8, y + ch - 6)
        }
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

    // loop brace on the ruler (drag preview takes precedence)
    const inLoopDrag = drag && drag.mode === 'loop' && drag.moved
    const ls = inLoopDrag ? drag.a : loopStart
    const le = inLoopDrag ? drag.b : loopEnd
    if ((looping || inLoopDrag) && le > ls) {
      const lx = ls * pxPerSec
      const lw = (le - ls) * pxPerSec
      ctx.fillStyle = 'rgba(232, 196, 104, 0.25)'
      ctx.fillRect(lx, 0, lw, RULER - 1)
      ctx.fillStyle = '#e8c468'
      ctx.fillRect(lx, 0, lw, 3)
      ctx.fillRect(lx, 0, 2, RULER - 1)
      ctx.fillRect(lx + lw - 2, 0, 2, RULER - 1)
    }

    // playhead
    const phX = Math.round(pos * pxPerSec) + 0.5
    ctx.strokeStyle = recording ? '#ff4438' : '#e8c468'
    ctx.lineWidth = 1.5
    ctx.beginPath(); ctx.moveTo(phX, 0); ctx.lineTo(phX, h); ctx.stroke()
    ctx.lineWidth = 1
    ctx.fillStyle = recording ? '#ff4438' : '#e8c468'
    ctx.beginPath(); ctx.moveTo(phX - 5, 0); ctx.lineTo(phX + 5, 0); ctx.lineTo(phX, 8); ctx.closePath(); ctx.fill()

    if ((playing || recording) && wrap && !drag) {
      const margin = 120
      if (phX > wrap.scrollLeft + wrap.clientWidth - margin || phX < wrap.scrollLeft)
        wrap.scrollLeft = Math.max(0, phX - margin)
    }

    raf = requestAnimationFrame(draw)
  }

  function zoom (factor) {
    pxPerSec = Math.min(300, Math.max(8, pxPerSec * factor))
  }

  onMount(() => {
    raf = requestAnimationFrame(draw)
    window.addEventListener('keydown', onKey)
  })
  onDestroy(() => {
    cancelAnimationFrame(raf)
    window.removeEventListener('keydown', onKey)
    u1(); u2(); u3(); u4(); u5()
  })
</script>

<div class="arrange">
  <div class="zoom">
    <button on:click={() => zoom(1 / 1.4)} title="Zoom out">−</button>
    <button on:click={() => zoom(1.4)} title="Zoom in">+</button>
  </div>
  <div class="scroll" bind:this={wrap}>
    <canvas
      bind:this={canvas}
      style="cursor: {drag ? (drag.mode === 'move' ? 'grabbing' : hoverCursor) : hoverCursor}"
      on:pointerdown={onPointerDown}
      on:pointermove={onPointerMove}
      on:pointerup={onPointerUp}
      on:pointercancel={onPointerUp}
    ></canvas>
  </div>
</div>

<style>
  .arrange { flex: 1; position: relative; overflow: hidden; background: #141416; }
  .scroll { width: 100%; height: 100%; overflow: auto; }
  canvas { display: block; }

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
