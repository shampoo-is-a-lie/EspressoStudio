<script>
  import { onDestroy } from 'svelte'
  import { hello, transport, linkUp, exportResult, send } from '../stores.js'

  let showExport = false
  let toast = ''
  let toastTimer = null

  const unsub = exportResult.subscribe(v => {
    if (!v) return
    toast = v.ok ? `☕ exported ${v.file.split('/').pop()}` : '✗ export failed'
    clearTimeout(toastTimer)
    toastTimer = setTimeout(() => { toast = '' }, 7000)
  })
  onDestroy(() => { unsub(); clearTimeout(toastTimer) })

  function timecode (secs) {
    const m = Math.floor(secs / 60)
    const s = Math.floor(secs % 60)
    const ms = Math.floor((secs % 1) * 1000)
    return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}.${String(ms).padStart(3, '0')}`
  }

  function meterPct (lin) {
    if (lin <= 0.0001) return 0
    const db = 20 * Math.log10(lin)
    return Math.max(0, Math.min(100, (db + 60) / 60 * 100))
  }

  function doExport (format) {
    showExport = false
    toast = '☕ exporting…'
    send({ cmd: 'export', format, bitDepth: 24 })
  }
</script>

<header>
  <span class="logo">☕ EspressoStudio</span>

  <div class="transport">
    <button class="tbtn" on:click={() => send({ cmd: 'rewind' })} title="Rewind">⏮</button>
    <button class="tbtn" on:click={() => send({ cmd: 'stop' })} title="Stop">■</button>
    <button class="tbtn play" class:active={$transport.playing} on:click={() => send({ cmd: 'play' })} title="Play">▶</button>
    <button class="tbtn rec" class:active={$transport.recording} on:click={() => send({ cmd: 'record' })} title="Record">●</button>
  </div>

  <div class="lcd">
    <span class="time" class:recording={$transport.recording}>{timecode($transport.position)}</span>
    <span class="lcd-detail">
      {#if $hello}
        {($hello.sampleRate / 1000)} kHz · {$hello.bufferSize} smp · app {$hello.latencyMs.toFixed(1)} ms · sys {($hello.systemLatencyMs ?? $hello.latencyMs).toFixed(1)} ms
      {:else}
        no device
      {/if}
    </span>
  </div>

  <div class="meters">
    {#each [$transport.levelL, $transport.levelR] as lvl}
      <div class="meter-track"><div class="meter-fill" style="width: {meterPct(lvl)}%"></div></div>
    {/each}
  </div>

  {#if toast}
    <span class="toast">{toast}</span>
  {/if}

  <div class="right">
    <div class="export-wrap">
      <button class="export-btn" on:click={() => showExport = !showExport}>Export ▾</button>
      {#if showExport}
        <div class="export-menu">
          <button on:click={() => doExport('wav')}>WAV · 24-bit</button>
          <button on:click={() => doExport('flac')}>FLAC · 24-bit</button>
        </div>
      {/if}
    </div>
    <span class="link {$linkUp ? 'up' : 'down'}">{$linkUp ? '●' : '○'}</span>
  </div>
</header>

<style>
  header {
    display: flex; align-items: center; gap: 18px;
    padding: 8px 16px;
    background: linear-gradient(#242428, #1c1c1f);
    border-bottom: 1px solid #000;
    z-index: 10;
  }
  .logo { font-weight: 700; font-size: 13px; letter-spacing: 0.5px; color: #d4a373; white-space: nowrap; }

  .transport { display: flex; gap: 6px; }
  .tbtn {
    width: 38px; height: 38px;
    border-radius: 50%;
    border: 1px solid #38383e;
    background: linear-gradient(#2c2c31, #202024);
    color: #b8b8c0;
    font-size: 13px;
    cursor: pointer;
  }
  .tbtn:hover { background: linear-gradient(#34343a, #26262b); }
  .tbtn:active { transform: translateY(1px); }
  .tbtn.play.active { color: #7dc97f; border-color: #4d7a50; box-shadow: 0 0 10px rgba(125, 201, 127, 0.3); }
  .tbtn.rec { color: #c9554c; }
  .tbtn.rec.active { color: #ff4438; border-color: #8a3530; box-shadow: 0 0 10px rgba(255, 68, 56, 0.4); }

  .lcd {
    background: #0d1410;
    border: 1px solid #2a2a2e;
    border-radius: 6px;
    padding: 5px 14px;
    display: flex; flex-direction: column; align-items: center;
    box-shadow: inset 0 2px 8px rgba(0,0,0,0.7);
    min-width: 230px;
  }
  .time {
    font-family: 'JetBrains Mono', monospace;
    font-size: 19px;
    color: #9fe2b0;
    text-shadow: 0 0 10px rgba(125, 226, 160, 0.3);
  }
  .time.recording { color: #ff6f61; text-shadow: 0 0 10px rgba(255, 110, 97, 0.4); }
  .lcd-detail { font-family: 'JetBrains Mono', monospace; font-size: 9px; color: #5d8a6b; white-space: nowrap; }

  .meters { display: flex; flex-direction: column; gap: 4px; width: 130px; }
  .meter-track {
    height: 8px;
    background: #101012;
    border-radius: 4px;
    border: 1px solid #2a2a2e;
    overflow: hidden;
  }
  .meter-fill {
    height: 100%;
    background: linear-gradient(90deg, #4d7a50, #7dc97f 60%, #e8c468 85%, #e06c60);
    transition: width 40ms linear;
  }

  .toast { font-size: 11px; color: #9fe2b0; max-width: 260px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

  .right { margin-left: auto; display: flex; align-items: center; gap: 12px; }
  .export-wrap { position: relative; }
  .export-btn {
    background: linear-gradient(#2c2c31, #202024);
    border: 1px solid #38383e;
    color: #d4a373;
    border-radius: 6px;
    padding: 7px 14px;
    font-size: 12px;
    cursor: pointer;
  }
  .export-btn:hover { background: linear-gradient(#34343a, #26262b); }
  .export-menu {
    position: absolute; right: 0; top: 110%;
    background: #232327;
    border: 1px solid #38383e;
    border-radius: 8px;
    display: flex; flex-direction: column;
    overflow: hidden;
    box-shadow: 0 8px 22px rgba(0,0,0,0.6);
    z-index: 50;
  }
  .export-menu button {
    background: none; border: none;
    color: #d8d8dc; font-size: 12px;
    padding: 9px 18px; cursor: pointer; text-align: left; white-space: nowrap;
  }
  .export-menu button:hover { background: #2e2e34; color: #d4a373; }

  .link.up { color: #7dc97f; }
  .link.down { color: #e06c60; }
</style>
