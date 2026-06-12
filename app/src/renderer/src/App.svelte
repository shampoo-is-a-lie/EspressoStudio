<script>
  let linkUp = false
  let playing = false
  let recording = false
  let position = 0
  let levelL = 0 // linear 0..1
  let levelR = 0
  let device = '—'
  let sampleRate = 0
  let bufferSize = 0
  let latencyMs = 0
  let takes = []
  let logLines = []

  window.espresso.onLink(({ up }) => { linkUp = up })
  window.espresso.onEvent((ev) => {
    if (ev.event === 'hello') {
      device = ev.device
      sampleRate = ev.sampleRate
      bufferSize = ev.bufferSize
      latencyMs = ev.latencyMs
    } else if (ev.event === 'takes') {
      takes = ev.tracks
    } else if (ev.event === 'state') {
      playing = ev.playing
      recording = ev.recording
      position = ev.position
      levelL = ev.levelL
      levelR = ev.levelR
    }
  })
  window.espresso.onLog((line) => {
    logLines = [...logLines.slice(-200), line]
  })

  const send = (cmd) => window.espresso.send({ cmd })

  function timecode (secs) {
    const m = Math.floor(secs / 60)
    const s = Math.floor(secs % 60)
    const ms = Math.floor((secs % 1) * 1000)
    return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}.${String(ms).padStart(3, '0')}`
  }

  // map linear level to meter percentage with a dB-ish curve
  function meterPct (lin) {
    if (lin <= 0.0001) return 0
    const db = 20 * Math.log10(lin)
    return Math.max(0, Math.min(100, (db + 60) / 60 * 100))
  }
</script>

<main>
  <header>
    <span class="logo">☕ EspressoStudio</span>
    <span class="phase">phase 0 — first shot</span>
    <span class="link {linkUp ? 'up' : 'down'}">{linkUp ? '● engine linked' : '○ engine offline'}</span>
  </header>

  <section class="deck">
    <div class="lcd">
      <div class="time" class:recording>{timecode(position)}</div>
      <div class="lcd-info">
        <span>{device}</span>
        <span>{sampleRate ? `${sampleRate / 1000} kHz` : '—'}</span>
        <span>{bufferSize ? `${bufferSize} smp` : '—'}</span>
        <span class="latency">{latencyMs ? `${latencyMs.toFixed(1)} ms` : '—'}</span>
      </div>
    </div>

    <div class="transport">
      <button class="tbtn small" on:click={() => send('rewind')} title="Rewind to start">⏮</button>
      <button class="tbtn" on:click={() => send('stop')} title="Stop">■</button>
      <button class="tbtn play" class:active={playing} on:click={() => send('play')} title="Play">▶</button>
      <button class="tbtn rec" class:active={recording} on:click={() => send('record')} title="Record">●</button>
    </div>

    <div class="meters">
      {#each [['L', levelL], ['R', levelR]] as [label, lvl]}
        <div class="meter-row">
          <span class="meter-label">{label}</span>
          <div class="meter-track">
            <div class="meter-fill" style="width: {meterPct(lvl)}%"></div>
          </div>
        </div>
      {/each}
      <div class="takes">
        {#if takes.length === 0}
          <span class="takes-empty">no takes yet — hit ● and make some noise</span>
        {:else}
          {#each takes as t}
            <span class="take-chip">{t.track} ▸ {t.clips} take{t.clips > 1 ? 's' : ''} · {t.length.toFixed(1)}s</span>
          {/each}
        {/if}
      </div>
    </div>
  </section>

  <section class="log">
    <pre>{logLines.join('')}</pre>
  </section>
</main>

<style>
  :global(body) {
    margin: 0;
    background: #161618;
    color: #d8d8dc;
    font-family: 'Inter', 'Cantarell', system-ui, sans-serif;
    user-select: none;
  }
  main { display: flex; flex-direction: column; height: 100vh; }

  header {
    display: flex; align-items: center; gap: 16px;
    padding: 10px 18px;
    background: linear-gradient(#242428, #1c1c1f);
    border-bottom: 1px solid #000;
    font-size: 13px;
  }
  .logo { font-weight: 700; letter-spacing: 0.5px; color: #d4a373; }
  .phase { color: #6e6e76; font-style: italic; }
  .link { margin-left: auto; font-size: 12px; }
  .link.up { color: #7dc97f; }
  .link.down { color: #e06c60; }

  .deck {
    display: grid;
    grid-template-columns: 1fr auto 1fr;
    align-items: center;
    gap: 24px;
    padding: 22px 26px;
    background: linear-gradient(#202024, #1a1a1d);
    border-bottom: 1px solid #000;
  }

  .lcd {
    background: #0d1410;
    border: 1px solid #2a2a2e;
    border-radius: 8px;
    padding: 12px 18px;
    box-shadow: inset 0 2px 10px rgba(0,0,0,0.7);
  }
  .time {
    font-family: 'JetBrains Mono', monospace;
    font-size: 34px;
    color: #9fe2b0;
    text-shadow: 0 0 12px rgba(125, 226, 160, 0.35);
  }
  .time.recording { color: #ff6f61; text-shadow: 0 0 12px rgba(255, 110, 97, 0.4); }
  .lcd-info {
    display: flex; gap: 14px;
    margin-top: 6px;
    font-family: 'JetBrains Mono', monospace;
    font-size: 11px;
    color: #5d8a6b;
  }
  .latency { color: #d4a373; }

  .transport { display: flex; gap: 10px; }
  .tbtn {
    width: 58px; height: 58px;
    border-radius: 50%;
    border: 1px solid #38383e;
    background: linear-gradient(#2c2c31, #202024);
    color: #b8b8c0;
    font-size: 20px;
    cursor: pointer;
    box-shadow: 0 3px 8px rgba(0,0,0,0.5);
  }
  .tbtn.small { width: 44px; height: 44px; font-size: 15px; align-self: center; }
  .tbtn:hover { background: linear-gradient(#34343a, #26262b); }
  .tbtn:active { transform: translateY(1px); }
  .tbtn.play.active { color: #7dc97f; border-color: #4d7a50; box-shadow: 0 0 14px rgba(125, 201, 127, 0.3); }
  .tbtn.rec { color: #c9554c; }
  .tbtn.rec.active { color: #ff4438; border-color: #8a3530; box-shadow: 0 0 14px rgba(255, 68, 56, 0.4); }

  .meters { display: flex; flex-direction: column; gap: 8px; }
  .meter-row { display: flex; align-items: center; gap: 8px; }
  .meter-label { font-size: 11px; color: #6e6e76; width: 12px; }
  .meter-track {
    flex: 1; height: 12px;
    background: #101012;
    border-radius: 6px;
    border: 1px solid #2a2a2e;
    overflow: hidden;
  }
  .meter-fill {
    height: 100%;
    background: linear-gradient(90deg, #4d7a50, #7dc97f 60%, #e8c468 85%, #e06c60);
    transition: width 40ms linear;
  }

  .takes { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 4px; }
  .takes-empty { font-size: 11px; color: #5a5a62; font-style: italic; }
  .take-chip {
    font-family: 'JetBrains Mono', monospace;
    font-size: 10px;
    color: #9fe2b0;
    background: #15211a;
    border: 1px solid #2c4434;
    border-radius: 10px;
    padding: 2px 8px;
  }

  .log {
    flex: 1;
    overflow-y: auto;
    padding: 10px 18px;
    background: #121214;
  }
  .log pre {
    margin: 0;
    font-family: 'JetBrains Mono', monospace;
    font-size: 11px;
    color: #6e6e76;
    white-space: pre-wrap;
  }
</style>
