<script>
  import { tracks, rigTrack, send } from '../stores.js'
  import Knob from './Knob.svelte'

  $: ti = $rigTrack
  $: track = ti != null ? $tracks[ti] : null
  // Engine indexes plugins within the full chain (incl. volume/meter); keep idx.
  $: chain = track ? track.plugins.map((p, idx) => ({ ...p, idx })) : []
  $: amp = chain.find((p) => p.type === 'espressoNamAmp') || null
  $: cab = chain.find((p) => p.type === 'impulseResponse') || null
  $: hasRig = !!amp

  const close = () => rigTrack.set(null)
  const setParam = (idx, param, value) => send({ cmd: 'setPluginParam', track: ti, plugin: idx, param, value })
  const toggle = (idx, on) => send({ cmd: 'setPluginEnabled', track: ti, plugin: idx, on })

  async function loadModel () {
    const file = await window.espresso.openFile({
      title: 'Load Neural Amp capture',
      filters: [{ name: 'NAM capture', extensions: ['nam'] }]
    })
    if (file) send({ cmd: 'loadAmpModel', track: ti, plugin: amp.idx, path: file })
  }
  async function loadIR () {
    const file = await window.espresso.openFile({
      title: 'Load cabinet impulse response',
      filters: [{ name: 'Impulse response', extensions: ['wav', 'flac', 'aiff', 'aif'] }]
    })
    if (file) send({ cmd: 'loadCabIR', track: ti, plugin: cab.idx, path: file })
  }
</script>

