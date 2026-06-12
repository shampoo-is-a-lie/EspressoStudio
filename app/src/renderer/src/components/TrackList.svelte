<script>
  import { tracks, fxTrack, send } from '../stores.js'

  export const ROW = 72
</script>

<div class="headers">
  <div class="corner">tracks</div>
  {#each $tracks as t, i}
    <div class="track-header">
      <div class="row1">
        <span class="tname">{t.name}</span>
        <button class="x" title="Delete track" on:click={() => send({ cmd: 'removeTrack', track: i })}>×</button>
      </div>
      <div class="row2">
        <button class="tgl arm" class:on={t.armed} title="Record arm" on:click={() => send({ cmd: 'arm', track: i, on: !t.armed })}>R</button>
        <button class="tgl mute" class:on={t.mute} title="Mute" on:click={() => send({ cmd: 'mute', track: i, on: !t.mute })}>M</button>
        <button class="tgl solo" class:on={t.solo} title="Solo" on:click={() => send({ cmd: 'solo', track: i, on: !t.solo })}>S</button>
        <button class="tgl fx" class:on={$fxTrack === i || (t.plugins ?? []).some(p => p.external)}
                title="FX chain" on:click={() => fxTrack.set($fxTrack === i ? null : i)}>fx</button>
        <input class="pan" type="range" min="-1" max="1" step="0.05" title="Pan"
               value={t.pan ?? 0} on:input={(e) => send({ cmd: 'pan', track: i, value: +e.target.value })} />
      </div>
      <div class="row3">
        <input class="vol" type="range" min="-60" max="6" step="0.5" title="Volume (dB)"
               value={t.volumeDb ?? 0} on:input={(e) => send({ cmd: 'volume', track: i, db: +e.target.value })} />
        <span class="db">{(t.volumeDb ?? 0).toFixed(1)}</span>
      </div>
    </div>
  {/each}
  <button class="add" on:click={() => send({ cmd: 'addTrack' })}>+ track</button>
</div>

<style>
  .headers {
    width: 210px;
    min-width: 210px;
    background: #1b1b1e;
    border-right: 1px solid #000;
    overflow-y: auto;
    display: flex; flex-direction: column;
  }
  .corner {
    height: 28px; min-height: 28px;
    display: flex; align-items: center;
    padding: 0 12px;
    font-size: 10px; color: #6e6e76; text-transform: uppercase; letter-spacing: 1px;
    background: #19191c;
    border-bottom: 1px solid #000;
    box-sizing: border-box;
  }
  .track-header {
    height: 72px; min-height: 72px;
    box-sizing: border-box;
    padding: 7px 10px;
    border-bottom: 1px solid #0c0c0e;
    background: linear-gradient(#222226, #1d1d21);
    display: flex; flex-direction: column; gap: 4px;
  }
  .row1 { display: flex; align-items: center; }
  .tname { font-size: 12px; font-weight: 600; color: #c9c9cf; flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .x { background: none; border: none; color: #55555c; cursor: pointer; font-size: 13px; padding: 0 2px; }
  .x:hover { color: #e06c60; }

  .row2 { display: flex; align-items: center; gap: 4px; }
  .tgl {
    width: 20px; height: 18px;
    border-radius: 4px;
    border: 1px solid #38383e;
    background: #232327;
    color: #8a8a92;
    font-size: 9px; font-weight: 700;
    cursor: pointer;
    padding: 0;
  }
  .tgl.arm.on { background: #6e2420; border-color: #a03830; color: #ff7a6e; }
  .tgl.mute.on { background: #5c4a16; border-color: #8a702a; color: #e8c468; }
  .tgl.solo.on { background: #1f4a52; border-color: #2e6e7a; color: #6ecbdc; }
  .tgl.fx { width: 24px; font-size: 8px; }
  .tgl.fx.on { background: #4a3a1f; border-color: #8a702a; color: #e8c468; }
  .pan { flex: 1; height: 14px; accent-color: #8a8a92; }

  .row3 { display: flex; align-items: center; gap: 6px; }
  .vol { flex: 1; height: 14px; accent-color: #d4a373; }
  .db { font-family: 'JetBrains Mono', monospace; font-size: 9px; color: #8a8a92; width: 34px; text-align: right; }

  .add {
    margin: 10px;
    padding: 7px;
    background: none;
    border: 1px dashed #3a3a40;
    border-radius: 6px;
    color: #8a8a92;
    font-size: 11px;
    cursor: pointer;
  }
  .add:hover { color: #d4a373; border-color: #d4a373; }
</style>
