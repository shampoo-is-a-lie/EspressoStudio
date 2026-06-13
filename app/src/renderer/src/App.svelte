<script>
  import { initEngineBridge } from './stores.js'
  import TransportBar from './components/TransportBar.svelte'
  import TrackList from './components/TrackList.svelte'
  import Arrange from './components/Arrange.svelte'
  import FxPanel from './components/FxPanel.svelte'
  import GuitarRig from './components/GuitarRig.svelte'

  initEngineBridge()
</script>

<main>
  <TransportBar />
  <div class="body">
    <div class="workspace">
      <TrackList />
      <Arrange />
      <FxPanel />
    </div>
    <GuitarRig />
  </div>
</main>

<style>
  :global(body) {
    margin: 0;
    background: var(--es-bg);
    color: var(--es-text);
    font-family: var(--es-font);
    user-select: none;
    overflow: hidden;
  }
  main { display: flex; flex-direction: column; height: 100vh; }
  .body { flex: 1; display: flex; flex-direction: column; overflow: hidden; }
  .workspace { flex: 1; display: flex; overflow: hidden; }
</style>