{#if track}
  <section class="rig">
    <header class="chrome">
      <span class="badge">RIG</span>
      <span class="title">{track.name}</span>
      <button class="close" on:click={close} title="Close rig">×</button>
    </header>

    {#if !hasRig}
      <div class="empty">
        <p>This track has no guitar rig yet.</p>
        <button class="cta" on:click={() => send({ cmd: 'addGuitarRig', track: ti })}>
          + Add Neural Amp + Cabinet
        </button>
      </div>
    {:else}
      <div class="devices">
        <!-- ── Amp head ── -->
        <article class="device amp" class:off={!amp.enabled}>
          <div class="dev-head">
            <button class="power" class:on={amp.enabled} title={amp.enabled ? 'Bypass amp' : 'Enable amp'}
                    on:click={() => toggle(amp.idx, !amp.enabled)}>⏻</button>
            <span class="dev-title">Neural Amp</span>
          </div>
          <div class="face">
            <div class="nameplate">
              <span class="model" class:loaded={amp.modelLoaded}
                    title={amp.modelLoaded ? amp.modelName : 'no capture loaded'}>
                {amp.modelName || 'no capture loaded'}
              </span>
              <button class="load" on:click={loadModel}>load .nam</button>
            </div>
            <div class="knobs">
              <Knob label="Input" min={-12} max={24} value={amp.inputDb} resetValue={0}
                    format={(v) => v.toFixed(1) + ' dB'}
                    on:change={(e) => setParam(amp.idx, 'inputGain', e.detail)} />
              <Knob label="Output" min={-24} max={12} value={amp.outputDb} resetValue={0}
                    format={(v) => v.toFixed(1) + ' dB'}
                    on:change={(e) => setParam(amp.idx, 'outputGain', e.detail)} />
            </div>
          </div>
        </article>

        <div class="flow" aria-hidden="true">▸</div>

        <!-- ── Cabinet ── -->
        {#if cab}
          <article class="device cab" class:off={!cab.enabled}>
            <div class="dev-head">
              <button class="power" class:on={cab.enabled} title={cab.enabled ? 'Bypass cabinet' : 'Enable cabinet'}
                      on:click={() => toggle(cab.idx, !cab.enabled)}>⏻</button>
              <span class="dev-title">Cabinet IR</span>
            </div>
            <div class="grille">
              <div class="nameplate">
                <span class="model" class:loaded={cab.irLoaded}
                      title={cab.irLoaded ? cab.irName : 'no IR loaded'}>
                  {cab.irName || 'no IR loaded'}
                </span>
                <button class="load" on:click={loadIR}>load IR</button>
              </div>
              <div class="knobs">
                <Knob label="Gain" min={-12} max={6} value={cab.cabGainDb} resetValue={0}
                      format={(v) => v.toFixed(1) + ' dB'}
                      on:change={(e) => setParam(cab.idx, 'gain', e.detail)} />
                <Knob label="Mix" min={0} max={1} value={cab.cabMix} resetValue={1}
                      format={(v) => Math.round(v * 100) + '%'}
                      on:change={(e) => setParam(cab.idx, 'mix', e.detail)} />
              </div>
            </div>
          </article>
        {/if}
      </div>
    {/if}
  </section>
{/if}

<style>
  .rig {
    height: 280px;
    min-height: 280px;
    display: flex;
    flex-direction: column;
    background: var(--es-surface-2);
    border-top: 1px solid var(--es-edge);
    box-shadow: var(--es-shadow-2);
  }

  .chrome {
    display: flex;
    align-items: center;
    gap: var(--es-sp-2);
    padding: 8px 12px;
    background: var(--es-surface-3);
    border-bottom: 1px solid var(--es-hairline);
  }
  .badge {
    font-size: 9px;
    font-weight: 800;
    letter-spacing: 1.5px;
    color: var(--es-accent-deep);
    border: 1px solid var(--es-accent-deep);
    border-radius: var(--es-r-sm);
    padding: 1px 6px;
  }
  .title { flex: 1; font-size: 12px; font-weight: 700; color: var(--es-accent); }
  .close {
    background: none; border: none; cursor: pointer;
    color: var(--es-text-muted); font-size: 18px; line-height: 1;
  }
  .close:hover { color: var(--es-danger); }

  .empty {
    flex: 1;
    display: flex; flex-direction: column;
    align-items: center; justify-content: center; gap: var(--es-sp-3);
    color: var(--es-text-dim);
    font-size: 13px;
  }
  .cta {
    background: var(--es-surface-4);
    border: 1px solid var(--es-accent-deep);
    border-radius: var(--es-r-lg);
    color: var(--es-accent);
    font-size: 13px; font-weight: 600;
    padding: 10px 18px; cursor: pointer;
  }
  .cta:hover { background: var(--es-accent); color: var(--es-bg); }

  .devices {
    flex: 1;
    display: flex;
    align-items: stretch;
    gap: var(--es-sp-3);
    padding: var(--es-sp-4);
    overflow-x: auto;
  }

  .device {
    display: flex;
    flex-direction: column;
    border-radius: var(--es-r-lg);
    border: 1px solid var(--es-edge);
    box-shadow: var(--es-shadow-1), inset 0 1px 0 rgba(255, 255, 255, 0.04);
    overflow: hidden;
    transition: opacity 0.15s;
  }
  .device.off { opacity: 0.45; }
  .amp { flex: 1.3; background: var(--es-amp-tolex); }
  .cab { flex: 1; background: var(--es-amp-tolex); }

  .dev-head {
    display: flex;
    align-items: center;
    gap: var(--es-sp-2);
    padding: 6px 10px;
    background: rgba(0, 0, 0, 0.3);
    border-bottom: 1px solid rgba(0, 0, 0, 0.5);
  }
  .power {
    width: 18px; height: 18px;
    border-radius: 50%;
    border: 1px solid var(--es-border-2);
    background: var(--es-inset);
    color: var(--es-text-faint);
    font-size: 10px; line-height: 1; cursor: pointer; padding: 0;
  }
  .power.on {
    color: var(--es-green);
    border-color: var(--es-green);
    box-shadow: 0 0 6px rgba(125, 201, 127, 0.5);
  }
  .dev-title {
    font-size: 11px; font-weight: 700; letter-spacing: 0.5px;
    color: var(--es-text-2);
    text-shadow: 0 1px 2px rgba(0, 0, 0, 0.6);
  }

  .face {
    flex: 1;
    display: flex; flex-direction: column;
    gap: var(--es-sp-3);
    padding: var(--es-sp-3);
    background: var(--es-amp-panel);
    margin: var(--es-sp-2);
    border-radius: var(--es-r);
    box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.4), inset 0 2px 8px rgba(0, 0, 0, 0.4);
  }
  .grille {
    flex: 1;
    display: flex; flex-direction: column;
    gap: var(--es-sp-3);
    padding: var(--es-sp-3);
    background: var(--es-cab-grille);
    margin: var(--es-sp-2);
    border-radius: var(--es-r);
    box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.5), inset 0 2px 10px rgba(0, 0, 0, 0.5);
  }

  .nameplate {
    display: flex;
    align-items: center;
    gap: var(--es-sp-2);
    padding: 6px 10px;
    background: var(--es-nameplate);
    border-radius: var(--es-r-sm);
    border: 1px solid rgba(0, 0, 0, 0.6);
    box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.7);
  }
  .model {
    flex: 1;
    font-family: var(--es-mono);
    font-size: 11px;
    color: var(--es-text-dim);
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .model.loaded { color: var(--es-amber); text-shadow: 0 0 6px rgba(232, 196, 104, 0.4); }
  .load {
    background: var(--es-surface-4);
    border: 1px solid var(--es-border-2);
    border-radius: var(--es-r-sm);
    color: var(--es-text-muted);
    font-size: 10px; cursor: pointer; padding: 3px 8px;
    white-space: nowrap;
  }
  .load:hover { color: var(--es-accent); border-color: var(--es-accent); }

  .knobs {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: var(--es-sp-5);
  }

  .flow {
    align-self: center;
    color: var(--es-accent-deep);
    font-size: 20px;
  }
</style>
