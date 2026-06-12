<script>
  import { tracks, knownPlugins, fxTrack, send } from '../stores.js'

  let search = ''

  $: ti = $fxTrack
  $: track = ti != null ? $tracks[ti] : null
  // engine indexes plugins within the full chain (incl. built-in volume/meter)
  $: chain = track ? track.plugins.map((p, idx) => ({ ...p, idx })).filter(p => p.external) : []
  $: results = $knownPlugins.plugins
      .filter(p => !search || p.name.toLowerCase().includes(search.toLowerCase()))
      .slice(0, 60)

  const close = () => fxTrack.set(null)
</script>

{#if track}
  <aside>
    <div class="head">
      <span class="title">{track.name} — FX</span>
      <button class="x" on:click={close}>×</button>
    </div>

    <div class="chain">
      {#if chain.length === 0}
        <div class="empty">no plugins yet — add one below</div>
      {/if}
      {#each chain as p, slot}
        <div class="slot">
          <button class="bypass" class:on={p.enabled} title={p.enabled ? 'Bypass' : 'Enable'}
                  on:click={() => send({ cmd: 'setPluginEnabled', track: ti, plugin: p.idx, on: !p.enabled })}>⏻</button>
          <span class="pname" class:bypassed={!p.enabled}>{slot + 1}. {p.name}</span>
          <button class="open" title="Open plugin editor"
                  on:click={() => send({ cmd: 'showPluginEditor', track: ti, plugin: p.idx })}>ui</button>
          <button class="rm" title="Remove"
                  on:click={() => send({ cmd: 'removePlugin', track: ti, plugin: p.idx })}>×</button>
        </div>
      {/each}
    </div>

    <div class="browser">
      <div class="browser-head">
        <input placeholder="search plugins…" bind:value={search} />
        <button class="scan" disabled={$knownPlugins.scanning} on:click={() => send({ cmd: 'scanPlugins' })}>
          {$knownPlugins.scanning ? 'scanning…' : 'scan'}
        </button>
      </div>
      <div class="results">
        {#if $knownPlugins.plugins.length === 0 && !$knownPlugins.scanning}
          <div class="empty">no plugins known — hit scan</div>
        {/if}
        {#each results as p}
          <button class="result" on:click={() => send({ cmd: 'addPlugin', track: ti, ident: p.ident })}>
            <span class="fmt">{p.format}</span>
            <span class="rname">{p.name}</span>
            <span class="cat">{p.category}</span>
          </button>
        {/each}
      </div>
    </div>
  </aside>
{/if}

<style>
  aside {
    width: 290px; min-width: 290px;
    background: #1d1d21;
    border-left: 1px solid #000;
    display: flex; flex-direction: column;
    overflow: hidden;
  }
  .head {
    display: flex; align-items: center;
    padding: 10px 12px;
    background: #222226;
    border-bottom: 1px solid #0c0c0e;
  }
  .title { flex: 1; font-size: 12px; font-weight: 700; color: #d4a373; }
  .x { background: none; border: none; color: #8a8a92; font-size: 16px; cursor: pointer; }
  .x:hover { color: #e06c60; }

  .chain { padding: 8px; display: flex; flex-direction: column; gap: 4px; }
  .empty { font-size: 11px; color: #5a5a62; font-style: italic; padding: 8px; text-align: center; }

  .slot {
    display: flex; align-items: center; gap: 6px;
    background: #232327;
    border: 1px solid #2e2e34;
    border-radius: 6px;
    padding: 6px 8px;
  }
  .bypass { background: none; border: none; color: #55555c; cursor: pointer; font-size: 12px; padding: 0; }
  .bypass.on { color: #7dc97f; }
  .pname { flex: 1; font-size: 11px; color: #c9c9cf; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .pname.bypassed { color: #5a5a62; text-decoration: line-through; }
  .open, .rm {
    background: #2a2a2f; border: 1px solid #38383e; border-radius: 4px;
    color: #8a8a92; font-size: 10px; cursor: pointer; padding: 2px 7px;
  }
  .open:hover { color: #d4a373; }
  .rm:hover { color: #e06c60; }

  .browser { flex: 1; display: flex; flex-direction: column; border-top: 1px solid #0c0c0e; overflow: hidden; }
  .browser-head { display: flex; gap: 6px; padding: 8px; }
  input {
    flex: 1;
    background: #141416; border: 1px solid #2e2e34; border-radius: 6px;
    color: #d8d8dc; font-size: 11px; padding: 6px 9px; outline: none;
  }
  input:focus { border-color: #d4a373; }
  .scan {
    background: #2a2a2f; border: 1px solid #38383e; border-radius: 6px;
    color: #8a8a92; font-size: 11px; cursor: pointer; padding: 0 10px;
  }
  .scan:hover:not(:disabled) { color: #d4a373; }
  .scan:disabled { opacity: 0.5; }

  .results { flex: 1; overflow-y: auto; padding: 0 8px 8px; display: flex; flex-direction: column; gap: 2px; }
  .result {
    display: flex; align-items: center; gap: 7px;
    background: none; border: none; border-radius: 5px;
    padding: 5px 7px; cursor: pointer; text-align: left;
  }
  .result:hover { background: #26262b; }
  .fmt {
    font-size: 8px; font-weight: 700; color: #6ecbdc;
    border: 1px solid #2e5a64; border-radius: 3px; padding: 1px 4px;
  }
  .rname { flex: 1; font-size: 11px; color: #c9c9cf; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .cat { font-size: 9px; color: #5a5a62; }
</style>
